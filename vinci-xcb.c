/*
 * Vinci
 *
 * Copyright (C) 2021-2025 Orastron Srl unipersonale
 *
 * Vinci is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Vinci is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Vinci. If not, see <http://www.gnu.org/licenses/>.
 *
 * File author: Stefano D'Angelo, Paolo Marrone
 */

#include "vinci.h"

#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
//#include <stdio.h>

struct window {
	vinci          *g;
	window         *next;
	xcb_window_t    window;
	xcb_pixmap_t    pixmap;
	xcb_gcontext_t  gc;
	uint16_t        x;
	uint16_t        y;
	uint16_t        width;
	uint16_t        height;
	void           *bgra;
	void           *data;
	window_cbs      cbs;
};

struct vinci {
	xcb_connection_t   *connection;
	xcb_screen_t       *screen;
	const xcb_setup_t  *setup;
	xcb_visualtype_t   *visual;
	xcb_atom_t          wm_protocols_atom;
	xcb_atom_t          wm_delete_atom;
	xcb_atom_t          xembed_info_atom;
	window             *windows;
};

vinci* vinci_new(void) {
	vinci* g = (vinci*) malloc(sizeof(vinci));
	if (g == NULL)
		return NULL;

	g->connection = NULL;
	g->connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(g->connection)) {
		xcb_disconnect(g->connection);
		free(g);
		return NULL;
	}
	g->screen = xcb_setup_roots_iterator(xcb_get_setup(g->connection)).data;
	g->setup = xcb_get_setup(g->connection);

	xcb_visualtype_iterator_t visual_iter; xcb_intern_atom_reply_t* reply; // stupid C++ and its cross-initialization issues...

	xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(g->screen);
	xcb_depth_t *depth = NULL;
	while (depth_iter.rem) {
		if (depth_iter.data->depth == 24 && depth_iter.data->visuals_len) {
			depth = depth_iter.data;
			break;
		}
		xcb_depth_next(&depth_iter);
	}
	if (!depth)
		goto err;

	visual_iter = xcb_depth_visuals_iterator(depth);
	g->visual = NULL;
	while (visual_iter.rem) {
		if (visual_iter.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
			g->visual = visual_iter.data;
			break;
		}
		xcb_visualtype_next(&visual_iter);
	}
	if (!g->visual)
		goto err;

	reply = xcb_intern_atom_reply(g->connection, xcb_intern_atom(g->connection, 1, 12, "WM_PROTOCOLS"), NULL);
	if (!reply)
		goto err;
	g->wm_protocols_atom = reply->atom;
	free(reply);

	reply = xcb_intern_atom_reply(g->connection, xcb_intern_atom(g->connection, 0, 16, "WM_DELETE_WINDOW"), NULL);
	if (!reply)
		goto err;
	g->wm_delete_atom = reply->atom;
	free(reply);

	reply = xcb_intern_atom_reply(g->connection, xcb_intern_atom(g->connection, 0, 12, "_XEMBED_INFO"), NULL);
	if (!reply)
		goto err;
	g->xembed_info_atom = reply->atom;
	free(reply);

	g->windows = NULL;

	return g;

err:
	if (g->connection)
		xcb_disconnect(g->connection);
	free(g);
	return NULL;
}

void vinci_destroy(vinci* g) {
	while (g->windows)
		window_free(g->windows);
	xcb_disconnect(g->connection);
	free(g);
}

static window* get_window(vinci* g, xcb_window_t xw) {
	window* w = g->windows;
	while (w && w->window != xw)
		w = (window*)w->next;
	return w;
}

static uint32_t get_mouse_state(uint16_t state, xcb_button_t last) {
	return (
		(state & XCB_BUTTON_MASK_1) >> 8
	| 	(state & XCB_BUTTON_MASK_2) >> 7 
	| 	(state & XCB_BUTTON_MASK_3) >> 9	)
	^	(last == 1 ? 1 : (last == 2 ? 4 : (last == 3 ? 2 : 0)))
	;
}

