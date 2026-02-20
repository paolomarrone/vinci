/*
 * Vinci
 *
 * Copyright (C) 2022-2025 Orastron Srl unipersonale
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
 * File author: Paolo Marrone
 */

#include "vinci.h"

#include <stdint.h>
#include <string.h>
#import <Cocoa/Cocoa.h>

#if __has_feature(objc_arc)
#define _OBJC_RELEASE(obj) { obj = nil; }
#else
#define _OBJC_RELEASE(obj) { [obj release]; obj = nil; }
#endif

@interface VinciView : NSView
{
	NSTrackingArea *tracking;
	@public window *win;
}
@end

@interface VinciViewController : NSViewController
{
	@public window *win;
}
@property (nonatomic, strong) VinciView *vinciView;
@end

struct vinci {
	window            *windows;
	NSApplication     *app;
	char               standalone; // If any view has a parent, it is not standalone
};

struct window {
	vinci               *g;
	window              *next;
	NSWindow            *nswindow;
	VinciView           *view;
	VinciViewController *controller;
	unsigned char       *img;
	NSImage             *nsImage;
	void                *data;
	window_cbs           cbs;
	NSRect               default_frame;
	char                 owns_nswindow;
	char                 closing;
};

static uint32_t get_mouse_state(NSEvent* e) {
	(void) e;
	return (uint32_t) [NSEvent pressedMouseButtons];
}

@implementation VinciView

- (void) updateTrackingAreas {

	[self removeTrackingArea];

	tracking = [[NSTrackingArea alloc] initWithRect:[self bounds]
	                                        options:NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved
	                                          owner:self
	                                       userInfo:nil];
	[self addTrackingArea:tracking];
	[super updateTrackingAreas];
}

- (void) removeTrackingArea {
	if (tracking) {
		[super removeTrackingArea:tracking];
		_OBJC_RELEASE(tracking);
		tracking = 0;
	}
}

- (void) mouseDown : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_press)
		w->cbs.on_mouse_press(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) mouseUp : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_release)
		w->cbs.on_mouse_release(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) mouseMoved : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_move)
		w->cbs.on_mouse_move(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) rightMouseDown : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_press)
		w->cbs.on_mouse_press(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) rightMouseUp : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_release)
		w->cbs.on_mouse_release(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) rightMouseDragged : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_move)
		w->cbs.on_mouse_move(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) otherMouseDown : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_press)
		w->cbs.on_mouse_press(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) otherMouseUp : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_release)
		w->cbs.on_mouse_release(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) otherMouseDragged : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_move)
		w->cbs.on_mouse_move(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) mouseDragged : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_move)
		w->cbs.on_mouse_move(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) mouseEntered : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_enter)
		w->cbs.on_mouse_enter(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) mouseExited : (NSEvent *) e {
	window *w = self->win;
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	if (w->cbs.on_mouse_leave)
		w->cbs.on_mouse_leave(w, (int32_t)p.x, self.frame.size.height - (int32_t)p.y, get_mouse_state(e));
}

- (void) keyDown : (NSEvent *) e {
	window *w = self->win;
	if (w->cbs.on_key_press)
		w->cbs.on_key_press(w, (uint32_t)e.keyCode, (uint32_t)e.modifierFlags);
}

- (void) keyUp : (NSEvent *) e {
	window *w = self->win;
	if (w->cbs.on_key_release)
		w->cbs.on_key_release(w, (uint32_t)e.keyCode, (uint32_t)e.modifierFlags);
}

// custom
- (void) vinci_resized {
	window *w = self->win;
	NSSize size = [w->view bounds].size;
	unsigned char *new_img = (unsigned char*) realloc(w->img, (uint32_t) (size.width * size.height) * 4);
	if (new_img)
		w->img = new_img;
	if (w->cbs.on_window_resize)
		w->cbs.on_window_resize(w, size.width, size.height);
	[self setNeedsDisplay:YES];
}

- (void)removeFromSuperview {
	[super removeFromSuperview];
	window *w = self->win;
	if (w && !w->closing && w->cbs.on_window_close)
		w->cbs.on_window_close(w);
}

// Custom method for standalone application
- (void)windowWillClose:(NSNotification *)notification {
	(void)notification;
	window *w = self->win;
	if (w && !w->closing && w->cbs.on_window_close)
		w->cbs.on_window_close(w);
}

