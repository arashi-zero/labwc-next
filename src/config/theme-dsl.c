// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/theme-dsl.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/log.h>
#include "common/dir.h"
#include "common/list.h"
#include "common/mem.h"
#include "common/node-type.h"
#include "ssd.h"

/* ================================================================== vars */

#define DSL_VARS_MAX 128
static struct { char *name; char *value; } g_vars[DSL_VARS_MAX];
static int g_vars_count;

void
dsl_vars_clear(void)
{
	for (int i = 0; i < g_vars_count; i++) {
		free(g_vars[i].name);
		free(g_vars[i].value);
		g_vars[i].name = NULL;
		g_vars[i].value = NULL;
	}
	g_vars_count = 0;
}

void
dsl_var_set(const char *name, const char *value)
{
	/* update existing */
	for (int i = 0; i < g_vars_count; i++) {
		if (!strcmp(g_vars[i].name, name)) {
			free(g_vars[i].value);
			g_vars[i].value = xstrdup(value);
			return;
		}
	}
	if (g_vars_count >= DSL_VARS_MAX) {
		wlr_log(WLR_ERROR, "theme-dsl: too many variables (max %d)", DSL_VARS_MAX);
		return;
	}
	g_vars[g_vars_count].name  = xstrdup(name);
	g_vars[g_vars_count].value = xstrdup(value);
	g_vars_count++;
}

const char *
dsl_var_resolve(const char *s)
{
	if (!s || s[0] != '$') {
		return s;
	}
	const char *name = s + 1;
	for (int i = 0; i < g_vars_count; i++) {
		if (!strcmp(g_vars[i].name, name)) {
			return g_vars[i].value;
		}
	}
	wlr_log(WLR_ERROR, "theme-dsl: undefined variable '$%s'", name);
	return s;
}

/* ================================================================== color helpers */

static unsigned
hex_nibble(char c)
{
	if (c >= '0' && c <= '9') return (unsigned)(c - '0');
	if (c >= 'a' && c <= 'f') return (unsigned)(c - 'a' + 10);
	if (c >= 'A' && c <= 'F') return (unsigned)(c - 'A' + 10);
	return 0;
}

/*
 * Parse #RGB / #RRGGBB / #RRGGBBAA into premultiplied float[4].
 * Returns false on parse failure (rgba left unchanged).
 */
static bool
parse_hex_color(const char *s, float rgba[4])
{
	if (!s) return false;
	s = dsl_var_resolve(s);
	if (!s || s[0] != '#') return false;

	size_t len = strlen(s);
	if (len == 4) {
		/* #RGB → expand each nibble */
		rgba[0] = (float)(hex_nibble(s[1]) * 17) / 255.0f;
		rgba[1] = (float)(hex_nibble(s[2]) * 17) / 255.0f;
		rgba[2] = (float)(hex_nibble(s[3]) * 17) / 255.0f;
		rgba[3] = 1.0f;
	} else if (len == 7) {
		rgba[0] = (float)(hex_nibble(s[1]) * 16 + hex_nibble(s[2])) / 255.0f;
		rgba[1] = (float)(hex_nibble(s[3]) * 16 + hex_nibble(s[4])) / 255.0f;
		rgba[2] = (float)(hex_nibble(s[5]) * 16 + hex_nibble(s[6])) / 255.0f;
		rgba[3] = 1.0f;
	} else if (len == 9) {
		rgba[0] = (float)(hex_nibble(s[1]) * 16 + hex_nibble(s[2])) / 255.0f;
		rgba[1] = (float)(hex_nibble(s[3]) * 16 + hex_nibble(s[4])) / 255.0f;
		rgba[2] = (float)(hex_nibble(s[5]) * 16 + hex_nibble(s[6])) / 255.0f;
		rgba[3] = (float)(hex_nibble(s[7]) * 16 + hex_nibble(s[8])) / 255.0f;
	} else {
		return false;
	}

	/* pre-multiply alpha as expected by wlr_scene */
	rgba[0] *= rgba[3];
	rgba[1] *= rgba[3];
	rgba[2] *= rgba[3];
	return true;
}

static void
color_set_unset(float rgba[4])
{
	rgba[0] = rgba[1] = rgba[2] = 0.0f;
	rgba[3] = -1.0f;
}

