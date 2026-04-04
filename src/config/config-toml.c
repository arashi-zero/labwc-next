// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/config-toml.h"
#include <pango/pango-font.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/log.h>
#include "tomlc17.h"
#include "common/dir.h"
#include "common/font.h"
#include "common/list.h"
#include "common/mem.h"
#include "config/keybind.h"
#include "config/mousebind.h"
#include "config/rcxml.h"
#include "config/toml-vars.h"
#include "action.h"
#include "view.h"
#include "workspaces.h"

/* Extern global config struct defined in rcxml.c */
extern struct rcxml rc;

/* ------------------------------------------------------------------ helpers */

static bool
toml_str(toml_datum_t tab, const char *key, const char **out)
{
	toml_datum_t d = toml_seek(tab, key);
	if (d.type != TOML_STRING) {
		return false;
	}
	*out = resolve_var(d.u.s);
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

static void
fill_font(toml_datum_t tab, struct font *font)
{
	const char *s;
	int i;

	if (toml_str(tab, "name", &s)) {
		xstrdup_replace(font->name, s);
	}
	if (toml_int(tab, "size", &i)) {
		font->size = i;
	}
	if (toml_str(tab, "slant", &s)) {
		if (!strcasecmp(s, "italic")) {
			font->slant = PANGO_STYLE_ITALIC;
		} else if (!strcasecmp(s, "oblique")) {
			font->slant = PANGO_STYLE_OBLIQUE;
		} else {
			font->slant = PANGO_STYLE_NORMAL;
		}
	}
	if (toml_str(tab, "weight", &s)) {
		if (!strcasecmp(s, "thin")) {
			font->weight = PANGO_WEIGHT_THIN;
		} else if (!strcasecmp(s, "ultralight")) {
			font->weight = PANGO_WEIGHT_ULTRALIGHT;
		} else if (!strcasecmp(s, "light")) {
			font->weight = PANGO_WEIGHT_LIGHT;
		} else if (!strcasecmp(s, "semilight")) {
			font->weight = PANGO_WEIGHT_SEMILIGHT;
		} else if (!strcasecmp(s, "book")) {
			font->weight = PANGO_WEIGHT_BOOK;
		} else if (!strcasecmp(s, "medium")) {
			font->weight = PANGO_WEIGHT_MEDIUM;
		} else if (!strcasecmp(s, "semibold")) {
			font->weight = PANGO_WEIGHT_SEMIBOLD;
		} else if (!strcasecmp(s, "bold")) {
			font->weight = PANGO_WEIGHT_BOLD;
		} else if (!strcasecmp(s, "ultrabold")) {
			font->weight = PANGO_WEIGHT_ULTRABOLD;
		} else if (!strcasecmp(s, "heavy")) {
			font->weight = PANGO_WEIGHT_HEAVY;
		} else if (!strcasecmp(s, "ultraheavy")) {
			font->weight = PANGO_WEIGHT_ULTRAHEAVY;
		} else {
			font->weight = PANGO_WEIGHT_NORMAL;
		}
	}
}

/* ------------------------------------------------------------------ sections */

static void
parse_core(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "core");
	if (sec.type != TOML_TABLE) {
		return;
	}

	const char *s;
	bool b;
	int i;

	if (toml_str(sec, "decoration", &s)) {
		rc.xdg_shell_server_side_deco = strcasecmp(s, "client") != 0;
	}
	if (toml_int(sec, "gap", &i)) {
		rc.gap = i;
	}
	if (toml_str(sec, "adaptive-sync", &s)) {
		if (!strcasecmp(s, "enabled")) {
			rc.adaptive_sync = LAB_ADAPTIVE_SYNC_ENABLED;
		} else if (!strcasecmp(s, "fullscreen")) {
			rc.adaptive_sync = LAB_ADAPTIVE_SYNC_FULLSCREEN;
		} else {
			rc.adaptive_sync = LAB_ADAPTIVE_SYNC_DISABLED;
		}
	}
	if (toml_str(sec, "allow-tearing", &s)) {
		if (!strcasecmp(s, "enabled")) {
			rc.allow_tearing = LAB_TEARING_ENABLED;
		} else if (!strcasecmp(s, "fullscreen")) {
			rc.allow_tearing = LAB_TEARING_FULLSCREEN;
		} else if (!strcasecmp(s, "fullscreen-forced")) {
			rc.allow_tearing = LAB_TEARING_FULLSCREEN_FORCED;
		} else {
			rc.allow_tearing = LAB_TEARING_DISABLED;
		}
	}
	if (toml_bool(sec, "auto-enable-outputs", &b)) {
		rc.auto_enable_outputs = b;
	}
	if (toml_bool(sec, "reuse-output-mode", &b)) {
		rc.reuse_output_mode = b;
	}
	if (toml_bool(sec, "xwayland-persistence", &b)) {
		rc.xwayland_persistence = b;
	}
	if (toml_bool(sec, "primary-selection", &b)) {
		rc.primary_selection = b;
	}
	if (toml_str(sec, "prompt-command", &s)) {
		xstrdup_replace(rc.prompt_command, s);
	}
	if (toml_str(sec, "bell-command", &s)) {
		xstrdup_replace(rc.bell_command, s);
	}
}

