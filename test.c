/*
 * Vinci
 *
 * Copyright (C) 2025, 2026 Orastron Srl unipersonale
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
 * File author: Paolo Marrone, Stefano D'Angelo
 */

#define _XOPEN_SOURCE   600
#define _POSIX_C_SOURCE 200112L

#include "vinci.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <windows.h>
    #define SLEEP(ms) Sleep(ms)
#else
    #include <unistd.h>
    #define SLEEP(ms) usleep(ms * 1000)
#endif

static uint32_t floatToRGB(float input) {
    if (input < 0.0f) input = 0.0f;
    if (input > 1.0f) input = 1.0f;
    return (uint32_t)(input * 0xFFFFFF);
}

vinci *g;
window *w1, *w2;
char active_windows = 0;

static void on_close(window *w) {
	printf("on_close %p \n", (void*)w); fflush(stdout);
	window_free(w);
	active_windows--;
}

static void on_mouse_press (window *w, int32_t x, int32_t y, uint32_t state) {
	printf("on_mouse_press %p %d %d %u \n", (void*)w, x, y, state);

	uint32_t side = 50;
	uint32_t *data = (uint32_t*) malloc(side * side * 4);
	uint32_t p = 0;
	for (uint32_t i = 0; i < side; i++)
		for (uint32_t j = 0; j < side; j++, p++)
			data[p] = 0xff1122;
	window_draw(w, (unsigned char*)data, 0, 0, side, side, x - side / 2, y - side / 2, side, side);
	free(data);
}

static void on_mouse_release (window *w, int32_t x, int32_t y, uint32_t state) {
	printf("on_mouse_release %p %d %d %u \n", (void*)w, x, y, state);

	uint32_t side = 20;
	uint32_t *data = (uint32_t*) malloc(side * side * 4);
	uint32_t p = 0;
	for (uint32_t i = 0; i < side; i++)
		for (uint32_t j = 0; j < side; j++, p++)
			data[p] = 0x1122ff;
	window_draw(w, (unsigned char*)data, 0, 0, side, side, x - side / 2, y - side / 2, side, side);
	free(data);
}

static void on_mouse_move (window *w, int32_t x, int32_t y, uint32_t state) {
	printf("on_mouse_move %p %d %d %u \n", (void*)w, x, y, state);

	uint32_t side = 8;
	uint32_t *data = (uint32_t*) malloc(side * side * 4);
	uint32_t p = 0;
	for (uint32_t i = 0; i < side; i++)
		for (uint32_t j = 0; j < side; j++, p++)
			data[p] = 0x11ff22;
	window_draw(w, (unsigned char*)data, 0, 0, side, side, x - side / 2, y - side / 2, side, side);
	free(data);
}

static void on_window_resize (window *w, int32_t width, int32_t height) {
	printf("on_window_resize %p %d %d \n", (void*) w, width, height);
	if (width <= 0 || height <= 0)
		return;
	uint32_t color = floatToRGB(width > height ? (float) height / (float) width : (float) width / (float) height);
	uint32_t *data = (uint32_t*) malloc(width * height * 4);
	if (!data)
		return;
	uint32_t p = 0;
	for (int32_t i = 0; i < width; i++)
		for (int32_t j = 0; j < height; j++, p++) {
			data[p] = color;
		}
	window_draw(w, (unsigned char*)data, 0, 0, width, height, 0, 0, width, height);
	free(data);
}

static void tick(void) {
    vinci_idle(g);
}

int main (void) {

	// Vinci init
	g = vinci_new();
	if (!g) {
		fprintf(stderr, "Failed to initialize Vinci.\n");
		return 1;
	}

	// w1
	struct window_cbs w1cbs;
	memset(&w1cbs, 0, sizeof(window_cbs));
	w1cbs.on_window_close  = on_close;
	w1cbs.on_mouse_press   = on_mouse_press;
	w1cbs.on_mouse_release = on_mouse_release;
	w1cbs.on_mouse_move    = on_mouse_move;
	w1cbs.on_window_resize = on_window_resize;

	w1 = window_new(g, NULL, 300, 500, 1, &w1cbs);
	if (!w1) {
		fprintf(stderr, "Failed to create first window.\n");
		vinci_destroy(g);
		return 1;
	}
	on_window_resize(w1, window_get_width(w1), window_get_height(w1)); // This is needed for x only

	// w2

	struct window_cbs w2cbs;
	memset(&w2cbs, 0, sizeof(window_cbs));
	w2cbs.on_window_close  = on_close;
	w2cbs.on_mouse_press   = on_mouse_press;
	w2cbs.on_mouse_release = on_mouse_release;
	w2cbs.on_mouse_move    = on_mouse_move;
	w2cbs.on_window_resize = on_window_resize;

	w2 = window_new(g, NULL, 500, 500, 1, &w2cbs);
	if (!w2) {
		fprintf(stderr, "Failed to create second window.\n");
		window_free(w1);
		vinci_destroy(g);
		return 1;
	}
	on_window_resize(w2, window_get_width(w2), window_get_height(w2));

	// I dare you to find a worse timer :)
	active_windows = 2;
	while (active_windows > 0) {
        tick();
        SLEEP(20);
    }

	vinci_destroy(g);
	return 0;
}
