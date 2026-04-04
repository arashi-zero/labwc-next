// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/rcxml.h"
#include <assert.h>
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "action.h"
#include "common/buf.h"
#include "common/list.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/parse-bool.h"
#include "common/parse-double.h"
#include "common/string-helpers.h"
#include "config/config-toml.h"
#include "config/default-bindings.h"
#include "config/keybind.h"
#include "config/libinput.h"
#include "config/mousebind.h"
#include "config/tablet.h"
#include "config/tablet-tool.h"
#include "config/touch.h"
#include "cycle.h"
#include "labwc.h"
#include "regions.h"
#include "ssd.h"
#include "translate.h"
#include "view.h"
#include "window-rules.h"
#include "workspaces.h"

static void load_default_key_bindings(void);
static void load_default_mouse_bindings(void);

static void
fill_section(const char *content, enum lab_node_type *buttons, int *count,
		uint32_t *found_buttons /* bitmask */)
{
	gchar **identifiers = g_strsplit(content, ",", -1);
	for (size_t i = 0; identifiers[i]; ++i) {
		char *identifier = identifiers[i];
		if (string_null_or_empty(identifier)) {
			continue;
		}
		enum lab_node_type type = LAB_NODE_NONE;
		if (!strcmp(identifier, "icon")) {
#if HAVE_LIBSFDO
			type = LAB_NODE_BUTTON_WINDOW_ICON;
#else
			wlr_log(WLR_ERROR, "libsfdo is not linked. "
				"Replacing 'icon' in titlebar layout with 'menu'.");
			type = LAB_NODE_BUTTON_WINDOW_MENU;
#endif
		} else if (!strcmp(identifier, "menu")) {
			type = LAB_NODE_BUTTON_WINDOW_MENU;
		} else if (!strcmp(identifier, "iconify")) {
			type = LAB_NODE_BUTTON_ICONIFY;
		} else if (!strcmp(identifier, "max")) {
			type = LAB_NODE_BUTTON_MAXIMIZE;
		} else if (!strcmp(identifier, "close")) {
			type = LAB_NODE_BUTTON_CLOSE;
		} else if (!strcmp(identifier, "shade")) {
			type = LAB_NODE_BUTTON_SHADE;
		} else if (!strcmp(identifier, "desk")) {
			type = LAB_NODE_BUTTON_OMNIPRESENT;
		} else {
			wlr_log(WLR_ERROR, "invalid titleLayout identifier '%s'",
				identifier);
			continue;
		}

		assert(type != LAB_NODE_NONE);

		if (*found_buttons & (1 << type)) {
			wlr_log(WLR_ERROR, "ignoring duplicated button type '%s'",
				identifier);
			continue;
		}

		*found_buttons |= (1 << type);

		assert(*count < TITLE_BUTTONS_MAX);
		buttons[(*count)++] = type;
	}
	g_strfreev(identifiers);
}

static void
clear_title_layout(void)
{
	rc.nr_title_buttons_left = 0;
	rc.nr_title_buttons_right = 0;
	rc.title_layout_loaded = false;
}

void
rcxml_fill_title_layout(const char *content)
{
	clear_title_layout();

	gchar **parts = g_strsplit(content, ":", -1);

	if (g_strv_length(parts) != 2) {
		wlr_log(WLR_ERROR, "<titlebar><layout> must contain one colon");
		goto err;
	}

	uint32_t found_buttons = 0;
	fill_section(parts[0], rc.title_buttons_left,
		&rc.nr_title_buttons_left, &found_buttons);
	fill_section(parts[1], rc.title_buttons_right,
		&rc.nr_title_buttons_right, &found_buttons);

	rc.title_layout_loaded = true;
err:
	g_strfreev(parts);
}

static void
clear_window_switcher_fields(void)
{
	struct cycle_osd_field *field, *field_tmp;
	wl_list_for_each_safe(field, field_tmp,
			&rc.window_switcher.osd.fields, link) {
		wl_list_remove(&field->link);
		cycle_osd_field_free(field);
	}
}

