// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/theme-toml.h"
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/log.h>
#include "tomlc17.h"
#include "common/dir.h"
#include "common/list.h"
#include "common/mem.h"
#include "common/node-type.h"
#include "ssd.h"

/* ------------------------------------------------------------------ helpers */

static bool
toml_str(toml_datum_t tab, const char *key, const char **out)
{
	toml_datum_t d = toml_seek(tab, key);
	if (d.type != TOML_STRING) {
		return false;
	}
	*out = d.u.s;
	return true;
}

static bool
toml_bool(toml_datum_t tab, const char *key, bool *out)
{
	toml_datum_t d = toml_seek(tab, key);
	if (d.type != TOML_BOOLEAN) {
		return false;
	}
	*out = d.u.boolean;
	return true;
}

static bool
toml_int(toml_datum_t tab, const char *key, int *out)
{
	toml_datum_t d = toml_seek(tab, key);
	if (d.type != TOML_INT64) {
		return false;
	}
	*out = (int)d.u.int64;
	return true;
}

static unsigned
hex_to_dec(char c)
{
	return (c >= '0' && c <= '9') ? (unsigned)(c - '0') :
	       (c >= 'a' && c <= 'f') ? (unsigned)(c - 'a' + 10) :
	       (c >= 'A' && c <= 'F') ? (unsigned)(c - 'A' + 10) : 0;
}

/*
 * Parse a single #RRGGBB or #RRGGBBAA hex color into a float[4] rgba
 * that is pre-multiplied (as expected by wlr_scene).
 */
static void
parse_hex(const char *hex, float *rgba)
{
	if (!hex || hex[0] != '#') {
		return;
	}
	size_t len = strlen(hex);

	if (len == 4) {
		/* #RGB */
		rgba[0] = hex_to_dec(hex[1]) / 15.0f;
		rgba[1] = hex_to_dec(hex[2]) / 15.0f;
		rgba[2] = hex_to_dec(hex[3]) / 15.0f;
		rgba[3] = 1.0f;
	} else if (len == 7) {
		/* #RRGGBB */
		rgba[0] = (hex_to_dec(hex[1]) * 16 + hex_to_dec(hex[2])) / 255.0f;
		rgba[1] = (hex_to_dec(hex[3]) * 16 + hex_to_dec(hex[4])) / 255.0f;
		rgba[2] = (hex_to_dec(hex[5]) * 16 + hex_to_dec(hex[6])) / 255.0f;
		rgba[3] = 1.0f;
	} else if (len == 9) {
		/* #RRGGBBAA */
		rgba[0] = (hex_to_dec(hex[1]) * 16 + hex_to_dec(hex[2])) / 255.0f;
		rgba[1] = (hex_to_dec(hex[3]) * 16 + hex_to_dec(hex[4])) / 255.0f;
		rgba[2] = (hex_to_dec(hex[5]) * 16 + hex_to_dec(hex[6])) / 255.0f;
		rgba[3] = (hex_to_dec(hex[7]) * 16 + hex_to_dec(hex[8])) / 255.0f;
	} else {
		wlr_log(WLR_ERROR, "theme-toml: invalid color '%s'", hex);
		return;
	}

	/* Pre-multiply as expected by wlr_scene */
	rgba[0] *= rgba[3];
	rgba[1] *= rgba[3];
	rgba[2] *= rgba[3];
}

static void
toml_color(toml_datum_t tab, const char *key, float *rgba)
{
	const char *s;
	if (toml_str(tab, key, &s)) {
		parse_hex(s, rgba);
	}
}

/*
 * Parse a TOML string array of up to 3 hex colors into colors[3][4],
 * e.g. ["#dddda6", "#000000", "#dddda6"]
 */
static void
toml_color_array(toml_datum_t tab, const char *key, float colors[3][4])
{
	toml_datum_t arr = toml_get(tab, key);
	if (arr.type != TOML_ARRAY) {
		return;
	}
	int count = arr.u.arr.size < 3 ? arr.u.arr.size : 3;
	for (int i = 0; i < count; i++) {
		toml_datum_t elem = arr.u.arr.elem[i];
		if (elem.type == TOML_STRING) {
			parse_hex(elem.u.s, colors[i]);
		}
	}
}

static enum lab_gradient
parse_gradient(const char *s)
{
	if (!strcasecmp(s, "vertical")) {
		return LAB_GRADIENT_VERTICAL;
	}
	if (!strcasecmp(s, "splitvertical")) {
		return LAB_GRADIENT_SPLITVERTICAL;
	}
	return LAB_GRADIENT_NONE;
}