static bool
color_is_set(const float rgba[4])
{
	return rgba[3] >= 0.0f;
}

/* ================================================================== node alloc */

static struct dsl_node *
node_new(enum dsl_node_type type)
{
	struct dsl_node *n = xzalloc(sizeof(*n));
	n->type   = type;
	n->width  = -1;
	n->height = -1;
	n->spacing = -1;
	n->corner_radius = -1;
	n->hover_corner_radius = -1;
	n->border_width = -1;
	n->shadow_size  = -1;
	n->icon_size    = -1;
	n->justify      = -1;
	struct dsl_box unset = DSL_BOX_UNSET;
	n->padding = unset;
	n->margin  = unset;
	color_set_unset(n->border_color);
	color_set_unset(n->border_color_inactive);
	color_set_unset(n->bg);
	color_set_unset(n->bg_inactive);
	color_set_unset(n->hover_bg);
	color_set_unset(n->color);
	color_set_unset(n->color_inactive);
	color_set_unset(n->shadow_color);
	wl_list_init(&n->children);
	wl_list_init(&n->sibling);
	return n;
}

void
dsl_node_free(struct dsl_node *node)
{
	if (!node) return;
	struct dsl_node *child, *tmp;
	wl_list_for_each_safe(child, tmp, &node->children, sibling) {
		wl_list_remove(&child->sibling);
		dsl_node_free(child);
	}
	free(node->name);
	free(node->icon_name);
	free(node);
}

/* ================================================================== tokenizer */

/*
 * Minimal hand-written tokenizer.
 *
 * Tokens:
 *   WORD   — alphanumeric, hyphen, underscore, $, #, dot, slash
 *   LBRACE — {
 *   RBRACE — }
 *   COLON  — :
 *   SEMI   — ;  (optional property separator, same as newline)
 *   EOF
 *
 * Comments start with # only when '#' is not the first char of a WORD
 * that looks like a color — we handle this by only treating '#' as
 * a comment when it follows whitespace at the start of a non-color token.
 * Actually simpler: '#' at start of a token is part of a color string;
 * '#' after whitespace on a fresh line is a comment.  We track this with
 * a "line_start" flag.
 */

enum tok_type { TOK_WORD, TOK_LBRACE, TOK_RBRACE, TOK_COLON, TOK_SEMI, TOK_EOF };

struct tokenizer {
	const char *src;
	size_t      pos;
	int         line;

	/* lookahead single token */
	enum tok_type la_type;
	char          la_val[256];
	bool          la_ready;
};

static void
tok_init(struct tokenizer *t, const char *src)
{
	memset(t, 0, sizeof(*t));
	t->src  = src;
	t->line = 1;
}

/* skip whitespace and comments; return false at EOF */
static bool
tok_skip_ws(struct tokenizer *t)
{
	while (t->src[t->pos]) {
		char c = t->src[t->pos];
		if (c == '\n') { t->line++; t->pos++; continue; }
		if (c == '\r' || c == ' ' || c == '\t') { t->pos++; continue; }
		/* comment: // */
		if (c == '/' && t->src[t->pos + 1] == '/') {
			while (t->src[t->pos] && t->src[t->pos] != '\n') t->pos++;
			continue;
		}
		/* block comment: /star ... star/ */
		if (c == '/' && t->src[t->pos + 1] == '*') {
			t->pos += 2;
			while (t->src[t->pos]) {
				if (t->src[t->pos] == '*' && t->src[t->pos + 1] == '/') {
					t->pos += 2;
					break;
				}
				if (t->src[t->pos] == '\n') t->line++;
				t->pos++;
			}
			continue;
		}
		/* comment: # only at start of a line-token that isn't a color */
		/* We use a simpler rule: if '#' follows a newline or is at pos 0
		 * and the *next char* is not a hex digit or '#', treat as comment.
		 * Colors start with '#' followed by hex digits. */
		if (c == '#') {
			/* peek next char */
			char n = t->src[t->pos + 1];
			bool is_hex = (n >= '0' && n <= '9') ||
			              (n >= 'a' && n <= 'f') ||
			              (n >= 'A' && n <= 'F');
			if (!is_hex) {
				/* line comment */
				while (t->src[t->pos] && t->src[t->pos] != '\n') t->pos++;
				continue;
			}
		}
		return true;
	}
	return false;
}