static void
init_font_defaults(struct font *font)
{
	font->size = 10;
	font->slant = PANGO_STYLE_NORMAL;
	font->weight = PANGO_WEIGHT_NORMAL;
}

static void
rcxml_init(void)
{
	static bool has_run;

	if (!has_run) {
		wl_list_init(&rc.usable_area_overrides);
		wl_list_init(&rc.keybinds);
		wl_list_init(&rc.mousebinds);
		wl_list_init(&rc.libinput_categories);
		wl_list_init(&rc.workspace_config.workspaces);
		wl_list_init(&rc.regions);
		wl_list_init(&rc.window_switcher.osd.fields);
		wl_list_init(&rc.window_rules);
		wl_list_init(&rc.touch_configs);
	}
	has_run = true;

	rc.placement_policy = LAB_PLACE_CASCADE;
	rc.placement_cascade_offset_x = 0;
	rc.placement_cascade_offset_y = 0;

	rc.xdg_shell_server_side_deco = true;
	rc.hide_maximized_window_titlebar = false;
	rc.show_title = true;
	rc.title_layout_loaded = false;
	rc.ssd_keep_border = true;
	rc.corner_radius = 8;
	rc.shadows_enabled = false;
	rc.shadows_on_tiled = false;

	rc.gap = 0;
	rc.adaptive_sync = LAB_ADAPTIVE_SYNC_DISABLED;
	rc.allow_tearing = LAB_TEARING_DISABLED;
	rc.auto_enable_outputs = true;
	rc.reuse_output_mode = false;
	rc.xwayland_persistence = false;
	rc.primary_selection = true;

	init_font_defaults(&rc.font_activewindow);
	init_font_defaults(&rc.font_inactivewindow);
	init_font_defaults(&rc.font_menuheader);
	init_font_defaults(&rc.font_menuitem);
	init_font_defaults(&rc.font_osd);

	rc.focus_follow_mouse = false;
	rc.focus_follow_mouse_requires_movement = true;
	rc.raise_on_focus = false;

	rc.doubleclick_time = 500;

	rc.tablet.force_mouse_emulation = false;
	rc.tablet.output_name = NULL;
	rc.tablet.rotation = LAB_ROTATE_NONE;
	rc.tablet.box = (struct wlr_fbox){0};
	tablet_load_default_button_mappings();
	rc.tablet_tool.motion = LAB_MOTION_ABSOLUTE;
	rc.tablet_tool.relative_motion_sensitivity = 1.0;
	rc.tablet_tool.min_pressure = 0.0;
	rc.tablet_tool.max_pressure = 1.0;

	rc.repeat_rate = 25;
	rc.repeat_delay = 600;
	rc.kb_numlock_enable = LAB_STATE_UNSPECIFIED;
	rc.kb_layout_per_window = false;
	rc.screen_edge_strength = 20;
	rc.window_edge_strength = 20;
	rc.unsnap_threshold = 20;
	rc.unmaximize_threshold = 150;

	rc.snap_edge_range_inner = 10;
	rc.snap_edge_range_outer = 10;
	rc.snap_edge_corner_range = 50;
	rc.snap_overlay_enabled = true;
	rc.snap_overlay_delay_inner = 500;
	rc.snap_overlay_delay_outer = 500;
	rc.snap_top_maximize = true;
	rc.snap_tiling_events_mode = LAB_TILING_EVENTS_ALWAYS;

	rc.window_switcher.osd.show = true;
	rc.window_switcher.osd.style = CYCLE_OSD_STYLE_CLASSIC;
	rc.window_switcher.osd.output_filter = CYCLE_OUTPUT_ALL;
	rc.window_switcher.osd.thumbnail_label_format = xstrdup("%T");
	rc.window_switcher.preview = true;
	rc.window_switcher.outlines = true;
	rc.window_switcher.unshade = true;
	rc.window_switcher.workspace_filter = CYCLE_WORKSPACE_CURRENT;
	rc.window_switcher.order = WINDOW_SWITCHER_ORDER_FOCUS;

	rc.resize_indicator = LAB_RESIZE_INDICATOR_NEVER;
	rc.resize_draw_contents = true;
	rc.resize_corner_range = -1;
	rc.resize_minimum_area = 8;

	rc.workspace_config.popuptime = INT_MIN;
	rc.workspace_config.min_nr_workspaces = 1;

	rc.menu_ignore_button_release_period = 250;
	rc.menu_show_icons = true;

	rc.mag_width = 400;
	rc.mag_height = 400;
	rc.mag_scale = 2.0;
	rc.mag_increment = 0.2;
	rc.mag_filter = true;
}

