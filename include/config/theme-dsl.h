/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_THEME_DSL_H
#define LABWC_THEME_DSL_H

#include <stdbool.h>
#include <wayland-util.h>
#include "theme.h"

/*
 * theme-dsl: declarative component-tree theme format
 *
 * A .labwc file looks like:
 *
 *   $bg    = #181818
 *   $border = #2b2b2b
 *
 *   Window {
 *       border: 1 $border
 *       corner-radius: 6
 *
 *       Titlebar {
 *           padding: 6 5
 *           background: $bg
 *           background-inactive: #1f1f1f
 *
 *           Left {
 *               Button app-menu {
 *                   AppIcon { size: 16 }
 *               }
 *           }
 *           Title {
 *               color: #cccccc
 *               color-inactive: #6b6b6b
 *               justify: left
 *           }
 *           Right {
 *               spacing: 4
 *               Button iconify {
 *                   padding: 3
 *                   corner-radius: 4
 *                   hover-bg: #80808020
 *                   Icon { name: window-minimize; size: 16 }
 *               }
 *               Button maximize {
 *                   padding: 3
 *                   corner-radius: 4
 *                   hover-bg: #80808020
 *                   Icon { name: window-maximize; size: 16 }
 *               }
 *               Button close {
 *                   padding: 3
 *                   corner-radius: 4
 *                   hover-bg: #e8112d40
 *                   Icon { name: window-close; size: 16 }
 *               }
 *           }
 *       }
 *   }
 */

/* ------------------------------------------------------------------ box model */

/*
 * Edges: top / right / bottom / left (CSS order).
 * DSL shorthand rules (same as CSS):
 *   padding: 6          → all four = 6
 *   padding: 6 5        → top/bottom = 6, left/right = 5
 *   padding: 6 5 4      → top = 6, left/right = 5, bottom = 4
 *   padding: 6 5 4 3    → top right bottom left
 *
 * -1 means "not set / inherit".
 */
struct dsl_box {
	int top, right, bottom, left;
};

#define DSL_BOX_UNSET { -1, -1, -1, -1 }

/* ------------------------------------------------------------------ node types */

enum dsl_node_type {
	DSL_NODE_WINDOW,
	DSL_NODE_TITLEBAR,
	DSL_NODE_LEFT,       /* left button slot    */
	DSL_NODE_RIGHT,      /* right button slot   */
	DSL_NODE_TITLE,      /* title text element  */
	DSL_NODE_BUTTON,     /* generic button      */
	DSL_NODE_ICON,       /* svg/png icon        */
	DSL_NODE_APP_ICON,   /* per-window app icon */
};

/* ------------------------------------------------------------------ node */

/*
 * A single parsed node in the component tree.
 *
 * All dimension/color fields use sentinel "not set" values:
 *   int:   -1  = not set
 *   float: rgba[3] < 0.0  = not set (we test rgba[3] == -1.0f)
 *
 * The renderer walks this tree to compute final geometry and calls
 * into the wlr_scene API; it ignores fields that are -1/unset.
 */
struct dsl_node {
	enum dsl_node_type type;

	/* optional name token after type keyword: e.g. Button close → name="close" */
	char *name;

	/* --- box model --- */
	struct dsl_box padding;
	struct dsl_box margin;

	/* explicit size; -1 = auto (derived from children + padding) */
	int width;
	int height;

	/* gap between children in a slot (like CSS gap / flexbox gap) */
	int spacing;

	int corner_radius;

	/* border: width color */
	int border_width;
	float border_color[4];       /* rgba[3] == -1 → unset */
	float border_color_inactive[4];

	/* background */
	float bg[4];                 /* rgba[3] == -1 → unset */
	float bg_inactive[4];

	/* hover background (buttons) */
	float hover_bg[4];           /* rgba[3] == -1 → unset */
	int hover_corner_radius;

	/* text / icon color */
	float color[4];              /* rgba[3] == -1 → unset */
	float color_inactive[4];

	/* shadow (Window node only) */
	int shadow_size;
	float shadow_color[4];

	/* Icon/AppIcon: icon name and pixel size */
	char *icon_name;
	int icon_size;

	/* justify for Title node */
	int justify;   /* -1 unset, 0 left, 1 center, 2 right */

	/* tree linkage */
	struct wl_list children; /* dsl_node.sibling */
	struct wl_list sibling;
};

/* ------------------------------------------------------------------ vars */

/*
 * Variable table for the DSL — parallel to toml-vars but
 * loaded from $name = #value lines at the top of a .labwc file.
 */
void dsl_vars_clear(void);
void dsl_var_set(const char *name, const char *value);
const char *dsl_var_resolve(const char *s); /* returns s unchanged if not $-prefixed */

/* ------------------------------------------------------------------ API */

/*
 * Parse a .labwc theme file into a tree of dsl_nodes.
 * Returns the root node (always DSL_NODE_WINDOW) or NULL on error.
 * Caller must free with dsl_node_free().
 */
struct dsl_node *dsl_parse_file(const char *path);

/*
 * Free a node and all its children recursively.
 */
void dsl_node_free(struct dsl_node *node);

/*
 * Walk the parsed DSL tree and apply values to struct theme.
 * This is used instead of (or after) theme_toml_read() when a
 * theme.labwc file is present.
 */
void dsl_apply_to_theme(struct dsl_node *root, struct theme *theme);

/*
 * Convenience: find and parse any theme.labwc in the config directory,
 * apply to theme. Returns true if a file was found and parsed.
 */
bool theme_dsl_read(struct theme *theme);

#endif /* LABWC_THEME_DSL_H */