static void
tok_read(struct tokenizer *t, enum tok_type *type, char *val, size_t valsz)
{
	val[0] = '\0';
	if (!tok_skip_ws(t)) { *type = TOK_EOF; return; }

	char c = t->src[t->pos];
	switch (c) {
	case '{': *type = TOK_LBRACE; t->pos++; return;
	case '}': *type = TOK_RBRACE; t->pos++; return;
	case ':': *type = TOK_COLON;  t->pos++; return;
	case ';': *type = TOK_SEMI;   t->pos++; return;
	}

	/* WORD: everything that isn't a structural char or whitespace */
	*type = TOK_WORD;
	size_t i = 0;
	while (t->src[t->pos]) {
		char ch = t->src[t->pos];
		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
		    ch == '{' || ch == '}' || ch == ':' || ch == ';' ||
		    (ch == '/' && (t->src[t->pos+1] == '/' || t->src[t->pos+1] == '*'))) {
			break;
		}
		if (i < valsz - 1) val[i++] = ch;
		t->pos++;
	}
	val[i] = '\0';
}

/* peek without consuming */
static enum tok_type
tok_peek(struct tokenizer *t, char *val, size_t valsz)
{
	if (!t->la_ready) {
		tok_read(t, &t->la_type, t->la_val, sizeof(t->la_val));
		t->la_ready = true;
	}
	if (val) {
		size_t n = strlen(t->la_val);
		if (n >= valsz) n = valsz - 1;
		memcpy(val, t->la_val, n);
		val[n] = '\0';
	}
	return t->la_type;
}

static enum tok_type
tok_next(struct tokenizer *t, char *val, size_t valsz)
{
	if (t->la_ready) {
		t->la_ready = false;
		enum tok_type ty = t->la_type;
		if (val) {
			size_t n = strlen(t->la_val);
			if (n >= valsz) n = valsz - 1;
			memcpy(val, t->la_val, n);
			val[n] = '\0';
		}
		return ty;
	}
	enum tok_type ty;
	tok_read(t, &ty, val ? val : (char[256]){""}, val ? valsz : 256);
	return ty;
}

/* ================================================================== box shorthand parser */

/*
 * Parse up to 4 integer values from @vals (the remaining tokens on a
 * property line, already collected as strings) into a dsl_box using
 * CSS shorthand semantics.
 */
static void
parse_box_shorthand(const char *vals[], int count, struct dsl_box *box)
{
	int v[4] = { 0, 0, 0, 0 };
	int n = count < 4 ? count : 4;
	for (int i = 0; i < n; i++) {
		v[i] = atoi(vals[i]);
	}
	switch (n) {
	case 1: box->top = box->right = box->bottom = box->left = v[0]; break;
	case 2: box->top = box->bottom = v[0]; box->left = box->right = v[1]; break;
	case 3: box->top = v[0]; box->left = box->right = v[1]; box->bottom = v[2]; break;
	case 4: box->top = v[0]; box->right = v[1]; box->bottom = v[2]; box->left = v[3]; break;
	}
}

/* ================================================================== property parser */

/*
 * collect_values: after consuming a COLON, read all WORD tokens until
 * the next structural token (LBRACE / RBRACE / SEMI) or end-of-line
 * (we approximate EOL by stopping at EOF too).
 *
 * Returns number of values collected.  vals[] are static storage inside
 * the function — caller must use them before next call.
 *
 * Because our tokenizer eats newlines, we use SEMI as an explicit
 * property terminator; bare newlines also work because consecutive
 * properties without braces each start with a WORD token.
 */
#define PROP_VALS_MAX 8