static enum lab_justification
parse_justify(const char *s)
{
	if (!strcasecmp(s, "left")) {
		return LAB_JUSTIFY_LEFT;
	}
	if (!strcasecmp(s, "right")) {
		return LAB_JUSTIFY_RIGHT;
	}
	return LAB_JUSTIFY_CENTER;
}

/* ---------------------------------------------------------- section parsers */

/*
 * [theme.colors.window.button.active] / [theme.colors.window.button.inactive]
 *
 * Supported keys:
 *   all        — sets all button icon colors at once
 *   close / maximize / minimize / shade / omnipresent / menu
 */
static void
parse_button_colors(toml_datum_t tab, struct theme *theme,
	enum ssd_active_state active)
{
	const char *s;
	float rgba[4];

	if (toml_str(tab, "all", &s)) {
		parse_hex(s, rgba);
		for (enum lab_node_type t = LAB_NODE_BUTTON_FIRST;
				t <= LAB_NODE_BUTTON_LAST; t++) {
			memcpy(theme->window[active].button_colors[t], rgba,
				sizeof(rgba));
		}
	}

	struct {
		const char *key;
		enum lab_node_type type;
	} map[] = {
		{ "close",       LAB_NODE_BUTTON_CLOSE },
		{ "maximize",    LAB_NODE_BUTTON_MAXIMIZE },
		{ "minimize",    LAB_NODE_BUTTON_ICONIFY },
		{ "shade",       LAB_NODE_BUTTON_SHADE },
		{ "omnipresent", LAB_NODE_BUTTON_OMNIPRESENT },
		{ "menu",        LAB_NODE_BUTTON_WINDOW_MENU },
	};
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (toml_str(tab, map[i].key, &s)) {
			parse_hex(s, theme->window[active].button_colors[map[i].type]);
		}
	}
}

/*
 * [theme.colors.window.active] / [theme.colors.window.inactive]
 */
static void
parse_window_state(toml_datum_t sec, struct theme *theme,
	enum ssd_active_state active)
{
	const char *s;
	int i;

	toml_color(sec, "border", theme->window[active].border_color);
	toml_color(sec, "title-bg", theme->window[active].title_bg.color);
	toml_color(sec, "title-bg-to",
		theme->window[active].title_bg.color_to);
	toml_color(sec, "title-bg-split-to",
		theme->window[active].title_bg.color_split_to);
	toml_color(sec, "title-bg-to-split-to",
		theme->window[active].title_bg.color_to_split_to);
	toml_color(sec, "title-text", theme->window[active].label_text_color);
	toml_color(sec, "shadow-color", theme->window[active].shadow_color);

	if (toml_str(sec, "title-bg-gradient", &s)) {
		theme->window[active].title_bg.gradient = parse_gradient(s);
	}
	if (toml_int(sec, "shadow-size", &i) && i >= 0) {
		theme->window[active].shadow_size = i;
	}

	/* [theme.colors.window.active.button] / [theme.colors.window.inactive.button] */
	toml_datum_t btn = toml_get(sec, "button");
	if (btn.type == TOML_TABLE) {
		parse_button_colors(btn, theme, active);
	}
}

/*
 * [theme.colors.window]
 */
static void
parse_window_colors(toml_datum_t root, struct theme *theme)
{
	toml_datum_t sec = toml_seek(root, "theme.colors.window");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;
	const char *s;

	if (toml_int(sec, "titlebar-padding-width", &i) && i >= 0) {
		theme->window_titlebar_padding_width = i;
	}
	if (toml_int(sec, "titlebar-padding-height", &i) && i >= 0) {
		theme->window_titlebar_padding_height = i;
	}
	if (toml_str(sec, "title-justify", &s)) {
		theme->window_label_text_justify = parse_justify(s);
	}

	/* Shared button geometry — lives under [theme.colors.window.button] */
	toml_datum_t btn = toml_seek(sec, "button");
	if (btn.type == TOML_TABLE) {
		if (toml_int(btn, "width", &i) && i >= 1) {
			theme->window_button_width = i;
		}
		if (toml_int(btn, "height", &i) && i >= 1) {
			theme->window_button_height = i;
		}
		if (toml_int(btn, "spacing", &i) && i >= 0) {
			theme->window_button_spacing = i;
		}
		toml_color(btn, "hover-bg", theme->window_button_hover_bg_color);
		if (toml_int(btn, "hover-corner-radius", &i) && i >= 0) {
			theme->window_button_hover_bg_corner_radius = i;
		}
	}

	toml_datum_t active = toml_get(sec, "active");
	if (active.type == TOML_TABLE) {
		parse_window_state(active, theme, SSD_ACTIVE);
	}
	toml_datum_t inactive = toml_get(sec, "inactive");
	if (inactive.type == TOML_TABLE) {
		parse_window_state(inactive, theme, SSD_INACTIVE);
	}

	/* window.active.indicator.toggled-keybind.color equivalent */
	toml_color(sec, "toggled-keybind-indicator",
		theme->window_toggled_keybinds_color);
}

