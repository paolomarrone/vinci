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
 * File author: Paolo Marrone, Stefano D'Angelo
 */

#include "vinci.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdio.h>

struct window {
	vinci            *g;
	window           *next;
	HWND              handle;
	HBITMAP           bitmap;
	BITMAPINFOHEADER  bitmap_info;
	uint8_t          *bgra;
	char              mouse_tracking;
	void             *data;
	window_cbs        cbs;
};

struct vinci {
	window *windows;
	char    className[66];
};

static uint32_t get_mouse_state(WPARAM wParam) {
	return (wParam & (MK_LBUTTON | MK_RBUTTON)) | ((wParam & MK_MBUTTON) >> 2);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	window *w = (window*) GetWindowLongPtrA(hwnd, 0);
	if (w == NULL)
		return DefWindowProc(hwnd, msg, wParam, lParam);

	POINTS points;
	switch(msg)
	{
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		if (w->cbs.on_mouse_press) {
			points = MAKEPOINTS(lParam);
			SetCapture(hwnd);
			w->cbs.on_mouse_press(w, points.x, points.y, get_mouse_state(wParam));
		}
			break;
		case WM_LBUTTONUP:
		case WM_MBUTTONUP:
		case WM_RBUTTONUP:
			if (w->cbs.on_mouse_release) {
				points = MAKEPOINTS(lParam);
				if (get_mouse_state(wParam) == 0) {
					ReleaseCapture();
				}
				w->cbs.on_mouse_release(w, points.x, points.y, get_mouse_state(wParam));
			}
			break;
		case WM_MOUSEMOVE:
			points = MAKEPOINTS(lParam);
			if (!w->mouse_tracking) {
				if (w->cbs.on_mouse_enter)
					w->cbs.on_mouse_enter(w, points.x, points.y, 1);
				TRACKMOUSEEVENT tme;
				tme.cbSize = sizeof(tme);
				tme.hwndTrack = hwnd;
				tme.dwFlags = TME_HOVER | TME_LEAVE;
				tme.dwHoverTime = HOVER_DEFAULT;
				TrackMouseEvent(&tme);
				w->mouse_tracking = 1;
			}
			if (w->cbs.on_mouse_move)
				w->cbs.on_mouse_move(w, points.x, points.y, get_mouse_state(wParam));
			break;
		case WM_MOUSELEAVE:
		   	w->mouse_tracking = 0;
		   	if (w->cbs.on_mouse_leave) {
				POINT p;
				GetCursorPos(&p);
				ScreenToClient(hwnd, &p);
			   	w->cbs.on_mouse_leave(w, p.x, p.y, get_mouse_state(wParam));
			}
			break;
		case WM_KEYDOWN:
			if (w->cbs.on_key_press)
				w->cbs.on_key_press(w, wParam, 1);
			break;
		case WM_KEYUP:
			if (w->cbs.on_key_release)
				w->cbs.on_key_release(w, wParam, 1);
			break;
		case WM_SIZE:
		{
			const uint32_t width = LOWORD(lParam);
			const uint32_t height = HIWORD(lParam);
			DeleteObject(w->bitmap);
			w->bitmap_info.biWidth = width;
			w->bitmap_info.biHeight = -height;
			HDC dc = GetDC(w->handle);
			w->bitmap = CreateDIBSection(dc, (BITMAPINFO*)&w->bitmap_info, DIB_RGB_COLORS, (void **)&w->bgra, NULL, 0);
			ReleaseDC(w->handle, dc);
			if (w->bitmap == NULL || w->bgra == NULL)
				break;
			if (w->cbs.on_window_resize)
				w->cbs.on_window_resize(w, width, height);
		}
			break;
		case WM_PAINT:
		{
			RECT cr;
			GetClientRect(w->handle, &cr);
			RECT r;
			if (GetUpdateRect(hwnd, &r, 0)) {
				PAINTSTRUCT ps;
				HDC dc = BeginPaint(hwnd, &ps);
				SetDIBitsToDevice(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, r.left, cr.bottom - cr.top - r.bottom, 0, -w->bitmap_info.biHeight, w->bgra, (BITMAPINFO*)&w->bitmap_info, DIB_RGB_COLORS);
				EndPaint(hwnd, &ps);
			}
		}
			break;
		case WM_DESTROY:
		{
			SetWindowLongPtrA(hwnd, 0, 0);
			if (w->cbs.on_window_close)
				w->cbs.on_window_close(w);
			return 0;
		}
			break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

vinci* vinci_new(void) {
	vinci *g = (vinci*)malloc(sizeof(vinci));
	if (!g)
		return NULL;
	snprintf(g->className, 66, "vc%p", (void*)g); // Nice random name

	WNDCLASSEX wc;
	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = 0;
	wc.lpfnWndProc   = WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = sizeof (window*); // To store window ptr into hwnd
	wc.hInstance     = NULL;
	wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(WHITE_BRUSH);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = g->className;
	wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
	if (!RegisterClassEx(&wc)) {
		free(g);
		return NULL;
	}
	g->windows = NULL;
	return g;
}

void vinci_destroy(vinci *g) {
	while (g->windows)
		window_free(g->windows);
	UnregisterClass(g->className, NULL);
	free(g);
}

void vinci_idle(vinci *g) {
	(void) g;
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

window* window_new(vinci *g, void* parent, uint32_t width, uint32_t height, window_cbs *cbs) {
	window *w = (window*)malloc(sizeof(window));
	if (w == NULL)
		return NULL;
	memset((void*) w, 0, sizeof(window));

	w->g = g;
	RECT wr = { 0, 0, (LONG)width, (LONG)height };
	AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
	w->handle = CreateWindowEx(0, g->className, NULL, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, NULL, NULL);
	if (w->handle == NULL) {
		free(w);
		return NULL;
	}
	SetWindowLongPtrW(w->handle, 0, (LONG_PTR) w);
	ZeroMemory(&w->bitmap_info, sizeof(BITMAPINFOHEADER));
	w->bitmap_info.biSize = sizeof(BITMAPINFOHEADER);
	w->bitmap_info.biWidth = width;
	w->bitmap_info.biHeight = -height;
	w->bitmap_info.biPlanes = 1;
	w->bitmap_info.biBitCount = 32;
	w->bitmap_info.biCompression = BI_RGB;
	HDC dc = GetDC(w->handle);
	w->bitmap = CreateDIBSection(dc, (BITMAPINFO*)&w->bitmap_info, DIB_RGB_COLORS, (void **)&w->bgra, NULL, 0);
	ReleaseDC(w->handle, dc);
	if (w->bitmap == NULL) {
		DestroyWindow(w->handle);
		free(w);
		return NULL;
	}
	
	if (parent) {
		SetParent(w->handle, (HWND)parent);
		SetWindowLong(w->handle, GWL_STYLE, GetWindowLong(w->handle, GWL_STYLE) & ~(WS_BORDER | WS_SIZEBOX | WS_DLGFRAME));
		SetWindowPos(w->handle, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED); // SWP_FRAMECHANGED triggers WndProc call with WM_SIZE right here. May they die hard
	}
	
	w->next = NULL;
	if (g->windows == NULL)
		g->windows = w;
	else {
		window *n = g->windows;
		while (n->next)
			n = n->next;
		n->next = w;
	}
	
	TRACKMOUSEEVENT tme;
	tme.cbSize = sizeof(TRACKMOUSEEVENT);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = w->handle;
	TrackMouseEvent(&tme);
	w->mouse_tracking = 1;

	w->data = NULL;
	if (cbs)
		w->cbs = *cbs;
	else
		memset(&w->cbs, 0, sizeof(window_cbs));
	UpdateWindow(w->handle);
	
	return w;
}

void window_free(window *w) {
	if (w->g->windows == w)
		w->g->windows = w->next;
	else {
		window *n = w->g->windows;
		while (n && n->next != w)
			n = n->next;
		if (!n)
			return;
		n->next = w->next;
	}
	DeleteObject(w->bitmap);
	if (GetWindowLongPtrA(w->handle, 0) == (LONG_PTR)w) {
		SetWindowLongPtrA(w->handle, 0, 0);
		DestroyWindow(w->handle);
	}
	free(w);
}

static inline int mini(int a, int b) {
	return a < b ? a : b;
}

static inline int maxi(int a, int b) {
	return a > b ? a : b;
}

void window_draw(window *w, unsigned char *data, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height) {	
	if (w->bgra == NULL)
		return;

	RECT cr;
	GetClientRect(w->handle, &cr);

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

	const int wx_ = maxi(wx, cr.left);
	const int wy_ = maxi(wy, cr.top);
	const int width_  = mini(width  - (wx_ - wx), cr.right - wx_);
	const int height_ = mini(height - (wy_ - wy), cr.bottom - wy_);
	if (width_ <= 0 || height_ <= 0 )
		return;
	dx = dx + (wx_ - wx);
	dy = dy + (wy_ - wy);
	wx = wx_;
	wy = wy_;
	width = width_;
	height = height_;
	if (dx >= dw || dy >= dh)
		return;
	if (width > dw - dx)
		width = dw - dx;
	if (height > dh - dy)
		height = dh - dy;
	if (width <= 0 || height <= 0)
		return;

	uint32_t *src = ((uint32_t *)data) + dw * dy + dx;
	uint32_t *dest = ((uint32_t *)w->bgra) + w->bitmap_info.biWidth * wy + wx;
	for (uint32_t h = height; h; h--, src += dw, dest += w->bitmap_info.biWidth) {
		//memcpy(dest, src, 4 * width);
		for (int32_t x = 0; x < width; x++) // TODO: BIG ENDIAN AND LITTLEENDIAN IFDEF
			dest[x] = ((src[x] >> 16) & 0x00000000ff) | ((src[x]) & 0x0000ff00) | ((src[x] << 16) & 0x00ff0000);
	}
	
	RECT r = {
		wx,         // left
		wy,         // top
		wx + width, // right
		wy + height // bottom
	};

	RedrawWindow(w->handle, &r, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

void window_resize(window *w, uint32_t width, uint32_t height) {
	RECT wr = { 0, 0, (LONG)width, (LONG)height };
	const DWORD style = (DWORD) GetWindowLongPtr(w->handle, GWL_STYLE);
	const DWORD exstyle = (DWORD) GetWindowLongPtr(w->handle, GWL_EXSTYLE);
	AdjustWindowRectEx(&wr, style, FALSE, exstyle);
	SetWindowPos(w->handle, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
}

void window_move(window *w, uint32_t x, uint32_t y) {
	SetWindowPos(w->handle, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

uint32_t window_get_width(window *w) {
	RECT r;
	GetClientRect(w->handle, &r);
	return r.right - r.left;
}

uint32_t window_get_height(window *w) {
	RECT r;
	GetClientRect(w->handle, &r);
	return r.bottom - r.top;
}

void *window_get_handle(window *w) {
	return w->handle;
}

void window_show(window *w) {
	ShowWindow(w->handle, SW_SHOW);
}

void window_hide(window *w) {
	ShowWindow(w->handle, SW_HIDE);
}

void window_set_data(window *w, void *data) {
	w->data = data;
}

void *window_get_data(window *w) {
	return w->data;
}