static int
collect_values(struct tokenizer *t, const char *vals[PROP_VALS_MAX])
{
	static char storage[PROP_VALS_MAX][256];
	int count = 0;
	while (count < PROP_VALS_MAX) {
		char tmp[256];
		enum tok_type ty = tok_peek(t, tmp, sizeof(tmp));
		if (ty == TOK_EOF || ty == TOK_LBRACE || ty == TOK_RBRACE ||
		    ty == TOK_COLON) {
			break;
		}
		if (ty == TOK_SEMI) {
			tok_next(t, NULL, 0); /* consume */
			break;
		}
		tok_next(t, tmp, sizeof(tmp));
		strncpy(storage[count], tmp, 255);
		storage[count][255] = '\0';
		vals[count] = storage[count];
		count++;
	}
	return count;
}

/*
 * apply_property: given a key string and values array, set the
 * corresponding fields on @node.
 */
static void
apply_property(struct dsl_node *node, const char *key,
	const char *vals[], int nv)
{
	if (!nv) return;

	const char *v0 = dsl_var_resolve(vals[0]);

	/* --- box model --- */
	if (!strcasecmp(key, "padding")) {
		parse_box_shorthand(vals, nv, &node->padding);
		return;
	}
	if (!strcasecmp(key, "margin")) {
		parse_box_shorthand(vals, nv, &node->margin);
		return;
	}
	if (!strcasecmp(key, "padding-top"))    { node->padding.top    = atoi(v0); return; }
	if (!strcasecmp(key, "padding-right"))  { node->padding.right  = atoi(v0); return; }
	if (!strcasecmp(key, "padding-bottom")) { node->padding.bottom = atoi(v0); return; }
	if (!strcasecmp(key, "padding-left"))   { node->padding.left   = atoi(v0); return; }
	if (!strcasecmp(key, "margin-top"))     { node->margin.top    = atoi(v0); return; }
	if (!strcasecmp(key, "margin-right"))   { node->margin.right  = atoi(v0); return; }
	if (!strcasecmp(key, "margin-bottom"))  { node->margin.bottom = atoi(v0); return; }
	if (!strcasecmp(key, "margin-left"))    { node->margin.left   = atoi(v0); return; }

	/* --- size --- */
	if (!strcasecmp(key, "width"))  { node->width  = atoi(v0); return; }
	if (!strcasecmp(key, "height")) { node->height = atoi(v0); return; }
	if (!strcasecmp(key, "size"))   { node->width = node->height = atoi(v0); return; }
	if (!strcasecmp(key, "spacing")) { node->spacing = atoi(v0); return; }
	if (!strcasecmp(key, "corner-radius")) { node->corner_radius = atoi(v0); return; }

	/* --- border: width [color] --- */
	if (!strcasecmp(key, "border")) {
		node->border_width = atoi(v0);
		if (nv >= 2) {
			parse_hex_color(dsl_var_resolve(vals[1]), node->border_color);
		}
		return;
	}
	if (!strcasecmp(key, "border-width")) { node->border_width = atoi(v0); return; }
	if (!strcasecmp(key, "border-color")) {
		parse_hex_color(v0, node->border_color);
		return;
	}
	if (!strcasecmp(key, "border-color-inactive")) {
		parse_hex_color(v0, node->border_color_inactive);
		return;
	}

	/* --- background --- */
	if (!strcasecmp(key, "background") || !strcasecmp(key, "bg")) {
		parse_hex_color(v0, node->bg);
		return;
	}
	if (!strcasecmp(key, "background-inactive") || !strcasecmp(key, "bg-inactive")) {
		parse_hex_color(v0, node->bg_inactive);
		return;
	}

	/* --- hover background (buttons) --- */
	if (!strcasecmp(key, "hover-bg")) {
		parse_hex_color(v0, node->hover_bg);
		return;
	}
	if (!strcasecmp(key, "hover-corner-radius")) {
		node->hover_corner_radius = atoi(v0);
		return;
	}

	/* --- text/icon color --- */
	if (!strcasecmp(key, "color")) {
		parse_hex_color(v0, node->color);
		return;
	}
	if (!strcasecmp(key, "color-inactive")) {
		parse_hex_color(v0, node->color_inactive);
		return;
	}

	/* --- shadow (Window) --- */
	if (!strcasecmp(key, "shadow")) {
		node->shadow_size = atoi(v0);
		if (nv >= 2) {
			parse_hex_color(dsl_var_resolve(vals[1]), node->shadow_color);
		}
		return;
	}

	/* --- icon --- */
	if (!strcasecmp(key, "name")) {
		free(node->icon_name);
		node->icon_name = xstrdup(v0);
		return;
	}
	if (!strcasecmp(key, "icon-size")) {
		node->icon_size = atoi(v0);
		return;
	}

	/* --- text justify --- */
	if (!strcasecmp(key, "justify")) {
		if (!strcasecmp(v0, "left"))   { node->justify = 0; return; }
		if (!strcasecmp(v0, "center")) { node->justify = 1; return; }
		if (!strcasecmp(v0, "right"))  { node->justify = 2; return; }
	}

	wlr_log(WLR_DEBUG, "theme-dsl: unknown property '%s'", key);
}