- (void) drawRect:(NSRect) r {
	[self->win->nsImage drawInRect:r fromRect:r operation:NSCompositingOperationCopy fraction:1.0];
}

@end

@implementation VinciViewController

- (void)loadView {
    self.vinciView = [[VinciView alloc] initWithFrame:self->win->default_frame];
    self.view = self.vinciView;
}

- (void)viewDidLayout {
	[super viewDidLayout];
	[self.vinciView vinci_resized];
}

@end

vinci* vinci_new(void) {
	vinci *g = (vinci*) malloc(sizeof(vinci));
	if (g == NULL)
		return NULL;
	g->windows = NULL;
	g->app = [NSApplication sharedApplication];
	//[g->app setActivationPolicy:NSApplicationActivationPolicyRegular]; // TODO: check if this is a problem when not standalone
	g->standalone = 1;
	return g;
}

void vinci_destroy(vinci *g) {
	while (g->windows)
		window_free(g->windows);
	free(g);
}

void vinci_idle(vinci *g) {
	(void) g;

	NSApplication *app = [NSApplication sharedApplication];
	NSDate *expiration = [NSDate now];

	while (true) {
		NSEvent *event = [app nextEventMatchingMask:NSEventMaskAny
		                                  untilDate:expiration
		                                     inMode:NSDefaultRunLoopMode
		                                    dequeue:YES];
		if (!event)
			break;
		[app sendEvent:event];
	}
}

window* window_new(vinci *g, void *parent, uint32_t width, uint32_t height, window_cbs *cbs) { 

	window *ret = (window*) malloc(sizeof(window));
	if (ret == NULL)
		return NULL;
	memset(ret, 0, sizeof(window));
  
	ret->default_frame = NSMakeRect(0, 0, width, height);

	VinciViewController *controller = [[VinciViewController alloc] init];
	controller->win = ret;
	VinciView *view = (VinciView*) controller.view;

	view->win = ret;

	ret->g = g;
	ret->view = view;
	ret->controller = controller;

	if (parent) {
		[((__bridge NSView*)parent) addSubview:view positioned:NSWindowAbove relativeTo:nil];
		ret->nswindow = [(__bridge NSView*)parent window];
		ret->owns_nswindow = 0;
		g->standalone = 0;
	}
	else {
		int style = NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskTitled | NSWindowStyleMaskMiniaturizable;
		ret->nswindow = [[NSWindow alloc] initWithContentRect:ret->default_frame 
		                                            styleMask:style
		                                              backing:NSBackingStoreBuffered 
		                                                defer:NO];
		[ret->nswindow setTitle:@"Vinci window"];
		[ret->nswindow setContentView:view];
		ret->owns_nswindow = 1;

		[[NSNotificationCenter defaultCenter] addObserver:view
		                                         selector:@selector(windowWillClose:)
		                                             name:NSWindowWillCloseNotification
		                                           object:ret->nswindow];
	}

	ret->img     = (unsigned char*) malloc(width * height * 4);
	if (!ret->img) {
		[[NSNotificationCenter defaultCenter] removeObserver:view
		                                                name:NSWindowWillCloseNotification
		                                              object:ret->nswindow];
		if (ret->owns_nswindow)
			_OBJC_RELEASE(ret->nswindow);
		_OBJC_RELEASE(controller);
		free(ret);
		return NULL;
	}
	ret->data    = NULL;
	ret->nsImage = NULL;
	ret->next    = NULL;
	if (cbs)
		ret->cbs = *cbs;
	else
		memset(&ret->cbs, 0, sizeof(window_cbs));

	_OBJC_RELEASE(view);

	if (g->windows == NULL)
		g->windows = ret;
	else {
		window *cur = g->windows;
		while (cur->next != NULL)
			cur = cur->next;
		cur->next = ret;
	}

	return ret;
}

void window_free(window *w) {
	w->closing = 1;

	if (w->g->windows == w) {
		w->g->windows = w->next;
	}
	else {
		window *cur = w->g->windows;
		while (cur != NULL && cur->next != w)
			cur = cur->next;
		if (cur == NULL)
			return;
		cur->next = w->next;
	}

	[[NSNotificationCenter defaultCenter] removeObserver:w->view
	                                                name:NSWindowWillCloseNotification
	                                              object:w->nswindow];
	[w->view removeTrackingArea];
	if ([w->view superview] != nil)
		[w->view removeFromSuperview];
	if (w->owns_nswindow && w->nswindow) {
		[w->nswindow setContentView:nil];
		[w->nswindow orderOut:nil];
	}
	_OBJC_RELEASE(w->nsImage);
	_OBJC_RELEASE(w->controller);
	if (w->owns_nswindow)
		_OBJC_RELEASE(w->nswindow);
	free(w->img);
	free(w);
}