/*
 * [theme.colors.menu]
 */
static void
parse_menu_colors(toml_datum_t root, struct theme *theme)
{
	toml_datum_t sec = toml_seek(root, "theme.colors.menu");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;
	const char *s;

	if (toml_int(sec, "overlap-x", &i)) {
		theme->menu_overlap_x = i;
	}
	if (toml_int(sec, "overlap-y", &i)) {
		theme->menu_overlap_y = i;
	}
	if (toml_int(sec, "min-width", &i) && i >= 0) {
		theme->menu_min_width = i;
	}
	if (toml_int(sec, "max-width", &i) && i >= 0) {
		theme->menu_max_width = i;
	}
	if (toml_int(sec, "border-width", &i) && i >= 0) {
		theme->menu_border_width = i;
	}
	toml_color(sec, "border", theme->menu_border_color);
	toml_color(sec, "bg", theme->menu_items_bg_color);
	toml_color(sec, "text", theme->menu_items_text_color);
	toml_color(sec, "active-bg", theme->menu_items_active_bg_color);
	toml_color(sec, "active-text", theme->menu_items_active_text_color);
	if (toml_int(sec, "padding-x", &i) && i >= 0) {
		theme->menu_items_padding_x = i;
	}
	if (toml_int(sec, "padding-y", &i) && i >= 0) {
		theme->menu_items_padding_y = i;
	}
	if (toml_int(sec, "separator-width", &i) && i >= 0) {
		theme->menu_separator_line_thickness = i;
	}
	if (toml_int(sec, "separator-padding-width", &i) && i >= 0) {
		theme->menu_separator_padding_width = i;
	}
	if (toml_int(sec, "separator-padding-height", &i) && i >= 0) {
		theme->menu_separator_padding_height = i;
	}
	toml_color(sec, "separator-color", theme->menu_separator_color);
	toml_color(sec, "title-bg", theme->menu_title_bg_color);
	toml_color(sec, "title-text", theme->menu_title_text_color);
	if (toml_str(sec, "title-justify", &s)) {
		theme->menu_title_text_justify = parse_justify(s);
	}
}

/*
 * [theme.colors.osd]
 */
static void
parse_osd_colors(toml_datum_t root, struct theme *theme)
{
	toml_datum_t sec = toml_seek(root, "theme.colors.osd");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;

	toml_color(sec, "bg", theme->osd_bg_color);
	toml_color(sec, "border", theme->osd_border_color);
	if (toml_int(sec, "border-width", &i) && i >= 0) {
		theme->osd_border_width = i;
	}
	toml_color(sec, "text", theme->osd_label_text_color);

	/* [theme.colors.osd.switcher-classic] */
	toml_datum_t sc = toml_get(sec, "switcher-classic");
	if (sc.type == TOML_TABLE) {
		struct window_switcher_classic_theme *t =
			&theme->osd_window_switcher_classic;
		if (toml_int(sc, "width", &i) && i > 0) {
			t->width = i;
			t->width_is_percent = false;
		}
		if (toml_int(sc, "padding", &i) && i >= 0) {
			t->padding = i;
		}
		if (toml_int(sc, "item-padding-x", &i) && i >= 0) {
			t->item_padding_x = i;
		}
		if (toml_int(sc, "item-padding-y", &i) && i >= 0) {
			t->item_padding_y = i;
		}
		if (toml_int(sc, "item-active-border-width", &i) && i >= 0) {
			t->item_active_border_width = i;
		}
		toml_color(sc, "item-active-border",
			t->item_active_border_color);
		toml_color(sc, "item-active-bg",
			t->item_active_bg_color);
		toml_color(sc, "item-active-text",
			t->item_active_text_color);
		if (toml_int(sc, "item-icon-size", &i) && i > 0) {
			t->item_icon_size = i;
		}
	}
	/* [theme.colors.osd.switcher-thumbnail] */
	toml_datum_t st = toml_get(sec, "switcher-thumbnail");
	if (st.type == TOML_TABLE) {
		struct window_switcher_thumbnail_theme *t =
			&theme->osd_window_switcher_thumbnail;
		if (toml_int(st, "max-width", &i) && i > 0) {
			t->max_width = i;
			t->max_width_is_percent = false;
		}
		if (toml_int(st, "padding", &i) && i >= 0) {
			t->padding = i;
		}
		if (toml_int(st, "item-width", &i) && i > 0) {
			t->item_width = i;
		}
		if (toml_int(st, "item-height", &i) && i > 0) {
			t->item_height = i;
		}
		if (toml_int(st, "item-padding", &i) && i >= 0) {
			t->item_padding = i;
		}
		if (toml_int(st, "item-active-border-width", &i) && i >= 0) {
			t->item_active_border_width = i;
		}
		toml_color(st, "item-active-border",
			t->item_active_border_color);
		toml_color(st, "item-active-bg",
			t->item_active_bg_color);
		toml_color(st, "item-active-text",
			t->item_active_text_color);
		if (toml_int(st, "item-icon-size", &i) && i > 0) {
			t->item_icon_size = i;
		}
	}

	/* [theme.colors.osd.preview] */
	toml_datum_t pv = toml_get(sec, "preview");
	if (pv.type == TOML_TABLE) {
		if (toml_int(pv, "border-width", &i) && i >= 0) {
			theme->osd_window_switcher_preview_border_width = i;
		}
		toml_color_array(pv, "border-colors",
			theme->osd_window_switcher_preview_border_color);
	}

	/* [theme.colors.osd.workspace-switcher] */
	toml_datum_t ws = toml_get(sec, "workspace-switcher");
	if (ws.type == TOML_TABLE) {
		if (toml_int(ws, "boxes-width", &i) && i > 0) {
			theme->osd_workspace_switcher_boxes_width = i;
		}
		if (toml_int(ws, "boxes-height", &i) && i > 0) {
			theme->osd_workspace_switcher_boxes_height = i;
		}
		if (toml_int(ws, "boxes-border-width", &i) && i >= 0) {
			theme->osd_workspace_switcher_boxes_border_width = i;
		}
	}
}