static void
load_default_key_bindings(void)
{
	struct keybind *k;
	struct action *action;
	for (int i = 0; key_combos[i].binding; i++) {
		struct key_combos *current = &key_combos[i];
		k = keybind_create(current->binding);
		if (!k) {
			continue;
		}

		action = action_create(current->action);
		wl_list_append(&k->actions, &action->link);

		for (size_t j = 0; j < ARRAY_SIZE(current->attributes); j++) {
			if (!current->attributes[j].name
					|| !current->attributes[j].value) {
				break;
			}
			action_arg_from_xml_node(action,
				current->attributes[j].name,
				current->attributes[j].value);
		}
	}
}

static void
load_default_mouse_bindings(void)
{
	uint32_t count = 0;
	struct mousebind *m;
	struct action *action;
	for (int i = 0; mouse_combos[i].context; i++) {
		struct mouse_combos *current = &mouse_combos[i];
		if (i == 0
				|| strcmp(current->context, mouse_combos[i - 1].context)
				|| strcmp(current->button, mouse_combos[i - 1].button)
				|| strcmp(current->event, mouse_combos[i - 1].event)) {
			/* Create new mousebind */
			m = mousebind_create(current->context);
			m->mouse_event = mousebind_event_from_str(current->event);
			if (m->mouse_event == MOUSE_ACTION_SCROLL) {
				m->direction = mousebind_direction_from_str(current->button,
					&m->modifiers);
			} else {
				m->button = mousebind_button_from_str(current->button,
					&m->modifiers);
			}
			count++;
		}

		action = action_create(current->action);
		wl_list_append(&m->actions, &action->link);

		for (size_t j = 0; j < ARRAY_SIZE(current->attributes); j++) {
			if (!current->attributes[j].name
					|| !current->attributes[j].value) {
				break;
			}
			action_arg_from_xml_node(action,
				current->attributes[j].name,
				current->attributes[j].value);
		}
	}
	wlr_log(WLR_DEBUG, "Loaded %u merged mousebinds", count);
}

static void
deduplicate_mouse_bindings(void)
{
	uint32_t replaced = 0;
	uint32_t cleared = 0;
	struct mousebind *current, *tmp, *existing;
	wl_list_for_each_safe(existing, tmp, &rc.mousebinds, link) {
		wl_list_for_each_reverse(current, &rc.mousebinds, link) {
			if (existing == current) {
				break;
			}
			if (mousebind_the_same(existing, current)) {
				wl_list_remove(&existing->link);
				action_list_free(&existing->actions);
				free(existing);
				replaced++;
				break;
			}
		}
	}
	wl_list_for_each_safe(current, tmp, &rc.mousebinds, link) {
		if (wl_list_empty(&current->actions)) {
			wl_list_remove(&current->link);
			free(current);
			cleared++;
		}
	}
	if (replaced) {
		wlr_log(WLR_DEBUG, "Replaced %u mousebinds", replaced);
	}
	if (cleared) {
		wlr_log(WLR_DEBUG, "Cleared %u mousebinds", cleared);
	}
}