void vinci_idle(vinci *g) {
	xcb_generic_event_t *ev;
	while ((ev = xcb_poll_for_event(g->connection))) {
		switch (ev->response_type & ~0x80) {
		case XCB_EXPOSE:
		{
			xcb_expose_event_t *x = (xcb_expose_event_t *)ev;
			window* w = get_window(g, x->window);
			if (!w)
				break;

			xcb_copy_area(g->connection, w->pixmap, w->window, w->gc, x->x, x->y, x->x, x->y, x->width, x->height);
			xcb_flush(g->connection);
		}
			break;

		case XCB_CONFIGURE_NOTIFY:
		{
			xcb_configure_notify_event_t *x = (xcb_configure_notify_event_t *)ev;
			window* w = get_window(g, x->window);
			if (!w)
				break;
			
			xcb_translate_coordinates_reply_t *trans =
				xcb_translate_coordinates_reply(g->connection, xcb_translate_coordinates(g->connection, w->window, g->screen->root, 0, 0), NULL);
			if (trans) {
				if (trans->dst_x != w->x || trans->dst_y != w->y) {
					w->x = trans->dst_x;
					w->y = trans->dst_y;
					if (w->cbs.on_window_move)
						w->cbs.on_window_move(w, trans->dst_x, trans->dst_y);
				}
				free(trans);
			}
		
			if (x->width != w->width || x->height != w->height) {
				void *new_bgra = realloc(w->bgra, ((uint32_t)x->width * (uint32_t)x->height) << 2);
				if (!new_bgra)
					break;
				w->bgra = (uint8_t*) new_bgra;

				w->width = x->width;
				w->height = x->height;
				xcb_free_pixmap(g->connection, w->pixmap);
				w->pixmap = xcb_generate_id(g->connection);
				xcb_create_pixmap(g->connection, g->screen->root_depth, w->pixmap, w->window, x->width, x->height);

				if (w->cbs.on_window_resize)
					w->cbs.on_window_resize(w, x->width, x->height);
			}
		}
			break;

		case XCB_CLIENT_MESSAGE:
		{
			xcb_client_message_event_t *x = (xcb_client_message_event_t *)ev;
			window* w = get_window(g, x->window);
			if (!w)
				break;
			if (w->cbs.on_window_close) {
				if (x->data.data32[0] == g->wm_delete_atom)
					w->cbs.on_window_close(w);
			}
		}
			break;

		case XCB_BUTTON_PRESS:
		{
			xcb_button_press_event_t *x = (xcb_button_press_event_t *)ev;
			window* w = get_window(g, x->event);
			if (!w)
				break;
			// First three buttons
			if (w->cbs.on_mouse_press) {
				if (x->detail <= 3)
					w->cbs.on_mouse_press(w, x->event_x, x->event_y, get_mouse_state(x->state, x->detail));
			}
			// wheel
			if (w->cbs.on_mouse_wheel) {
				if (x->detail == 4)
					w->cbs.on_mouse_wheel(w, x->event_x, x->event_y, 57);
				else if (x->detail == 5)
					w->cbs.on_mouse_wheel(w, x->event_x, x->event_y, -57);
			}
		}
			break;

		case XCB_BUTTON_RELEASE:
		{
			xcb_button_release_event_t *x = (xcb_button_release_event_t *)ev;
			window* w = get_window(g, x->event);
			if (!w)
				break;

			// First three buttons
			if (x->detail <= 3) {
				if (w->cbs.on_mouse_release)
					w->cbs.on_mouse_release(w, x->event_x, x->event_y, get_mouse_state(x->state, x->detail));
			}
			// Mouse wheel
			else {
				// Nothing to do
			}
		}
			break;

		case XCB_MOTION_NOTIFY:
		{
			xcb_motion_notify_event_t *x = (xcb_motion_notify_event_t *)ev;
			window* w = get_window(g, x->event);
			if (!w)
				break;
			if (w->cbs.on_mouse_move)
				w->cbs.on_mouse_move(w, x->event_x, x->event_y, get_mouse_state(x->state, 0));
		}
			break;

		case XCB_ENTER_NOTIFY:
		{
			xcb_enter_notify_event_t *x = (xcb_enter_notify_event_t *)ev;
			window* w = get_window(g, x->event);
			if (!w)
				break;
			if (w->cbs.on_mouse_enter)
				w->cbs.on_mouse_enter(w, x->event_x, x->event_y, get_mouse_state(x->state, 0));
		}
			break;

		case XCB_LEAVE_NOTIFY:
		{
			xcb_leave_notify_event_t *x = (xcb_leave_notify_event_t *)ev;
			window* w = get_window(g, x->event);
			if (!w)
				break;
			if (w->cbs.on_mouse_leave)
				w->cbs.on_mouse_leave(w, x->event_x, x->event_y, get_mouse_state(x->state, 0));
		}
			break;

		case XCB_KEY_PRESS:
		{
			xcb_key_press_event_t *x = (xcb_key_press_event_t *)ev;
			window* w = get_window(g, x->event);
			if (!w)
				break;
			if (w->cbs.on_key_press)
				w->cbs.on_key_press(w, x->detail, x->state);
		}
			break;

		case XCB_KEY_RELEASE:
		{
			xcb_key_release_event_t *x = (xcb_key_release_event_t *)ev;
			window* w = get_window(g, x->event);
			if (!w)
				break;
			if (w->cbs.on_key_release)
				w->cbs.on_key_release(w, x->detail, x->state);
		}
			break;

		default:
			break;
		}
		free(ev);
	}
}