/* ================================================================== recursive parser */

static const struct {
	const char *keyword;
	enum dsl_node_type type;
} node_keywords[] = {
	{ "Window",   DSL_NODE_WINDOW   },
	{ "Titlebar", DSL_NODE_TITLEBAR },
	{ "Left",     DSL_NODE_LEFT     },
	{ "Right",    DSL_NODE_RIGHT    },
	{ "Title",    DSL_NODE_TITLE    },
	{ "Button",   DSL_NODE_BUTTON   },
	{ "Icon",     DSL_NODE_ICON     },
	{ "AppIcon",  DSL_NODE_APP_ICON },
};
#define N_KEYWORDS (sizeof(node_keywords) / sizeof(node_keywords[0]))

static bool
is_node_keyword(const char *word, enum dsl_node_type *out_type)
{
	for (size_t i = 0; i < N_KEYWORDS; i++) {
		if (!strcasecmp(word, node_keywords[i].keyword)) {
			if (out_type) *out_type = node_keywords[i].type;
			return true;
		}
	}
	return false;
}

/* forward declaration */
static struct dsl_node *parse_node(struct tokenizer *t,
	enum dsl_node_type type, const char *name);

/*
 * parse_block: parse the body of a { ... } block, adding children
 * and properties to @parent.
 */
static void
parse_block(struct tokenizer *t, struct dsl_node *parent)
{
	char word[256];
	while (true) {
		enum tok_type ty = tok_peek(t, word, sizeof(word));
		if (ty == TOK_EOF || ty == TOK_RBRACE) {
			tok_next(t, NULL, 0); /* consume } or leave EOF */
			break;
		}

		if (ty != TOK_WORD) {
			/* skip unexpected structural token */
			tok_next(t, NULL, 0);
			continue;
		}
		tok_next(t, word, sizeof(word)); /* consume the word */

		enum dsl_node_type child_type;
		if (is_node_keyword(word, &child_type)) {
			/* may have an optional name before { */
			char name_or_brace[256] = "";
			const char *child_name = NULL;
			enum tok_type nt = tok_peek(t, name_or_brace, sizeof(name_or_brace));
			if (nt == TOK_WORD) {
				enum dsl_node_type dummy;
				if (!is_node_keyword(name_or_brace, &dummy)) {
					/* treat as name */
					tok_next(t, name_or_brace, sizeof(name_or_brace));
					child_name = name_or_brace;
				}
			}
			/* expect { */
			nt = tok_peek(t, NULL, 0);
			if (nt == TOK_LBRACE) {
				tok_next(t, NULL, 0);
				struct dsl_node *child = parse_node(t, child_type, child_name);
				wl_list_insert(parent->children.prev, &child->sibling);
			}
		} else {
			/* property:  key: val1 val2 ... */
			enum tok_type ct = tok_peek(t, NULL, 0);
			if (ct == TOK_COLON) {
				tok_next(t, NULL, 0); /* consume : */
				const char *vals[PROP_VALS_MAX];
				int nv = collect_values(t, vals);
				apply_property(parent, word, vals, nv);
			}
		}
	}
}

static struct dsl_node *
parse_node(struct tokenizer *t, enum dsl_node_type type, const char *name)
{
	struct dsl_node *n = node_new(type);
	if (name) n->name = xstrdup(name);
	parse_block(t, n);
	return n;
}

/* ================================================================== top-level parse */

/*
 * parse_top: scan for $var declarations and top-level node blocks.
 * Returns root Window node or NULL.
 */