static void
parse_placement(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "placement");
	if (sec.type != TOML_TABLE) {
		return;
	}

	const char *s;
	int i;

	if (toml_str(sec, "policy", &s)) {
		enum lab_placement_policy policy = view_placement_parse(s);
		if (policy != LAB_PLACE_INVALID) {
			rc.placement_policy = policy;
		}
	}
	if (toml_int(sec, "cascade-offset-x", &i)) {
		rc.placement_cascade_offset_x = i;
	}
	if (toml_int(sec, "cascade-offset-y", &i)) {
		rc.placement_cascade_offset_y = i;
	}
}

static void
parse_focus(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "focus");
	if (sec.type != TOML_TABLE) {
		return;
	}

	bool b;

	if (toml_bool(sec, "follow-mouse", &b)) {
		rc.focus_follow_mouse = b;
	}
	if (toml_bool(sec, "follow-mouse-requires-movement", &b)) {
		rc.focus_follow_mouse_requires_movement = b;
	}
	if (toml_bool(sec, "raise-on-focus", &b)) {
		rc.raise_on_focus = b;
	}
}

static void
parse_theme(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "theme");
	if (sec.type != TOML_TABLE) {
		return;
	}

	const char *s;
	bool b;
	int i;

	if (toml_str(sec, "name", &s)) {
		xstrdup_replace(rc.theme_name, s);
	}
	if (toml_str(sec, "icon-theme", &s)) {
		xstrdup_replace(rc.icon_theme_name, s);
	}
	if (toml_str(sec, "fallback-app-icon", &s)) {
		xstrdup_replace(rc.fallback_app_icon_name, s);
	}
	if (toml_int(sec, "corner-radius", &i)) {
		rc.corner_radius = i;
	}
	if (toml_bool(sec, "drop-shadows", &b)) {
		rc.shadows_enabled = b;
	}
	if (toml_bool(sec, "drop-shadows-on-tiled", &b)) {
		rc.shadows_on_tiled = b;
	}
	if (toml_bool(sec, "keep-border", &b)) {
		rc.ssd_keep_border = b;
	}
	if (toml_str(sec, "maximized-decoration", &s)) {
		if (!strcasecmp(s, "none")) {
			rc.hide_maximized_window_titlebar = true;
		} else {
			rc.hide_maximized_window_titlebar = false;
		}
	}

	/* [theme.titlebar] */
	toml_datum_t tb = toml_get(sec, "titlebar");
	if (tb.type == TOML_TABLE) {
		if (toml_str(tb, "layout", &s)) {
			rcxml_fill_title_layout(s);
		}
		if (toml_bool(tb, "show-title", &b)) {
			rc.show_title = b;
		}
	}

	/* [theme.font] - set all font places at once when no sub-key */
	toml_datum_t font_all = toml_get(sec, "font");
	if (font_all.type == TOML_TABLE) {
		fill_font(font_all, &rc.font_activewindow);
		fill_font(font_all, &rc.font_inactivewindow);
		fill_font(font_all, &rc.font_menuheader);
		fill_font(font_all, &rc.font_menuitem);
		fill_font(font_all, &rc.font_osd);
	}

	/* Per-place font overrides */
	struct {
		const char *key;
		struct font *dst;
	} font_places[] = {
		{ "font.active",      &rc.font_activewindow },
		{ "font.inactive",    &rc.font_inactivewindow },
		{ "font.menu-header", &rc.font_menuheader },
		{ "font.menu-item",   &rc.font_menuitem },
		{ "font.osd",         &rc.font_osd },
	};
	for (size_t k = 0; k < sizeof(font_places) / sizeof(font_places[0]); k++) {
		toml_datum_t fd = toml_seek(sec, font_places[k].key);
		if (fd.type == TOML_TABLE) {
			fill_font(fd, font_places[k].dst);
		}
	}
}