#define XEMBED_MAPPED (1 << 0)

window* window_new(vinci* g, void* p, uint32_t width, uint32_t height, window_cbs *cbs) {
	// make C++ compilers happy
	uint32_t mask;
	uint32_t values[1];
	xcb_window_t parent;
	uint32_t xembed_info[2];

	window* ret = (window*) malloc(sizeof(window));
	if (ret == NULL)
		goto err_alloc;
	ret->bgra = malloc((width * height) << 2);
	if (!ret->bgra)
		goto err_bgra;

	ret->window = xcb_generate_id(g->connection);
	mask = XCB_CW_EVENT_MASK;
	values[0] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
		    | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
		    | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION
		    | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
		    | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

	// for some reason jalv.gtk3 doesn't like passing parent here but needs reparenting later
	xcb_create_window(g->connection, 24, ret->window, g->screen->root,
			  0, 0, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  g->visual->visual_id, mask, values);
	parent = (xcb_window_t)(uintptr_t)p;
	if (parent)
		xcb_reparent_window(g->connection, ret->window, parent, 0, 0);
	
	ret->pixmap = xcb_generate_id(g->connection);
	xcb_create_pixmap(g->connection, g->screen->root_depth, ret->pixmap, ret->window, width, height);

	ret->gc = xcb_generate_id(g->connection);
	xcb_create_gc(g->connection, ret->gc, ret->pixmap, 0, NULL);

	xembed_info[0] = 0;
	xembed_info[1] = 0;
	xcb_change_property(g->connection, XCB_PROP_MODE_REPLACE, ret->window, g->xembed_info_atom, g->xembed_info_atom, 32, 2, xembed_info);

	// subscribe to window close events
	xcb_change_property(g->connection, XCB_PROP_MODE_REPLACE, ret->window, g->wm_protocols_atom, XCB_ATOM_ATOM, 32, 1, &(g->wm_delete_atom));

	xcb_flush(g->connection);

	ret->next = NULL;
	if (g->windows == NULL)
		g->windows = ret;
	else {
		window* n = g->windows;
		while (n->next)
			n = (window*) n->next;
		n->next = ret;
	}

	ret->g = g;
	ret->x = 0;
	ret->y = 0;
	ret->width = width;
	ret->height = height;
	ret->data = NULL;
	if (cbs)
		ret->cbs = *cbs;
	else
		memset(&ret->cbs, 0, sizeof(window_cbs));

	return ret;

err_bgra:
	free(ret);
err_alloc:
	return NULL;
}

void window_free(window* w) {
	if (w->g->windows == w)
		w->g->windows = w->next;
	else {
		window* n = w->g->windows;
		while (n && (window*) n->next != w)
			n = (window*) n->next;
		if (!n)
			return;
		n->next = w->next;
	}

	xcb_free_gc(w->g->connection, w->gc);
	xcb_free_pixmap(w->g->connection, w->pixmap);
	xcb_destroy_window(w->g->connection, w->window);
	xcb_flush(w->g->connection);
	free(w->bgra);
	free(w);
}

