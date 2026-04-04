// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "config/rcxml.h"
#include "common/list.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "node.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "scaled-buffer/scaled-img-buffer.h"
#include "ssd.h"
#include "ssd-internal.h"

/* Internal API */

struct ssd_button *
attach_ssd_button(struct wl_list *button_parts, enum lab_node_type type,
		struct wlr_scene_tree *parent,
		struct lab_img *imgs[LAB_BS_ALL + 1],
		int x, int y, struct view *view, enum lab_button_corner corner)
{
	struct wlr_scene_tree *root = lab_wlr_scene_tree_create(parent);
	wlr_scene_node_set_position(&root->node, x, y);

	assert(node_type_contains(LAB_NODE_BUTTON, type));
	struct ssd_button *button = znew(*button);
	button->node = &root->node;
	button->type = type;
	node_descriptor_create(&root->node, type, view, button);
	wl_list_append(button_parts, &button->link);

	/* Hitbox */
	float invisible[4] = { 0, 0, 0, 0 };
	lab_wlr_scene_rect_create(root, rc.theme->window_button_width,
		rc.theme->window_button_height, invisible);

	/* Icons */
	int button_width = rc.theme->window_button_width;
	int button_height = rc.theme->window_button_height;
	/*
	 * Ensure a small amount of horizontal padding within the button
	 * area (2px on each side with the default 26px button width).
	 * A new theme setting could be added to configure this. Using
	 * an existing setting (padding.width or window.button.spacing)
	 * was considered, but these settings have distinct purposes
	 * already and are zero by default.
	 */
	int icon_padding = button_width / 10;

	if (type == LAB_NODE_BUTTON_WINDOW_ICON) {
		int app_icon_size = rc.theme->window_button_app_icon_size;
		int iw, ih, ix, iy;
		if (app_icon_size > 0) {
			iw = app_icon_size;
			ih = app_icon_size;
			ix = (button_width - iw) / 2;
			iy = (button_height - ih) / 2;
		} else {
			/* legacy formula: 80% width, full height, left-padded */
			iw = button_width - 2 * icon_padding;
			ih = button_height;
			ix = icon_padding;
			iy = 0;
		}
		struct scaled_icon_buffer *icon_buffer =
			scaled_icon_buffer_create(root, iw, ih);
		assert(icon_buffer);
		struct wlr_scene_node *icon_node = &icon_buffer->scene_buffer->node;
		scaled_icon_buffer_set_view(icon_buffer, view);
		wlr_scene_node_set_position(icon_node, ix, iy);
		button->window_icon = icon_buffer;
	} else {
		int icon_size = rc.theme->window_button_icon_size;
		int hover_bg_size = rc.theme->window_button_hover_bg_size;
		int hbg_w = (hover_bg_size > 0) ? hover_bg_size : button_width;
		int hbg_h = (hover_bg_size > 0) ? hover_bg_size : button_height;
		int hbg_x = (button_width - hbg_w) / 2;
		int hbg_y = (button_height - hbg_h) / 2;

		/*
		 * Hover background: added first so it sits behind the icon in
		 * the scene graph. Initially hidden; shown on hover.
		 */
		struct scaled_img_buffer *hover_bg = scaled_img_buffer_create(
			root, rc.theme->hover_bg_img, hbg_w, hbg_h);
		assert(hover_bg);
		wlr_scene_node_set_position(&hover_bg->scene_buffer->node, hbg_x, hbg_y);
		wlr_scene_node_set_enabled(&hover_bg->scene_buffer->node, false);
		button->hover_bg = hover_bg;

		/*
		 * For corner buttons, also create a rounded hover_bg that clips
		 * the outer corner to match the titlebar corner radius. Only
		 * needed when the hover bg fills the full button — if it's
		 * smaller (hover_bg_size > 0) it's centered and can't reach
		 * the titlebar edge, so no clipping is required.
		 */
		if (corner != LAB_BUTTON_CORNER_NONE && hover_bg_size == 0) {
			struct lab_img *rounded_img = (corner == LAB_BUTTON_CORNER_LEFT)
				? rc.theme->hover_bg_left_img
				: rc.theme->hover_bg_right_img;
			struct scaled_img_buffer *hover_bg_rounded =
				scaled_img_buffer_create(root, rounded_img, hbg_w, hbg_h);
			assert(hover_bg_rounded);
			wlr_scene_node_set_position(
				&hover_bg_rounded->scene_buffer->node, hbg_x, hbg_y);
			wlr_scene_node_set_enabled(
				&hover_bg_rounded->scene_buffer->node, false);
			button->hover_bg_rounded = hover_bg_rounded;
		}

		for (uint8_t state_set = LAB_BS_DEFAULT;
				state_set <= LAB_BS_ALL; state_set++) {
			if (!imgs[state_set]) {
				continue;
			}
			int w = (icon_size > 0) ? icon_size : button_width;
			int h = (icon_size > 0) ? icon_size : button_height;
			int x = (button_width - w) / 2;
			int y = (button_height - h) / 2;
			struct scaled_img_buffer *img_buffer = scaled_img_buffer_create(
				root, imgs[state_set], w, h);
			assert(img_buffer);
			struct wlr_scene_node *icon_node = &img_buffer->scene_buffer->node;
			wlr_scene_node_set_position(icon_node, x, y);
			wlr_scene_node_set_enabled(icon_node, false);
			button->img_buffers[state_set] = img_buffer;
		}
		/* Initially show non-hover, non-toggled, unrounded variant */
		wlr_scene_node_set_enabled(
			&button->img_buffers[LAB_BS_DEFAULT]->scene_buffer->node, true);
	}

	return button;
}

/* called from node descriptor destroy */
void ssd_button_free(struct ssd_button *button)
{
	wl_list_remove(&button->link);
	free(button);
}