static void
parse_keyboard(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "keyboard");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;
	bool b;
	const char *s;

	if (toml_int(sec, "repeat-rate", &i)) {
		rc.repeat_rate = i;
	}
	if (toml_int(sec, "repeat-delay", &i)) {
		rc.repeat_delay = i;
	}
	if (toml_bool(sec, "numlock", &b)) {
		rc.kb_numlock_enable = b ? LAB_STATE_ENABLED : LAB_STATE_DISABLED;
	}
	if (toml_str(sec, "layout-scope", &s)) {
		rc.kb_layout_per_window = !strcasecmp(s, "window");
	}

	/* [[keyboard.keybind]] - array of tables */
	toml_datum_t arr = toml_get(sec, "keybind");
	if (arr.type == TOML_ARRAY) {
		for (int k = 0; k < arr.u.arr.size; k++) {
			toml_datum_t entry = arr.u.arr.elem[k];
			if (entry.type != TOML_TABLE) {
				continue;
			}
			const char *key = NULL;
			if (!toml_str(entry, "key", &key) || !key) {
				wlr_log(WLR_ERROR, "config.toml: keybind missing 'key'");
				continue;
			}
			struct keybind *kb = keybind_create(key);
			if (!kb) {
				wlr_log(WLR_ERROR, "config.toml: invalid keybind key '%s'", key);
				continue;
			}

			bool on_release = false;
			bool layout_dep = false;
			bool allow_locked = false;
			toml_bool(entry, "on-release", &on_release);
			toml_bool(entry, "layout-dependent", &layout_dep);
			toml_bool(entry, "allow-when-locked", &allow_locked);
			kb->on_release = on_release;
			kb->use_syms_only = layout_dep;
			kb->allow_when_locked = allow_locked;

			toml_datum_t actions = toml_get(entry, "actions");
			if (actions.type == TOML_ARRAY) {
				for (int ai = 0; ai < actions.u.arr.size; ai++) {
					toml_datum_t act = actions.u.arr.elem[ai];
					if (act.type != TOML_TABLE) {
						continue;
					}
					const char *name = NULL;
					if (!toml_str(act, "name", &name) || !name) {
						continue;
					}
					struct action *action = action_create(name);
					if (!action) {
						continue;
					}
					/* Pass remaining keys as action args */
					for (int ki = 0; ki < act.u.tab.size; ki++) {
						const char *akey = act.u.tab.key[ki];
						if (!strcasecmp(akey, "name")) {
							continue;
						}
						toml_datum_t val = act.u.tab.value[ki];
						if (val.type == TOML_STRING) {
							action_arg_from_xml_node(action, akey, val.u.s);
						}
					}
					wl_list_append(&kb->actions, &action->link);
				}
			}
		}
	}
}

static void
parse_mouse(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "mouse");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;

	if (toml_int(sec, "double-click-time", &i) && i > 0) {
		rc.doubleclick_time = i;
	}

	/* [[mouse.mousebind]] - array of tables */
	toml_datum_t arr = toml_get(sec, "mousebind");
	if (arr.type == TOML_ARRAY) {
		for (int k = 0; k < arr.u.arr.size; k++) {
			toml_datum_t entry = arr.u.arr.elem[k];
			if (entry.type != TOML_TABLE) {
				continue;
			}
			const char *context = NULL;
			const char *button = NULL;
			const char *event = NULL;
			if (!toml_str(entry, "context", &context) || !context) {
				wlr_log(WLR_ERROR, "config.toml: mousebind missing 'context'");
				continue;
			}
			struct mousebind *mb = mousebind_create(context);

			if (toml_str(entry, "button", &button) && button) {
				mb->button = mousebind_button_from_str(button, &mb->modifiers);
			}
			if (toml_str(entry, "direction", &button) && button) {
				mb->direction = mousebind_direction_from_str(button, &mb->modifiers);
			}
			if (toml_str(entry, "event", &event) && event) {
				mb->mouse_event = mousebind_event_from_str(event);
			}

			toml_datum_t actions = toml_get(entry, "actions");
			if (actions.type == TOML_ARRAY) {
				for (int ai = 0; ai < actions.u.arr.size; ai++) {
					toml_datum_t act = actions.u.arr.elem[ai];
					if (act.type != TOML_TABLE) {
						continue;
					}
					const char *name = NULL;
					if (!toml_str(act, "name", &name) || !name) {
						continue;
					}
					struct action *action = action_create(name);
					if (!action) {
						continue;
					}
					for (int ki = 0; ki < act.u.tab.size; ki++) {
						const char *akey = act.u.tab.key[ki];
						if (!strcasecmp(akey, "name")) {
							continue;
						}
						toml_datum_t val = act.u.tab.value[ki];
						if (val.type == TOML_STRING) {
							action_arg_from_xml_node(action, akey, val.u.s);
						}
					}
					wl_list_append(&mb->actions, &action->link);
				}
			}
		}
	}
}