void window_draw(window* w, unsigned char *data, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height) {
	if (dx < 0) {
		width += dx;
		wx -= dx;
		dx = 0;
	}
	if (dy < 0) {
		height += dy;
		wy -= dy;
		dy = 0;
	}

	const int32_t wx_ = wx < 0 ? 0 : wx;
	const int32_t wy_ = wy < 0 ? 0 : wy;
	width  = width  - (wx_ - wx);
	height = height - (wy_ - wy);
	if (width <= 0 || height <= 0 )
		return;
	dx = dx + (wx_ - wx);
	dy = dy + (wy_ - wy);
	wx = wx_;
	wy = wy_;

	if (dx >= dw || dy >= dh)
		return;
	if (wx >= w->width || wy >= w->height)
		return;
	if (width > dw - dx)
		width = dw - dx;
	if (height > dh - dy)
		height = dh - dy;
	if (width > (int32_t)w->width - wx)
		width = (int32_t)w->width - wx;
	if (height > (int32_t)w->height - wy)
		height = (int32_t)w->height - wy;
	if (width <= 0 || height <= 0)
		return;

	uint8_t *bgra = (uint8_t*) w->bgra;

	int32_t i = 0;
	int32_t o = (dw * dy + dx) << 2;
	int32_t p = (dw - width) << 2;

	if (w->g->setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST) {
		for (int32_t y = dy; y < dy + height; y++) {
			for (int32_t x = dx; x < dx + width; x++, o += 4) {
				bgra[i++] = data[o + 2];
				bgra[i++] = data[o + 1];
				bgra[i++] = data[o    ];
				i++;
			}
			o += p;
		}
	} else {
		for (int32_t y = dy; y < dy + height; y++) {
			for (int32_t x = dx; x < dx + width; x++, o += 4) {
				bgra[i++] = data[o    ];
				bgra[i++] = data[o + 1];
				bgra[i++] = data[o + 2];
				i++;
			}
			o += p;
		}
	}

	const uint32_t bytes = (width * height) << 2;
	xcb_put_image(w->g->connection, XCB_IMAGE_FORMAT_Z_PIXMAP, w->pixmap, w->gc, width, height, wx, wy, 0, w->g->screen->root_depth, bytes, bgra);
	xcb_copy_area(w->g->connection, w->pixmap, w->window, w->gc, wx, wy, wx, wy, width, height);
	xcb_flush(w->g->connection);
}

void *window_get_handle(window* w) {
	return (void *)(uintptr_t)(w->window);
}

uint32_t window_get_width(window* w) {
	return w->width;
}
uint32_t window_get_height(window* w) {
	return w->height;
}
/*	
	// In case we need to get w and h from x:
	xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(w->g->connection, w->window);
    xcb_get_geometry_reply_t *cr = xcb_get_geometry_reply(w->g->connection, geom_cookie, NULL);
	cr->width; cr->height;
*/

void window_resize(window* w, uint32_t width, uint32_t height) {
	const uint32_t values[] = { width, height };
	xcb_configure_window(w->g->connection, w->window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
	xcb_clear_area(w->g->connection, 1, w->window, 0, 0, width, height);
	xcb_flush(w->g->connection);
}

void window_move (window* w, uint32_t x, uint32_t y) {
	const uint32_t values[] = { x, y };
	xcb_configure_window(w->g->connection, w->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
	xcb_flush(w->g->connection);
}

void window_show(window* w) {
	uint32_t xembed_info[] = { 0, XEMBED_MAPPED };
	xcb_change_property(w->g->connection, XCB_PROP_MODE_REPLACE, w->window, w->g->xembed_info_atom, w->g->xembed_info_atom, 32, 2, xembed_info);
	xcb_map_window(w->g->connection, w->window);
	xcb_flush(w->g->connection);
}

void window_hide(window* w) {
	uint32_t xembed_info[] = { 0, 0 };
	xcb_change_property(w->g->connection, XCB_PROP_MODE_REPLACE, w->window, w->g->xembed_info_atom, w->g->xembed_info_atom, 32, 2, xembed_info);
	xcb_unmap_window(w->g->connection, w->window);
	xcb_flush(w->g->connection);
}

void window_set_data(window* w, void *data) {
	w->data = data;
}

void *window_get_data(window* w) {
	return w->data;
}
