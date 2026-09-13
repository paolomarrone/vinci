/*
 * Vinci
 *
 * Copyright (C) 2021-2026 Orastron Srl unipersonale
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

#include <stddef.h>
#include <stdlib.h>

#define IMPORT(name) __attribute__((import_module("vinci_web"), import_name(#name)))

// Private ABI, mirrored by vinci-web.js. All arguments are wasm32 integers.
IMPORT(init) int web_init(vinci *g);
IMPORT(destroy) void web_destroy(vinci *g);
IMPORT(create) uint32_t web_create(window *w, vinci *g, void *parent, uint32_t width, uint32_t height, int visible);
IMPORT(remove) void web_remove(uint32_t id);
IMPORT(resize) int web_resize(uint32_t id, uint32_t width, uint32_t height);
IMPORT(move) void web_move(uint32_t id, uint32_t x, uint32_t y);
IMPORT(show) void web_show(uint32_t id, int visible);
IMPORT(draw) void web_draw(uint32_t id, const unsigned char *data, int32_t dx, int32_t dy, int32_t dw, int32_t wx, int32_t wy, int32_t width, int32_t height);
IMPORT(notify) void web_notify(uint32_t id, int type, int32_t x, int32_t y, uint32_t state);
IMPORT(pending) int web_pending(vinci *g);
IMPORT(poll) int web_poll(vinci *g, int32_t *event);
IMPORT(present) void web_present(vinci *g);

enum { RESIZE = 1, CLOSE, MOVE, MOUSE_MOVE, MOUSE_PRESS, MOUSE_RELEASE,
       MOUSE_WHEEL, MOUSE_ENTER, MOUSE_LEAVE, KEY_PRESS, KEY_RELEASE };

struct window {
	vinci *g;
	window *next;
	uint32_t id, width, height, x, y;
	window_cbs cbs;
	void *data;
};

struct vinci {
	window *windows;
	char idle, destroyed;
};

vinci * vinci_new(void) {
	vinci *g = (vinci *)calloc(1, sizeof(vinci));
	if (g && !web_init(g)) {
		free(g);
		return NULL;
	}
	return g;
}

void vinci_destroy(vinci *g) {
	if (!g || g->destroyed)
		return;
	g->destroyed = 1;
	while (g->windows)
		window_free(g->windows);
	web_destroy(g);
	// A callback may destroy its context while vinci_idle is dispatching it.
	if (!g->idle)
		free(g);
}

void vinci_idle(vinci *g) {
	if (!g || g->idle || g->destroyed)
		return;
	g->idle = 1;
	int count = web_pending(g);
	for (int i = 0; i < count && !g->destroyed; i++) {
		int32_t e[5];
		if (!web_poll(g, e))
			break;
		window *w = (window *)(uintptr_t)(uint32_t)e[0];
		// poll drops events for freed windows, even if an allocator reuses w.
		switch (e[1]) {
		case RESIZE:
			if (w->width != (uint32_t)e[2] || w->height != (uint32_t)e[3]) {
				if (!web_resize(w->id, (uint32_t)e[2], (uint32_t)e[3]))
				break;
				w->width = (uint32_t)e[2];
				w->height = (uint32_t)e[3];
			}
			if (w->cbs.on_window_resize)
				w->cbs.on_window_resize(w, e[2], e[3]);
			break;
		case CLOSE:
			if (w->cbs.on_window_close)
				w->cbs.on_window_close(w);
			else
				window_free(w);
			break;
		case MOVE:
			if (w->cbs.on_window_move)
				w->cbs.on_window_move(w, (uint32_t)e[2], (uint32_t)e[3]);
			break;
		case MOUSE_MOVE:
			if (w->cbs.on_mouse_move) w->cbs.on_mouse_move(w, e[2], e[3], (uint32_t)e[4]);
			break;
		case MOUSE_PRESS:
			if (w->cbs.on_mouse_press) w->cbs.on_mouse_press(w, e[2], e[3], (uint32_t)e[4]);
			break;
		case MOUSE_RELEASE:
			if (w->cbs.on_mouse_release) w->cbs.on_mouse_release(w, e[2], e[3], (uint32_t)e[4]);
			break;
		case MOUSE_WHEEL:
			if (w->cbs.on_mouse_wheel) w->cbs.on_mouse_wheel(w, e[2], e[3], (uint32_t)e[4]);
			break;
		case MOUSE_ENTER:
			if (w->cbs.on_mouse_enter) w->cbs.on_mouse_enter(w, e[2], e[3], (uint32_t)e[4]);
			break;
		case MOUSE_LEAVE:
			if (w->cbs.on_mouse_leave) w->cbs.on_mouse_leave(w, e[2], e[3], (uint32_t)e[4]);
			break;
		case KEY_PRESS:
			if (w->cbs.on_key_press) w->cbs.on_key_press(w, (uint32_t)e[2], (uint32_t)e[4]);
			break;
		case KEY_RELEASE:
			if (w->cbs.on_key_release) w->cbs.on_key_release(w, (uint32_t)e[2], (uint32_t)e[4]);
			break;
		}
	}
	if (!g->destroyed)
		web_present(g);
	g->idle = 0;
	if (g->destroyed)
		free(g);
}

window * window_new(vinci *g, void *parent, uint32_t width, uint32_t height, char visible, window_cbs *cbs) {
	if (!g || g->destroyed || width > INT32_MAX || height > INT32_MAX)
		return NULL;
	window *w = (window *)calloc(1, sizeof(window));
	if (!w)
		return NULL;
	w->g = g;
	w->width = width;
	w->height = height;
	if (cbs)
		w->cbs = *cbs;
	w->id = web_create(w, g, parent, width, height, visible != 0);
	if (!w->id) {
		free(w);
		return NULL;
	}
	w->next = g->windows;
	g->windows = w;
	return w;
}

void window_free(window *w) {
	if (!w)
		return;
	window **link = &w->g->windows;
	while (*link != w)
		link = &(*link)->next;
	*link = w->next;
	web_remove(w->id);
	free(w);
}

void window_draw(window *w, unsigned char *data, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height) {
	if (!data || dw <= 0 || dh <= 0 || width <= 0 || height <= 0)
		return;
	// Clip both rectangles together; 64-bit intermediates also cover INT32_MIN.
	int64_t left = 0, top = 0, right = width, bottom = height;
	if (left < -(int64_t)dx) left = -(int64_t)dx;
	if (left < -(int64_t)wx) left = -(int64_t)wx;
	if (top < -(int64_t)dy) top = -(int64_t)dy;
	if (top < -(int64_t)wy) top = -(int64_t)wy;
	if (right > (int64_t)dw - dx) right = (int64_t)dw - dx;
	if (right > (int64_t)w->width - wx) right = (int64_t)w->width - wx;
	if (bottom > (int64_t)dh - dy) bottom = (int64_t)dh - dy;
	if (bottom > (int64_t)w->height - wy) bottom = (int64_t)w->height - wy;
	if (right <= left || bottom <= top)
		return;
	web_draw(w->id, data, (int32_t)(dx + left), (int32_t)(dy + top), dw,
		(int32_t)(wx + left), (int32_t)(wy + top), (int32_t)(right - left), (int32_t)(bottom - top));
}

void * window_get_handle(window *w) { return (void *)(uintptr_t)w->id; }
uint32_t window_get_width(window *w) { return w->width; }
uint32_t window_get_height(window *w) { return w->height; }

void window_resize(window *w, uint32_t width, uint32_t height) {
	if (width > INT32_MAX || height > INT32_MAX || (width == w->width && height == w->height))
		return;
	if (!web_resize(w->id, width, height))
		return;
	w->width = width;
	w->height = height;
	web_notify(w->id, RESIZE, (int32_t)width, (int32_t)height, 0);
}

void window_move(window *w, uint32_t x, uint32_t y) {
	if (w->x == x && w->y == y)
		return;
	w->x = x;
	w->y = y;
	web_move(w->id, x, y);
	web_notify(w->id, MOVE, (int32_t)x, (int32_t)y, 0);
}

void window_show(window *w) { web_show(w->id, 1); }
void window_hide(window *w) { web_show(w->id, 0); }
void window_set_data(window *w, void *data) { w->data = data; }
void * window_get_data(window *w) { return w->data; }