static void
parse_workspace(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "workspace");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;
	const char *s;

	if (toml_int(sec, "popup-time", &i)) {
		rc.workspace_config.popuptime = i;
	}
	if (toml_int(sec, "min-count", &i) && i >= 1) {
		rc.workspace_config.min_nr_workspaces = i;
	}
	if (toml_str(sec, "prefix", &s)) {
		xstrdup_replace(rc.workspace_config.prefix, s);
	}
	if (toml_str(sec, "initial", &s)) {
		xstrdup_replace(rc.workspace_config.initial_workspace_name, s);
	}

	toml_datum_t names = toml_get(sec, "names");
	if (names.type == TOML_ARRAY) {
		for (int k = 0; k < names.u.arr.size; k++) {
			toml_datum_t nm = names.u.arr.elem[k];
			if (nm.type == TOML_STRING) {
				struct workspace_config *conf = znew(*conf);
				conf->name = xstrdup(nm.u.s);
				wl_list_append(&rc.workspace_config.workspaces, &conf->link);
			}
		}
	}
}

static void
parse_resistance(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "resistance");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;

	if (toml_int(sec, "screen-edge-strength", &i)) {
		rc.screen_edge_strength = i;
	}
	if (toml_int(sec, "window-edge-strength", &i)) {
		rc.window_edge_strength = i;
	}
	if (toml_int(sec, "unsnap-threshold", &i)) {
		rc.unsnap_threshold = i;
	}
	if (toml_int(sec, "unmaximize-threshold", &i)) {
		rc.unmaximize_threshold = i;
	}
}

static void
parse_snapping(toml_datum_t root)
{
	toml_datum_t sec = toml_get(root, "snapping");
	if (sec.type != TOML_TABLE) {
		return;
	}

	int i;
	bool b;
	const char *s;

	if (toml_int(sec, "range-inner", &i)) {
		rc.snap_edge_range_inner = i;
	}
	if (toml_int(sec, "range-outer", &i)) {
		rc.snap_edge_range_outer = i;
	}
	if (toml_int(sec, "corner-range", &i)) {
		rc.snap_edge_corner_range = i;
	}
	if (toml_bool(sec, "top-maximize", &b)) {
		rc.snap_top_maximize = b;
	}
	if (toml_str(sec, "notify-client", &s)) {
		if (!strcasecmp(s, "always")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_ALWAYS;
		} else if (!strcasecmp(s, "region")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_REGION;
		} else if (!strcasecmp(s, "edge")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_EDGE;
		} else if (!strcasecmp(s, "never")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_NEVER;
		}
	}

	toml_datum_t ov = toml_get(sec, "overlay");
	if (ov.type == TOML_TABLE) {
		if (toml_bool(ov, "enabled", &b)) {
			rc.snap_overlay_enabled = b;
		}
		if (toml_int(ov, "delay-inner", &i)) {
			rc.snap_overlay_delay_inner = i;
		}
		if (toml_int(ov, "delay-outer", &i)) {
			rc.snap_overlay_delay_outer = i;
		}
	}
}

/* ------------------------------------------------------------------ entry */

bool
config_toml_read(void)
{
	struct wl_list paths;
	paths_config_glob(&paths, "*.toml");

	if (wl_list_empty(&paths)) {
		paths_destroy(&paths);
		return false;
	}

	bool found = false;
	/* vars accumulate across files in glob order — name your palette file
	 * so it sorts first (e.g. 00-vars.toml) to act as a global palette. */
	vars_clear();
	struct path *p;
	wl_list_for_each(p, &paths, link) {
		toml_result_t result = toml_parse_file_ex(p->string);
		if (!result.ok) {
			wlr_log(WLR_ERROR, "toml parse error in %s: %s",
				p->string, result.errmsg);
			toml_free(result);
			continue;
		}

		wlr_log(WLR_INFO, "read config file %s", p->string);

		toml_datum_t root = result.toptab;
		vars_load(root);
		parse_core(root);
		parse_placement(root);
		parse_focus(root);
		parse_theme(root);
		parse_keyboard(root);
		parse_mouse(root);
		parse_workspace(root);
		parse_resistance(root);
		parse_snapping(root);

		toml_free(result);
		found = true;
	}

	vars_clear(); /* final cleanup */
	paths_destroy(&paths);
	return found;
}