static struct dsl_node *
parse_top(struct tokenizer *t)
{
	struct dsl_node *root = NULL;
	char word[256];

	while (true) {
		enum tok_type ty = tok_next(t, word, sizeof(word));
		if (ty == TOK_EOF) break;
		if (ty != TOK_WORD) continue;

		/* variable declaration: $name = value */
		if (word[0] == '$') {
			char name[256];
			strncpy(name, word + 1, sizeof(name) - 1);
			name[sizeof(name)-1] = '\0';
			/* expect '=' — we repurpose COLON logic; in the DSL
			 * we use '=' for var assignment, so peek for WORD '=' */
			char eq[8];
			enum tok_type et = tok_peek(t, eq, sizeof(eq));
			if (et == TOK_WORD && eq[0] == '=') {
				tok_next(t, NULL, 0); /* consume = */
				char val[256];
				et = tok_next(t, val, sizeof(val));
				if (et == TOK_WORD) {
					dsl_var_set(name, dsl_var_resolve(val));
				}
			}
			continue;
		}

		/* top-level node */
		enum dsl_node_type ntype;
		if (is_node_keyword(word, &ntype)) {
			/* expect { */
			if (tok_peek(t, NULL, 0) == TOK_LBRACE) {
				tok_next(t, NULL, 0);
				struct dsl_node *node = parse_node(t, ntype, NULL);
				if (ntype == DSL_NODE_WINDOW && !root) {
					root = node;
				} else {
					/* future: support top-level Palette {} etc */
					dsl_node_free(node);
				}
			}
		}
	}
	return root;
}

/* ================================================================== file API */

struct dsl_node *
dsl_parse_file(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		wlr_log(WLR_ERROR, "theme-dsl: cannot open '%s': %s",
			path, strerror(errno));
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	rewind(f);
	if (sz <= 0) { fclose(f); return NULL; }

	char *src = xmalloc((size_t)sz + 1);
	if (fread(src, 1, (size_t)sz, f) != (size_t)sz) {
		fclose(f);
		free(src);
		wlr_log(WLR_ERROR, "theme-dsl: read error on '%s'", path);
		return NULL;
	}
	src[sz] = '\0';
	fclose(f);

	struct tokenizer t;
	tok_init(&t, src);
	struct dsl_node *root = parse_top(&t);
	free(src);

	if (!root) {
		wlr_log(WLR_ERROR, "theme-dsl: no Window { } block found in '%s'", path);
	} else {
		wlr_log(WLR_INFO, "theme-dsl: loaded '%s'", path);
	}
	return root;
}

/* ================================================================== apply to theme */

/*
 * Helper: resolve a dsl_box into a single integer (use top value,
 * assuming uniform when caller stored uniform shorthand).  If all four
 * sides are set identically we return that; otherwise return top.
 * Returns def if unset.
 */
static int __attribute__((unused))
box_uniform(const struct dsl_box *b, int def)
{
	if (b->top < 0) return def;
	return b->top;
}

static int
box_x(const struct dsl_box *b, int def)
{
	if (b->left >= 0) return b->left;
	if (b->top  >= 0) return b->top;
	return def;
}

static int
box_y(const struct dsl_box *b, int def)
{
	if (b->top >= 0) return b->top;
	return def;
}

/* find first child of given type (and optional name) */
static struct dsl_node *
find_child(struct dsl_node *parent, enum dsl_node_type type, const char *name)
{
	struct dsl_node *c;
	wl_list_for_each(c, &parent->children, sibling) {
		if (c->type != type) continue;
		if (name && (!c->name || strcasecmp(c->name, name) != 0)) continue;
		return c;
	}
	return NULL;
}

static void
apply_button_node(struct dsl_node *btn, const char *btn_name,
	struct theme *theme)
{
	if (!btn_name) return;

	struct {
		const char *name;
		enum lab_node_type type;
	} map[] = {
		{ "close",       LAB_NODE_BUTTON_CLOSE        },
		{ "maximize",    LAB_NODE_BUTTON_MAXIMIZE      },
		{ "iconify",     LAB_NODE_BUTTON_ICONIFY       },
		{ "minimize",    LAB_NODE_BUTTON_ICONIFY       },
		{ "shade",       LAB_NODE_BUTTON_SHADE         },
		{ "omnipresent", LAB_NODE_BUTTON_OMNIPRESENT   },
		{ "desk",        LAB_NODE_BUTTON_OMNIPRESENT   },
		{ "menu",        LAB_NODE_BUTTON_WINDOW_MENU   },
		{ "app-menu",    LAB_NODE_BUTTON_WINDOW_MENU   },
	};

