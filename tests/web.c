/* Vinci browser regression fixture, GPL-3.0-or-later. */
#include "vinci.h"
#include <stdlib.h>

#define EXPORT(name) __attribute__((export_name(#name)))

typedef struct fixture {
	vinci *g;
	window *w;
	int counts[12], last[4], action;
} fixture;

EXPORT(test_new) fixture *test_new(void *parent, int visible);
EXPORT(test_free) void test_free(fixture *f);
EXPORT(test_window) window *test_window(fixture *f);
EXPORT(test_context) vinci *test_context(fixture *f);
EXPORT(test_count) int test_count(fixture *f, int type);
EXPORT(test_last) int test_last(fixture *f, int field);
EXPORT(test_action) void test_action(fixture *f, int action);
EXPORT(test_draw) void test_draw(fixture *f, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height);
EXPORT(test_alloc) void *test_alloc(uint32_t size);
EXPORT(test_release) void test_release(void *ptr);

static void record(window *w, int type, int32_t x, int32_t y, uint32_t state) {
	fixture *f = (fixture *)window_get_data(w);
	f->counts[type]++;
	f->last[0] = type;
	f->last[1] = x;
	f->last[2] = y;
	f->last[3] = (int32_t)state;
	if (f->action == 1) { f->w = NULL; window_free(w); }
	else if (f->action == 2) { vinci_destroy(f->g); f->g = NULL; f->w = NULL; }
	else if (f->action == 3) vinci_idle(f->g);
}

static void resize_cb(window *w, int32_t x, int32_t y) { record(w, 1, x, y, 0); }
static void close_cb(window *w) {
	fixture *f = (fixture *)window_get_data(w);
	record(w, 2, 0, 0, 0);
	if (f->w) { window_free(w); f->w = NULL; }
}
static void move_cb(window *w, uint32_t x, uint32_t y) { record(w, 3, (int32_t)x, (int32_t)y, 0); }
static void mouse_move(window *w, int32_t x, int32_t y, uint32_t s) { record(w, 4, x, y, s); }
static void mouse_press(window *w, int32_t x, int32_t y, uint32_t s) { record(w, 5, x, y, s); }
static void mouse_release(window *w, int32_t x, int32_t y, uint32_t s) { record(w, 6, x, y, s); }
static void mouse_wheel(window *w, int32_t x, int32_t y, uint32_t s) { record(w, 7, x, y, s); }
static void mouse_enter(window *w, int32_t x, int32_t y, uint32_t s) { record(w, 8, x, y, s); }
static void mouse_leave(window *w, int32_t x, int32_t y, uint32_t s) { record(w, 9, x, y, s); }
static void key_press(window *w, uint32_t code, uint32_t s) { record(w, 10, (int32_t)code, 0, s); }
static void key_release(window *w, uint32_t code, uint32_t s) { record(w, 11, (int32_t)code, 0, s); }

fixture *test_new(void *parent, int visible) {
	fixture *f = (fixture *)calloc(1, sizeof(fixture));
	if (!f) return NULL;
	f->g = vinci_new();
	if (!f->g) { free(f); return NULL; }
	window_cbs cbs = { resize_cb, close_cb, move_cb, mouse_move, mouse_press,
		mouse_release, mouse_wheel, mouse_enter, mouse_leave, key_press, key_release };
	f->w = window_new(f->g, parent, 64, 48, (char)visible, &cbs);
	if (!f->w) { vinci_destroy(f->g); free(f); return NULL; }
	window_set_data(f->w, f);
	return f;
}

void test_free(fixture *f) { if (f->g) vinci_destroy(f->g); free(f); }
window *test_window(fixture *f) { return f->w; }
vinci *test_context(fixture *f) { return f->g; }
int test_count(fixture *f, int type) { return f->counts[type]; }
int test_last(fixture *f, int field) { return f->last[field]; }
void test_action(fixture *f, int action) { f->action = action; }

void test_draw(fixture *f, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height) {
	uint32_t pixels[] = { 0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x123456, 0xabcdef };
	window_draw(f->w, (unsigned char *)pixels, dx, dy, dw, dh, wx, wy, width, height);
}
void *test_alloc(uint32_t size) { return malloc(size); }
void test_release(void *ptr) { free(ptr); }