static void
deduplicate_key_bindings(void)
{
	uint32_t replaced = 0;
	uint32_t cleared = 0;
	struct keybind *current, *tmp, *existing;
	wl_list_for_each_safe(existing, tmp, &rc.keybinds, link) {
		wl_list_for_each_reverse(current, &rc.keybinds, link) {
			if (existing == current) {
				break;
			}
			if (keybind_the_same(existing, current)) {
				wl_list_remove(&existing->link);
				action_list_free(&existing->actions);
				keybind_destroy(existing);
				replaced++;
				break;
			}
		}
	}
	wl_list_for_each_safe(current, tmp, &rc.keybinds, link) {
		if (wl_list_empty(&current->actions)) {
			wl_list_remove(&current->link);
			keybind_destroy(current);
			cleared++;
		}
	}
	if (replaced) {
		wlr_log(WLR_DEBUG, "Replaced %u keybinds", replaced);
	}
	if (cleared) {
		wlr_log(WLR_DEBUG, "Cleared %u keybinds", cleared);
	}
}

static void
load_default_window_switcher_fields(void)
{
	static const struct {
		enum cycle_osd_field_content content;
		int width;
	} fields[] = {
#if HAVE_LIBSFDO
		{ LAB_FIELD_ICON, 5 },
		{ LAB_FIELD_DESKTOP_ENTRY_NAME, 30 },
		{ LAB_FIELD_TITLE, 65 },
#else
		{ LAB_FIELD_DESKTOP_ENTRY_NAME, 30 },
		{ LAB_FIELD_TITLE, 70 },
#endif
	};

	for (size_t i = 0; i < ARRAY_SIZE(fields); i++) {
		struct cycle_osd_field *field = znew(*field);
		field->content = fields[i].content;
		field->width = fields[i].width;
		wl_list_append(&rc.window_switcher.osd.fields, &field->link);
	}
}

static void
post_processing(void)
{
	if (!wl_list_length(&rc.keybinds)) {
		wlr_log(WLR_INFO, "load default key bindings");
		load_default_key_bindings();
	}

	if (!wl_list_length(&rc.mousebinds)) {
		wlr_log(WLR_INFO, "load default mouse bindings");
		load_default_mouse_bindings();
	}

	if (!rc.prompt_command) {
		rc.prompt_command =
			xstrdup("labnag "
				"--message '%m' "
				"--button-dismiss '%n' "
				"--button-dismiss '%y' "
				"--background-color '%b' "
				"--text-color '%t' "
				"--button-border-color '%t' "
				"--border-bottom-color '%t' "
				"--button-background-color '%b' "
				"--button-text-color '%t' "
				"--border-bottom-size 1 "
				"--button-border-size 3 "
				"--keyboard-focus on-demand "
				"--layer overlay "
				"--timeout 0");
	}
	if (!rc.fallback_app_icon_name) {
		rc.fallback_app_icon_name = xstrdup("labwc-next");
	}

	if (!rc.icon_theme_name && rc.theme_name) {
		rc.icon_theme_name = xstrdup(rc.theme_name);
	}

	if (!rc.title_layout_loaded) {
#if HAVE_LIBSFDO
		rcxml_fill_title_layout("icon:iconify,max,close");
#else
		/*
		 * 'icon' is replaced with 'menu' in fill_title_layout() when
		 * libsfdo is not linked, but we also replace it here not to
		 * show error message with default settings.
		 */
		rcxml_fill_title_layout("menu:iconify,max,close");
#endif
	}

	/*
	 * Replace all earlier bindings by later ones
	 * and clear the ones with an empty action list.
	 *
	 * This is required so users are able to remove
	 * a default binding by using the "None" action.
	 */
	deduplicate_key_bindings();
	deduplicate_mouse_bindings();

	if (!rc.font_activewindow.name) {
		rc.font_activewindow.name = xstrdup("sans");
	}
	if (!rc.font_inactivewindow.name) {
		rc.font_inactivewindow.name = xstrdup("sans");
	}
	if (!rc.font_menuheader.name) {
		rc.font_menuheader.name = xstrdup("sans");
	}
	if (!rc.font_menuitem.name) {
		rc.font_menuitem.name = xstrdup("sans");
	}
	if (!rc.font_osd.name) {
		rc.font_osd.name = xstrdup("sans");
	}
	if (!libinput_category_get_default()) {
		/* So we set default values of <tap> and <scrollFactor> */
		struct libinput_category *l = libinput_category_create();
		/* Prevents unused variable warning when compiled without asserts */
		(void)l;
		assert(l && libinput_category_get_default() == l);
	}
	int nr_workspaces = wl_list_length(&rc.workspace_config.workspaces);
	if (nr_workspaces < rc.workspace_config.min_nr_workspaces) {
		if (!rc.workspace_config.prefix) {
			rc.workspace_config.prefix = xstrdup(_("Workspace"));
		}

		struct buf b = BUF_INIT;
		for (int i = nr_workspaces; i < rc.workspace_config.min_nr_workspaces; i++) {
			struct workspace_config *conf = znew(*conf);
			if (!string_null_or_empty(rc.workspace_config.prefix)) {
				buf_add_fmt(&b, "%s ", rc.workspace_config.prefix);
			}
			buf_add_fmt(&b, "%d", i + 1);
			conf->name = xstrdup(b.data);
			wl_list_append(&rc.workspace_config.workspaces, &conf->link);
			buf_clear(&b);
		}
		buf_reset(&b);
	}
	if (rc.workspace_config.popuptime == INT_MIN) {
		rc.workspace_config.popuptime = 1000;
	}
	if (!wl_list_length(&rc.window_switcher.osd.fields)) {
		wlr_log(WLR_INFO, "load default window switcher fields");
		load_default_window_switcher_fields();
	}
}