	enum lab_node_type btype = (enum lab_node_type)-1;
	for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
		if (!strcasecmp(btn_name, map[i].name)) {
			btype = map[i].type;
			break;
		}
	}
	if (btype == (enum lab_node_type)-1) return;

	if (color_is_set(btn->color)) {
		memcpy(theme->window[SSD_ACTIVE].button_colors[btype],
			btn->color, sizeof(btn->color));
	}
	if (color_is_set(btn->color_inactive)) {
		memcpy(theme->window[SSD_INACTIVE].button_colors[btype],
			btn->color_inactive, sizeof(btn->color_inactive));
	}
	if (color_is_set(btn->hover_bg)) {
		memcpy(theme->window_button_hover_bg_color,
			btn->hover_bg, sizeof(btn->hover_bg));
	}
	if (btn->corner_radius >= 0) {
		theme->window_button_hover_bg_corner_radius = btn->corner_radius;
	}
	if (btn->hover_corner_radius >= 0) {
		theme->window_button_hover_bg_corner_radius = btn->hover_corner_radius;
	}

	/* icon size from nested Icon child */
	struct dsl_node *icon = find_child(btn, DSL_NODE_ICON, NULL);
	if (icon && icon->icon_size >= 0) {
		theme->window_button_icon_size = icon->icon_size;
	}
}

static void
apply_slot_node(struct dsl_node *slot, struct theme *theme)
{
	/* spacing between buttons */
	if (slot->spacing >= 0) {
		theme->window_button_spacing = slot->spacing;
	}
	struct dsl_node *c;
	wl_list_for_each(c, &slot->children, sibling) {
		if (c->type == DSL_NODE_BUTTON) {
			apply_button_node(c, c->name, theme);
		}
	}
}

static void
apply_titlebar_node(struct dsl_node *tb, struct theme *theme)
{
	if (tb->padding.top >= 0) {
		theme->window_titlebar_padding_height = tb->padding.top;
	}
	if (tb->padding.left >= 0) {
		theme->window_titlebar_padding_width = tb->padding.left;
	} else if (tb->padding.right >= 0) {
		theme->window_titlebar_padding_width = tb->padding.right;
	}

	if (color_is_set(tb->bg)) {
		memcpy(theme->window[SSD_ACTIVE].title_bg.color, tb->bg,
			sizeof(tb->bg));
	}
	if (color_is_set(tb->bg_inactive)) {
		memcpy(theme->window[SSD_INACTIVE].title_bg.color, tb->bg_inactive,
			sizeof(tb->bg_inactive));
	}
	if (color_is_set(tb->border_color)) {
		memcpy(theme->window[SSD_ACTIVE].titlebar_bottom_border_color,
			tb->border_color, sizeof(tb->border_color));
	}
	if (color_is_set(tb->border_color_inactive)) {
		memcpy(theme->window[SSD_INACTIVE].titlebar_bottom_border_color,
			tb->border_color_inactive, sizeof(tb->border_color_inactive));
	}
	if (tb->border_width >= 0) {
		theme->titlebar_bottom_border_width = tb->border_width;
	}

	struct dsl_node *title = find_child(tb, DSL_NODE_TITLE, NULL);
	if (title) {
		if (color_is_set(title->color)) {
			memcpy(theme->window[SSD_ACTIVE].label_text_color,
				title->color, sizeof(title->color));
		}
		if (color_is_set(title->color_inactive)) {
			memcpy(theme->window[SSD_INACTIVE].label_text_color,
				title->color_inactive, sizeof(title->color_inactive));
		}
		if (title->justify == 0) theme->window_label_text_justify = LAB_JUSTIFY_LEFT;
		if (title->justify == 1) theme->window_label_text_justify = LAB_JUSTIFY_CENTER;
		if (title->justify == 2) theme->window_label_text_justify = LAB_JUSTIFY_RIGHT;
	}

	/* AppIcon in a Left slot */
	struct dsl_node *left  = find_child(tb, DSL_NODE_LEFT, NULL);
	struct dsl_node *right = find_child(tb, DSL_NODE_RIGHT, NULL);
	if (left) apply_slot_node(left, theme);
	if (right) apply_slot_node(right, theme);

	/* Button size: check any Button child for width/height/padding */
	struct dsl_node *any_btn = find_child(right ? right : left, DSL_NODE_BUTTON, NULL);
	if (!any_btn && left)  any_btn = find_child(left,  DSL_NODE_BUTTON, NULL);
	if (!any_btn && right) any_btn = find_child(right, DSL_NODE_BUTTON, NULL);
	if (any_btn) {
		if (any_btn->width  >= 0) theme->window_button_width  = any_btn->width;
		if (any_btn->height >= 0) theme->window_button_height = any_btn->height;
		/* derive button size from padding + icon size if no explicit width */
		if (any_btn->width < 0 && any_btn->padding.left >= 0) {
			struct dsl_node *ic = find_child(any_btn, DSL_NODE_ICON, NULL);
			if (ic && ic->icon_size >= 0) {
				int pad_x = box_x(&any_btn->padding, 0) * 2;
				int pad_y = box_y(&any_btn->padding, 0) * 2;
				theme->window_button_width  = ic->icon_size + pad_x;
				theme->window_button_height = ic->icon_size + pad_y;
			}
		}
	}
}

