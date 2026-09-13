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


#ifndef VINCI_H
#define VINCI_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct vinci vinci;
typedef struct window window;
typedef struct window_cbs {
	void (*on_window_resize) (window *w, int32_t width, int32_t height);
	void (*on_window_close)  (window *w);
	void (*on_window_move)   (window *w, uint32_t x, uint32_t y);

	void (*on_mouse_move)    (window *w, int32_t x, int32_t y, uint32_t state);
	void (*on_mouse_press)   (window *w, int32_t x, int32_t y, uint32_t state);
	void (*on_mouse_release) (window *w, int32_t x, int32_t y, uint32_t state);
	void (*on_mouse_wheel)   (window *w, int32_t x, int32_t y, uint32_t state);
	void (*on_mouse_enter)   (window *w, int32_t x, int32_t y, uint32_t state);
	void (*on_mouse_leave)   (window *w, int32_t x, int32_t y, uint32_t state);

	void (*on_key_press)     (window *w, uint32_t keycode, uint32_t state);
	void (*on_key_release)   (window *w, uint32_t keycode, uint32_t state);
} window_cbs;

vinci*   vinci_new        (void);
void     vinci_destroy    (vinci *g);
void     vinci_idle       (vinci *g); // Non blocking: call it repeatedly with a timer
window*  window_new       (vinci *g, void* p, uint32_t width, uint32_t height, char visible, window_cbs *cbs);
void     window_free      (window *w);
void     window_draw      (window *w, unsigned char *data, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height);
void*    window_get_handle(window *w);
uint32_t window_get_width (window *w); // Internal: excluding title etc
uint32_t window_get_height(window *w); // ^
void     window_resize    (window *w, uint32_t width, uint32_t height);
void     window_move      (window *w, uint32_t x, uint32_t y);
void     window_show      (window *w);
void     window_hide      (window *w);
void     window_set_data  (window *w, void *data);
void*    window_get_data  (window *w);

#ifdef __cplusplus
}
#endif
#endif