void window_draw(window *w, unsigned char *img, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int32_t wx, int32_t wy, int32_t width, int32_t height) {
	if (w->img == NULL)
		return;

	NSSize size = [w->view bounds].size;
	const int32_t vw = (int32_t) size.width;
	const int32_t vh = (int32_t) size.height;

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

	uint32_t iw = (uint32_t)(wy * vw + wx);
	uint32_t o  = dy * dw + dx;
	uint32_t p1 = dw;
	uint32_t p2 = (uint32_t) vw;

	for (int32_t y = dy; y < dy + height && y < dh; y++) {
		memcpy(w->img + iw * 4, img + o * 4, width * 4);
		iw += p2;
		o  += p1;
	}

	CGDataProviderRef provider = CGDataProviderCreateWithData(
		NULL,                         // void *info
		w->img,                       // const void *data
		size.width * size.height * 4, // size_t size
		NULL                          // CGDataProviderReleaseDataCallback releaseData
	);

	CGColorSpaceRef colorSpaceRef = CGColorSpaceCreateDeviceRGB();
	CGBitmapInfo bitmapInfo = kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast;
	CGColorRenderingIntent renderingIntent = kCGRenderingIntentDefault;

	CGImageRef imageRef = CGImageCreate(
		size.width,      // size_t width
		size.height,     // size_t height 
		8,               // size_t bitsPerComponent
		4 * 8,           // size_t bitsPerPixel
		size.width * 4,  // size_t bytesPerRow
		colorSpaceRef,   // CGColorSpaceRef space
		bitmapInfo,      // CGBitmapInfo bitmapInfo
		provider,        // CGDataProviderRef provider
		NULL,            // const CGFloat *decode
		NO,              // bool shouldInterpolate
		renderingIntent  // CGColorRenderingIntent intent
	);

	NSImage* image = [[NSImage alloc] initWithCGImage: imageRef size:NSZeroSize];
	_OBJC_RELEASE(w->nsImage);
	w->nsImage = image;

	[w->view setNeedsDisplayInRect:NSMakeRect(wx, size.height - (wy + height), width, height)];

	CGDataProviderRelease(provider);
	CGColorSpaceRelease(colorSpaceRef);
	CGImageRelease(imageRef);
}

void window_resize (window *w, uint32_t width, uint32_t height) {
	NSSize newSize = NSMakeSize(width, height);
	[w->view setFrameSize:newSize]; // TODO: this sends NSViewFrameDidChangeNotification, we should handle that instead in 1 place
	unsigned char *new_img = (unsigned char*) realloc(w->img, width * height * 4);
	if (new_img)
		w->img = new_img;
	w->view.needsDisplay = YES;
	if (w->cbs.on_window_resize)
		w->cbs.on_window_resize(w, width, height); // view should be automatically informed
}

void window_move(window *w, uint32_t x, uint32_t y) {
	if ([w->view superview] != nil)
		[w->view setFrameOrigin:NSMakePoint(x, y)];
	else if (w->nswindow)
		[w->nswindow setFrameOrigin:NSMakePoint(x, y)];
}

void* window_get_handle(window *w) {
	return (__bridge void *)(w->view);
}

uint32_t window_get_width(window *w) {
	return [w->view bounds].size.width;
}
uint32_t window_get_height(window *w) {
	return [w->view bounds].size.height;
}

void window_show(window *w) {
	if ([w->view superview] != nil && !w->owns_nswindow) {
		[w->view setHidden:NO];
		return;
	}
	if (w->nswindow)
		[w->nswindow setIsVisible:YES];
}

void window_hide(window *w) {
	if ([w->view superview] != nil && !w->owns_nswindow) {
		[w->view setHidden:YES];
		return;
	}
	if (w->nswindow)
		[w->nswindow setIsVisible:NO];
}

void window_set_data(window *w, void *data) {
	w->data = data;
}

void *window_get_data(window *w) {
	return w->data;
}