static void
apply_window_node(struct dsl_node *win, struct theme *theme)
{
	if (win->border_width >= 0) {
		theme->border_width = win->border_width;
	}
	if (color_is_set(win->border_color)) {
		memcpy(theme->window[SSD_ACTIVE].border_color,
			win->border_color, sizeof(win->border_color));
	}
	if (color_is_set(win->border_color_inactive)) {
		memcpy(theme->window[SSD_INACTIVE].border_color,
			win->border_color_inactive, sizeof(win->border_color_inactive));
	}
	if (win->corner_radius >= 0) {
		/* stored on rc, not theme — apply via theme field if added */
	}
	if (win->shadow_size >= 0) {
		theme->window[SSD_ACTIVE].shadow_size   = win->shadow_size;
		theme->window[SSD_INACTIVE].shadow_size = win->shadow_size;
	}
	if (color_is_set(win->shadow_color)) {
		memcpy(theme->window[SSD_ACTIVE].shadow_color,
			win->shadow_color, sizeof(win->shadow_color));
		memcpy(theme->window[SSD_INACTIVE].shadow_color,
			win->shadow_color, sizeof(win->shadow_color));
	}
	if (color_is_set(win->bg)) {
		memcpy(theme->window[SSD_ACTIVE].title_bg.color, win->bg,
			sizeof(win->bg));
	}
	if (color_is_set(win->bg_inactive)) {
		memcpy(theme->window[SSD_INACTIVE].title_bg.color, win->bg_inactive,
			sizeof(win->bg_inactive));
	}

	struct dsl_node *tb = find_child(win, DSL_NODE_TITLEBAR, NULL);
	if (tb) {
		apply_titlebar_node(tb, theme);
	}
}

void
dsl_apply_to_theme(struct dsl_node *root, struct theme *theme)
{
	if (!root || root->type != DSL_NODE_WINDOW) {
		wlr_log(WLR_ERROR, "theme-dsl: root node is not Window");
		return;
	}
	apply_window_node(root, theme);
}

/* ================================================================== entry point */

bool
theme_dsl_read(struct theme *theme)
{
	struct wl_list paths;
	/* look for theme.labwc in config dir */
	paths_config_create(&paths, "theme.labwc");

	bool found = false;
	struct path *p;
	dsl_vars_clear();
	wl_list_for_each(p, &paths, link) {
		struct dsl_node *root = dsl_parse_file(p->string);
		if (!root) continue;
		dsl_apply_to_theme(root, theme);
		dsl_node_free(root);
		found = true;
		break; /* first file wins */
	}
	dsl_vars_clear();
	paths_destroy(&paths);
	return found;
}
