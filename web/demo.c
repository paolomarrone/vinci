/* Vinci - GPL-3.0-or-later. Copyright (C) 2026 Orastron Srl unipersonale. */
#include "vinci.h"
#include <stdlib.h>

#define EXPORT(name) __attribute__((export_name(#name)))

typedef struct demo {
	vinci *g;
	window *w;
	uint32_t color, events, key;
} demo;

EXPORT(demo_new) demo *demo_new(void *parent, uint32_t color);
EXPORT(demo_free) void demo_free(demo *d);
EXPORT(demo_handle) void *demo_handle(demo *d);
EXPORT(demo_events) uint32_t demo_events(demo *d);
EXPORT(demo_key) uint32_t demo_key(demo *d);

static void paint(window *w, int32_t width, int32_t height) {
	demo *d = (demo *)window_get_data(w);
	uint32_t row[256];
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x += 256) {
			int32_t n = width - x < 256 ? width - x : 256;
			for (int32_t i = 0; i < n; i++)
				row[i] = ((x + i) / 24 + y / 24) % 2 ? d->color : 0x182431;
			window_draw(w, (unsigned char *)row, 0, 0, n, 1, x, y, n, 1);
		}
	}
	d->events++;
}

static void mouse(window *w, int32_t x, int32_t y, uint32_t state) {
	demo *d = (demo *)window_get_data(w);
	uint32_t brush[81];
	for (int i = 0; i < 81; i++) brush[i] = state ? 0xffc857 : 0xffffff;
	window_draw(w, (unsigned char *)brush, 0, 0, 9, 9, x - 4, y - 4, 9, 9);
	d->events++;
}

static void key(window *w, uint32_t code, uint32_t state) {
	(void)state;
	demo *d = (demo *)window_get_data(w);
	d->key = code;
	d->color ^= 0x808080;
	paint(w, (int32_t)window_get_width(w), (int32_t)window_get_height(w));
}

static void close_window(window *w) {
	demo *d = (demo *)window_get_data(w);
	d->w = NULL;
	window_free(w);
}

demo *demo_new(void *parent, uint32_t color) {
	demo *d = (demo *)calloc(1, sizeof(demo));
	if (!d) return NULL;
	d->color = color;
	d->g = vinci_new();
	if (!d->g) { free(d); return NULL; }
	window_cbs cbs = { 0 };
	cbs.on_window_resize = paint;
	cbs.on_window_close = close_window;
	cbs.on_mouse_move = mouse;
	cbs.on_mouse_press = mouse;
	cbs.on_mouse_release = mouse;
	cbs.on_key_press = key;
	d->w = window_new(d->g, parent, 320, 192, 1, &cbs);
	if (!d->w) { vinci_destroy(d->g); free(d); return NULL; }
	window_set_data(d->w, d);
	paint(d->w, 320, 192);
	return d;
}

void demo_free(demo *d) {
	if (!d) return;
	vinci_destroy(d->g);
	free(d);
}

void *demo_handle(demo *d) { return d->w ? window_get_handle(d->w) : NULL; }
uint32_t demo_events(demo *d) { return d->events; }
uint32_t demo_key(demo *d) { return d->key; }
