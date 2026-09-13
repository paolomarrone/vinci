/*
 * Vinci
 *
 * Copyright (C) 2022-2026 Orastron Srl unipersonale
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
#import <Cocoa/Cocoa.h>

#if __has_feature(objc_arc)
#define _OBJC_RELEASE(obj) { obj = nil; }
#else
#define _OBJC_RELEASE(obj) { [obj release]; obj = nil; }
#endif

struct vinci {
	window *windows;
};

vinci * vinci_new(void) {
	vinci * v = (vinci *)malloc(sizeof(vinci));
	if (v)
		v->windows = NULL;
	return v;
}

void vinci_destroy(vinci * g) {
	while (g->windows)
		window_free(g->windows);
	free(g);
}

void vinci_idle(vinci * g) {
	(void) g;

	NSApplication *app = [NSApplication sharedApplication];
	NSDate *expiration = [NSDate now];

	while (true) {
		@autoreleasepool{
		NSEvent *event = [app nextEventMatchingMask:NSEventMaskAny untilDate:expiration inMode:NSDefaultRunLoopMode dequeue:YES];
		if (!event)
			break;
		[app sendEvent:event];
		}
	}
}

@interface VinciView : NSView
- (void)invalidate;
@end

@implementation VinciView {
	void *				handle;
	window_cbs		cbs;
	uint8_t *			fb;
	NSTrackingArea *	trackingArea;
}

- (instancetype) init:(CGRect)frame handle:(void *)h callbacks:(window_cbs *)callbacks {
	self = [super initWithFrame:frame];
	if (!self)
		return nil;
	handle = h;
	cbs = *callbacks;

	fb = (uint8_t *)calloc(frame.size.width * frame.size.height, 4); // handling NULL, no worries
	[self initFb];

	return self;
}

- (void)dealloc {
	[self removeTrackingArea];
	if (fb)
		free(fb);
#if !__has_feature(objc_arc)
	[super dealloc];
#endif
}

- (void)invalidate {
	memset(&cbs, 0, sizeof(cbs));
	handle = NULL;
}

- (void)removeFromSuperview {
	[super removeFromSuperview];
	if (cbs.on_window_close)
		cbs.on_window_close(handle);
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];

	size_t s = newSize.width * newSize.height * 4;
	uint8_t * newFb = s ? (uint8_t *)realloc(fb, s) : NULL;
	if (newFb) {
		memset(newFb, 0, s);
		fb = newFb;
	} else if (fb) {
		free(fb);
		fb = NULL;
	}
	[self initFb];

	if (cbs.on_window_resize)
		cbs.on_window_resize(handle, newSize.width, newSize.height);
}

- (void)setFrameOrigin:(NSPoint)newOrigin {
    [super setFrameOrigin:newOrigin];

	if (cbs.on_window_move)
		cbs.on_window_move(handle, newOrigin.x, newOrigin.y);
}

// TODO: this could be optimized by keeping a CGImage around repainting the dirty region etc.
- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    if (!fb)
		return;

	size_t fbWidth = self.frame.size.width;
	size_t fbHeight = self.frame.size.height;

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];

    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, fb, fbWidth * fbHeight * 4, NULL);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGImageRef fullImage = CGImageCreate(
							fbWidth, fbHeight,
							8, 32, fbWidth * 4,
							colorSpace,
							kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
							provider, NULL, false, kCGRenderingIntentDefault);

    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);

    if (fullImage) {
		CGContextDrawImage(ctx, NSRectToCGRect(self.bounds), fullImage);
		CGImageRelease(fullImage);
	}
}

- (void)initFb {
	if (!fb)
		return;

	size_t w = self.frame.size.width;
	size_t h = self.frame.size.height;
	for (size_t x = 0; x < w; x++)
		for (size_t y = 0; y < h; y++) {
			int i = 4 * (w * y + x);
			fb[i] = 0;
			fb[i + 1] = 0;
			fb[i + 2] = 0;
			fb[i + 3] = 0xff;
		}
}

- (void)draw:(unsigned char *)data dx:(int32_t)dx dy:(int32_t)dy dw:(int32_t)dw dh:(int32_t)dh wx:(int32_t)wx wy:(int32_t)wy width:(int32_t)width height:(int32_t)height {
	if (!fb)
		return;

	NSSize size = [self bounds].size;
	const int32_t vw = (int32_t)size.width;
	const int32_t vh = (int32_t)size.height;

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
	if (wx < 0) {
		width += wx;
		dx -= wx;
		wx = 0;
	}
	if (wy < 0) {
		height += wy;
		dy -= wy;
		wy = 0;
	}
	if (dx >= dw || dy >= dh || wx >= vw || wy >= vh)
		return;
	if (width > dw - dx)
		width = dw - dx;
	if (height > dh - dy)
		height = dh - dy;
	if (width > vw - wx)
		width = vw - wx;
	if (height > vh - wy)
		height = vh - wy;
	if (width <= 0 || height <= 0)
		return;

	for (int32_t y = 0; y < height; y++)
		for (int32_t x = 0; x < width; x++) {
			int32_t fbi = 4 * ((int32_t)size.width * (wy + y) + wx + x);
			int32_t datai = 4 * (dw * (dy + y) + dx + x);
			fb[fbi] = data[datai + 2];
			fb[fbi + 1] = data[datai + 1];
			fb[fbi + 2] = data[datai];
			fb[fbi + 3] = 0xff;
		}

	[self setNeedsDisplay:YES];
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];

    [self removeTrackingArea];
	trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
						options:NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved
	                    owner:self userInfo:nil];
	[self addTrackingArea:trackingArea];
}

- (void) removeTrackingArea {
	if (trackingArea) {
		[self removeTrackingArea:trackingArea];
		_OBJC_RELEASE(trackingArea);
	}
}

- (void)mouseEntered:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_enter)
		cbs.on_mouse_enter(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)mouseExited:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_leave)
		cbs.on_mouse_leave(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_move)
		cbs.on_mouse_move(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)mouseDragged:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_move)
		cbs.on_mouse_move(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)mouseDown:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_press)
		cbs.on_mouse_press(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)mouseUp:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_release)
		cbs.on_mouse_release(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)rightMouseMoved:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_move)
		cbs.on_mouse_move(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)rightMouseDragged:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_move)
		cbs.on_mouse_move(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)rightMouseDown:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_press)
		cbs.on_mouse_press(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)rightMouseUp:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_release)
		cbs.on_mouse_release(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)otherMouseMoved:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_move)
		cbs.on_mouse_move(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)otherMouseDragged:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_move)
		cbs.on_mouse_move(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)otherMouseDown:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_press)
		cbs.on_mouse_press(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)otherMouseUp:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (cbs.on_mouse_release)
		cbs.on_mouse_release(handle, p.x, self.frame.size.height - p.y, [NSEvent pressedMouseButtons]);
}

- (void)keyDown:(NSEvent *)event {
	if (cbs.on_key_press)
		cbs.on_key_press(handle, (uint32_t)event.keyCode, (uint32_t)event.modifierFlags);
}

- (void)keyUp:(NSEvent *)event {
	if (cbs.on_key_release)
		cbs.on_key_release(handle, (uint32_t)event.keyCode, (uint32_t)event.modifierFlags);
}

@end

struct window {
	vinci *		g;
	window *	next;
	VinciView *	view;
	NSWindow *	win;
	window_cbs	cbs;
	void *		data;
	id			will_close_token;
	id			will_move_token;
};

window * window_new(vinci * g, void * parent, uint32_t width, uint32_t height, char visible, window_cbs * cbs) {
	window * w = (window *)calloc(1, sizeof(window));
	if (w == NULL)
		goto err_w;

	w->g = g;
	if (cbs)
		w->cbs = *cbs;
	w->data = NULL;

	w->view = [[VinciView alloc] init:NSMakeRect(0, 0, width, height) handle:w callbacks:&w->cbs];
	if (!w->view)
		goto err_view;

	if (parent) {
		w->win = nil;
		[((__bridge NSView *)parent) addSubview:w->view];
	} else {
		w->win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
					styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
					backing:NSBackingStoreBuffered
					defer:NO];
		if (!w->win)
			goto err_window;
#if !__has_feature(objc_arc)
		[w->win setReleasedWhenClosed:NO];
#endif
		w->win.contentView = w->view;
		w->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		w->will_close_token = [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillCloseNotification object:w->win queue:nil usingBlock:^(NSNotification *note) {
			(void)note;
			if (w->cbs.on_window_close)
				w->cbs.on_window_close(w);
		}];
		w->will_move_token = [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidMoveNotification object:w->win queue:nil usingBlock:^(NSNotification *note) {
			(void)note;
			if (w->cbs.on_window_move)
				w->cbs.on_window_move(w, w->win.frame.origin.x, w->win.frame.origin.y);
		}];
	}

	w->next = g->windows;
	g->windows = w;

	if (visible)
		window_show(w);
	else
		window_hide(w);

	return w;

err_window:
	[w->view invalidate];
	_OBJC_RELEASE(w->view);
err_view:
	free(w);
err_w:
	return NULL;
}

void window_free(window * w) {
	window **link = &w->g->windows;
	while (*link && *link != w)
		link = &(*link)->next;
	if (!*link)
		return;
	*link = w->next;

	if (w->win) {
		[[NSNotificationCenter defaultCenter] removeObserver:w->will_close_token];
		[[NSNotificationCenter defaultCenter] removeObserver:w->will_move_token];
	}
	[w->view invalidate];
	[w->view removeTrackingArea];
	[w->view removeFromSuperview];
	if (w->win) {
		[w->win setContentView:nil];
		[w->win orderOut:nil];
	}
	_OBJC_RELEASE(w->view);
	_OBJC_RELEASE(w->win);
#if __has_feature(objc_arc)
	w->will_close_token = nil;
	w->will_move_token = nil;
#endif
	free(w);
}

void window_draw(window * w, unsigned char * data, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height) {
	[w->view draw:data dx:dx dy:dy dw:dw dh:dh wx:wx wy:wy width:width height:height];
}

void * window_get_handle(window * w) {
	return (__bridge void *)w->view;
}

uint32_t window_get_width(window * w) {
	return w->view.frame.size.width;
}

uint32_t window_get_height(window * w) {
	return w->view.frame.size.height;
}

void window_resize(window * w, uint32_t width, uint32_t height) {
	if (w->win) {
		NSRect frame = w->win.frame;
		frame.size = NSMakeSize(width, height);
		[w->win setFrame:frame display:YES];
	} else
		[w->view setFrameSize:NSMakeSize(width, height)];
}

void window_move(window * w, uint32_t x, uint32_t y) {
	if (w->win)
		[w->win setFrameOrigin:NSMakePoint(x, y)];
	else
		[w->view setFrameOrigin:NSMakePoint(x, y)];
}

void window_show(window * w) {
	if (w->win)
		[w->win makeKeyAndOrderFront:nil];
	else
		w->view.hidden = NO;
}

void window_hide(window * w) {
	if (w->win)
		[w->win orderOut:nil];
	else
		w->view.hidden = YES;
}

void window_set_data(window * w, void * data) {
	w->data = data;
}

void * window_get_data(window * w) {
	return w->data;
}