/*
 * [theme.colors.snapping]
 */
static void
parse_snapping_colors(toml_datum_t root, struct theme *theme)
{
	toml_datum_t sec = toml_seek(root, "theme.colors.snapping");
	if (sec.type != TOML_TABLE) {
		return;
	}

	struct {
		const char *key;
		struct theme_snapping_overlay *overlay;
	} map[] = {
		{ "region", &theme->snapping_overlay_region },
		{ "edge",   &theme->snapping_overlay_edge },
	};

	for (size_t k = 0; k < sizeof(map) / sizeof(map[0]); k++) {
		toml_datum_t ov = toml_get(sec, map[k].key);
		if (ov.type != TOML_TABLE) {
			continue;
		}
		struct theme_snapping_overlay *o = map[k].overlay;
		int i;
		toml_bool(ov, "bg-enabled", &o->bg_enabled);
		toml_bool(ov, "border-enabled", &o->border_enabled);
		toml_color(ov, "bg", o->bg_color);
		if (toml_int(ov, "border-width", &i) && i >= 0) {
			o->border_width = i;
		}
		toml_color_array(ov, "border-colors", o->border_color);
	}
}

/*
 * [theme.colors.magnifier]
 */
static void
parse_magnifier_colors(toml_datum_t root, struct theme *theme)
{
	toml_datum_t sec = toml_seek(root, "theme.colors.magnifier");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;
	if (toml_int(sec, "border-width", &i) && i >= 0) {
		theme->mag_border_width = i;
	}
	toml_color(sec, "border", theme->mag_border_color);
}

/*
 * [theme.colors]  — top-level colors section (border-width etc.)
 */
static void
parse_colors(toml_datum_t root, struct theme *theme)
{
	toml_datum_t sec = toml_seek(root, "theme.colors");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;
	if (toml_int(sec, "border-width", &i) && i >= 0) {
		theme->border_width = i;
	}
}

static void
apply_toml(toml_datum_t root, struct theme *theme)
{
	parse_colors(root, theme);
	parse_window_colors(root, theme);
	parse_menu_colors(root, theme);
	parse_osd_colors(root, theme);
	parse_snapping_colors(root, theme);
	parse_magnifier_colors(root, theme);
}

/* ------------------------------------------------------------------ entry */

void
theme_toml_read(struct theme *theme)
{
	struct wl_list paths;
	paths_config_glob(&paths, "*.toml");

	struct path *p;
	wl_list_for_each(p, &paths, link) {
		toml_result_t result = toml_parse_file_ex(p->string);
		if (!result.ok) {
			wlr_log(WLR_ERROR, "toml parse error in %s: %s",
				p->string, result.errmsg);
			toml_free(result);
			continue;
		}

		/* only log if the file actually has a [theme.colors] section */
		toml_datum_t colors = toml_seek(result.toptab, "theme.colors");
		if (colors.type == TOML_TABLE) {
			wlr_log(WLR_INFO, "read theme colors from %s", p->string);
			apply_toml(result.toptab, theme);
		}

		toml_free(result);
	}

	paths_destroy(&paths);
}