static void
rule_destroy(struct window_rule *rule)
{
	wl_list_remove(&rule->link);
	zfree(rule->identifier);
	zfree(rule->title);
	zfree(rule->sandbox_engine);
	zfree(rule->sandbox_app_id);
	action_list_free(&rule->actions);
	zfree(rule);
}

static void
validate_actions(void)
{
	struct action *action, *action_tmp;

	struct keybind *keybind;
	wl_list_for_each(keybind, &rc.keybinds, link) {
		wl_list_for_each_safe(action, action_tmp, &keybind->actions, link) {
			if (!action_is_valid(action)) {
				wl_list_remove(&action->link);
				action_free(action);
				wlr_log(WLR_ERROR, "Removed invalid keybind action");
			}
		}
	}

	struct mousebind *mousebind;
	wl_list_for_each(mousebind, &rc.mousebinds, link) {
		wl_list_for_each_safe(action, action_tmp, &mousebind->actions, link) {
			if (!action_is_valid(action)) {
				wl_list_remove(&action->link);
				action_free(action);
				wlr_log(WLR_ERROR, "Removed invalid mousebind action");
			}
		}
	}

	struct window_rule *rule;
	wl_list_for_each(rule, &rc.window_rules, link) {
		wl_list_for_each_safe(action, action_tmp, &rule->actions, link) {
			if (!action_is_valid(action)) {
				wl_list_remove(&action->link);
				action_free(action);
				wlr_log(WLR_ERROR, "Removed invalid window rule action");
			}
		}
	}
}

static void
validate(void)
{
	/* Regions */
	struct region *region, *region_tmp;
	wl_list_for_each_safe(region, region_tmp, &rc.regions, link) {
		struct wlr_box box = region->percentage;
		bool invalid = !region->name
			|| box.x < 0 || box.x > 100
			|| box.y < 0 || box.y > 100
			|| box.width <= 0 || box.width > 100
			|| box.height <= 0 || box.height > 100;
		if (invalid) {
			wlr_log(WLR_ERROR,
				"Removing invalid region '%s': %d%% x %d%% @ %d%%,%d%%",
				region->name, box.width, box.height, box.x, box.y);
			wl_list_remove(&region->link);
			zfree(region->name);
			free(region);
		}
	}

	/* Window-rule criteria */
	struct window_rule *rule, *rule_tmp;
	wl_list_for_each_safe(rule, rule_tmp, &rc.window_rules, link) {
		if (!rule->identifier && !rule->title && rule->window_type < 0
				&& !rule->sandbox_engine && !rule->sandbox_app_id) {
			wlr_log(WLR_ERROR, "Deleting rule %p as it has no criteria", rule);
			rule_destroy(rule);
		}
	}

	validate_actions();

	/* OSD fields */
	int field_width_sum = 0;
	struct cycle_osd_field *field, *field_tmp;
	wl_list_for_each_safe(field, field_tmp, &rc.window_switcher.osd.fields, link) {
		field_width_sum += field->width;
		if (!cycle_osd_field_is_valid(field) || field_width_sum > 100) {
			wlr_log(WLR_ERROR, "Deleting invalid window switcher field %p", field);
			wl_list_remove(&field->link);
			cycle_osd_field_free(field);
		}
	}
}

void
rcxml_read(const char *filename)
{
	rcxml_init();
	config_toml_read();
	post_processing();
	validate();
}

void
rcxml_finish(void)
{
	zfree(rc.font_activewindow.name);
	zfree(rc.font_inactivewindow.name);
	zfree(rc.font_menuheader.name);
	zfree(rc.font_menuitem.name);
	zfree(rc.font_osd.name);
	zfree(rc.prompt_command);
	zfree(rc.theme_name);
	zfree(rc.icon_theme_name);
	zfree(rc.fallback_app_icon_name);
	zfree(rc.workspace_config.prefix);
	zfree(rc.workspace_config.initial_workspace_name);
	zfree(rc.tablet.output_name);
	zfree(rc.window_switcher.osd.thumbnail_label_format);

	clear_title_layout();

	struct usable_area_override *area, *area_tmp;
	wl_list_for_each_safe(area, area_tmp, &rc.usable_area_overrides, link) {
		wl_list_remove(&area->link);
		zfree(area->output);
		zfree(area);
	}

	struct keybind *k, *k_tmp;
	wl_list_for_each_safe(k, k_tmp, &rc.keybinds, link) {
		wl_list_remove(&k->link);
		action_list_free(&k->actions);
		keybind_destroy(k);
	}

	struct mousebind *m, *m_tmp;
	wl_list_for_each_safe(m, m_tmp, &rc.mousebinds, link) {
		wl_list_remove(&m->link);
		action_list_free(&m->actions);
		zfree(m);
	}

	struct touch_config_entry *touch_config, *touch_config_tmp;
	wl_list_for_each_safe(touch_config, touch_config_tmp, &rc.touch_configs, link) {
		wl_list_remove(&touch_config->link);
		zfree(touch_config->device_name);
		zfree(touch_config->output_name);
		zfree(touch_config);
	}

	struct libinput_category *l, *l_tmp;
	wl_list_for_each_safe(l, l_tmp, &rc.libinput_categories, link) {
		wl_list_remove(&l->link);
		zfree(l->name);
		zfree(l);
	}

	struct workspace_config *w, *w_tmp;
	wl_list_for_each_safe(w, w_tmp, &rc.workspace_config.workspaces, link) {
		wl_list_remove(&w->link);
		zfree(w->name);
		zfree(w);
	}

	regions_destroy(NULL, &rc.regions);

	clear_window_switcher_fields();

	struct window_rule *rule, *rule_tmp;
	wl_list_for_each_safe(rule, rule_tmp, &rc.window_rules, link) {
		rule_destroy(rule);
	}
}
