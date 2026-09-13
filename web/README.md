# Browser backend

`vinci-web.c` implements the unchanged `vinci.h` API. `vinci-web.js` is an ES module
that owns Canvas 2D surfaces and queues DOM events. Both run on the page thread.
There is no X11, WASI, Emscripten, audio or Perone dependency. An audio host can run
its DSP in an AudioWorklet and transport parameters separately; Vinci does not
create the worklet, share its memory or prescribe a transport.

## Build and demo

With Clang/LLD supporting `wasm32-unknown-unknown`:

```sh
make web
python3 -m http.server 8000 --directory build/web
```

Open `http://localhost:8000`. The demo creates two Vinci contexts in one Wasm
instance. Each has its own canvas, callbacks and data. Move/drag to paint, focus a
canvas and type to change its colour, or use Resize/Close/Add a view.

`WEB_CC`, `WEB_CFLAGS` and `BUILD_DIR` can be overridden. `make` continues to build
the desktop test. `test.c` contains a blocking desktop loop and is intentionally
not the browser entry point: export create/tick/destroy functions for the same UI
code instead of calling that loop on the page thread.

For your UI, compile `vinci-web.c` alongside your C/C++ sources and your
freestanding C runtime. The backend needs `calloc`, `free`, and compiler-generated
memory operations such as `memcpy`. The demo supplies these with `web/memory.c`
and the small `web/stdlib.h`; these are demo support, not a full libc or C++
standard library. Hosts with an existing allocator/runtime should supply their
own headers and implementation and omit these files.

For example, a C++ UI can be linked to the C backend as follows (adjust includes,
allocator/runtime and exported `ui_*` entry points for your application):

```sh
clang --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
  -I. -Iweb -c vinci-web.c -o build/vinci-web.o
clang --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
  -Iweb -c web/memory.c -o build/memory.o
clang++ --target=wasm32-unknown-unknown -std=c++11 -O2 -ffreestanding \
  -fno-exceptions -fno-rtti -I. -Iweb -c ui.cpp -o build/ui.o
clang --target=wasm32-unknown-unknown -nostdlib \
  build/vinci-web.o build/memory.o build/ui.o -o build/ui.wasm \
  -Wl,--no-entry,--export-memory,--export=__wasm_call_ctors \
  -Wl,--export=ui_create,--export=ui_destroy \
  -Wl,--export=vinci_idle,--export=vinci_destroy,--export=window_free \
  -Wl,--export=window_resize,--export=window_move
```

`ui_create`/`ui_destroy` are host-defined exports, declared `extern "C"` in C++.
A UI needing `new`, STL, exceptions or other library facilities must link an
appropriate runtime; the demo allocator alone does not provide these. The
backend also compiles directly as C++11. The Makefile exports the complete Vinci
API for the demo/tests; an application needs only its UI exports and the five
Vinci functions used by the adapter above, plus `memory`. Export and call
`__wasm_call_ctors` once if the UI has C++ static constructors. Keep application
initialization out of a Wasm start function: bind the adapter first.

## Host loading and lifetime

```js
import { VinciWeb } from './vinci-web.js';

const adapter = new VinciWeb(document.querySelector('#ui-container'));
const response = await fetch('./ui.wasm');
if (!response.ok) throw new Error(`UI load failed: ${response.status}`);
const { instance } = await WebAssembly.instantiate(
  await response.arrayBuffer(), adapter.imports);
adapter.bind(instance);
instance.exports.__wasm_call_ctors?.();

const parent = adapter.registerParent(document.querySelector('#plugin-slot'));
const ui = instance.exports.ui_create(parent); // passes (void *)(uintptr_t)parent to window_new
if (!ui) {
  adapter.dispose();
  throw new Error('UI creation failed');
}
adapter.start(); // requestAnimationFrame -> vinci_idle for each live context

// Before removing the UI/route:
adapter.stop();
instance.exports.ui_destroy(ui); // frees UI-owned data and calls vinci_destroy
adapter.unregisterParent(parent);
adapter.dispose();
```

The Wasm module exports its own memory. A host may merge its additional imports
with `adapter.imports`. Never use a shared global `Module`: construct and bind one
adapter per Wasm instance. You may reuse a compiled `WebAssembly.Module`, but each
instantiation gets its own adapter, memory, handles and contexts.

- `registerParent(element)` returns a nonzero wasm32 token for an HTML container,
  not a canvas or a C pointer. Pass it through the existing `window_new` parent
  argument. A null parent uses the adapter's root container. Invalid tokens make
  `window_new` return `NULL`.
- `window_get_handle(w)` returns a token in the same adapter, usable as another
  window's parent or with `getCanvas(handle)`, `close(handle)`, `resize(handle,w,h)`
  and `move(handle,x,y)`. The actual C `window *` is a different value. Handles
  are never reused and cannot be passed to another adapter.
- `idle()` ticks every live context; alternatively the host calls C `vinci_idle(g)`
  itself. `start()`/`stop()` are optional scheduling conveniences, not required
  runtime machinery. Do not run a blocking loop. Each idle call processes at most
  256 queued events then presents dirty canvases. Adjacent motion/resize events
  are coalesced. Callbacks must also return promptly.
- `close(handle)` queues one close request. `on_window_close` decides whether to
  call `window_free`; with no callback, Vinci frees the window. Explicit
  `window_free`/`vinci_destroy` never emit close callbacks.
- `window_free` removes the canvas and all child windows, listeners, pointer
  capture, ResizeObserver and pending events. `vinci_destroy` frees all windows
  in the context. Callbacks may free their window or destroy their context;
  recursive `vinci_idle` on the same context is ignored. Freed C pointers remain
  invalid; the application must clear its references, including child pointers.
- `unregisterParent` frees any windows mounted directly beneath that parent and
  their children. `dispose` is idempotent: it stops scheduling, destroys remaining
  contexts and releases registrations. Destroy application-owned UI objects
  first; the adapter cannot free their private data. Call cleanup before removing
  the containing DOM node; arbitrary external DOM removal is not a close request.

## Pixels, dimensions and input

The input to `window_draw` is a tightly packed `dw` by `dh` array of 32-bit pixels
`0x00RRGGBB` (bytes B, G, R, ignored on little-endian Wasm), as used by the desktop
example. Output is opaque RGBA. Source and destination rectangles are clipped
together, including negative coordinates. Data is copied during the call, so the
caller may immediately reuse/free its buffer. Only `vinci_idle` presents changes.
Wasm memory views are reacquired for every transfer and event, including after
`memory.grow`. The caller must supply a valid source allocation.

Widths/heights are integer canvas pixels; default CSS size equals backing size.
Resize clears to opaque black and queues `on_window_resize` for redraw. The C
getters update immediately on a successful C/host resize; CSS changes are observed
and applied at the next idle. Zero sizes are valid; dimensions above 16384 or
surfaces above 16777216 pixels are rejected, preserving the previous size.
Creation failures return `NULL`; the existing void resize API reports failure by
leaving dimensions unchanged. Place borders/padding on the wrapper/container,
not on the canvas. CSS transforms/scaling are mapped back to canvas coordinates.
There is no implicit `devicePixelRatio` multiplier; the host controls resolution.

Mouse coordinates use a top-left origin; captured dragging can report coordinates
outside the canvas. Only the primary pointer is used (mouse, pen or first touch).
`state` for mouse callbacks is the DOM `buttons` bitmask: left=1, right=2,
middle=4, back=8, forward=16. Chorded buttons are reported. Cancellation releases
pressed buttons; blur/hiding also releases held keys. Context menus and handled
input defaults are suppressed on the canvas, without handlers that intercept
keyboard input elsewhere on the page.

For wheel callbacks, the existing `state` field carries signed vertical movement
in rounded CSS pixels, encoded as `uint32_t`; cast to `int32_t`. Up is positive.
Line/page wheel deltas are converted with 16 pixels/line or the canvas height.
The API has no separate horizontal wheel delta or text/composition callback.

Keyboard callbacks use physical USB HID usages derived from `KeyboardEvent.code`:
A–Z=4–29, 1–0=30–39, Enter=40, Escape=41, Backspace=42, Tab=43, Space=44,
F1–F12=58–69, Right/Left/Down/Up=79/80/81/82, left modifiers=224–227,
right modifiers=228–231; punctuation/navigation/numpad mappings are in the adapter.
Unknown codes are ignored. Modifier `state` bits are Shift=1, Ctrl=2, Alt=4, Meta=8.
Repeats emit further press callbacks. Focus a canvas by clicking it or calling
`getCanvas(handle).focus()`. As with native platform key codes, the application
must translate these codes if it needs a common cross-platform key abstraction.

## Private JS/Wasm ABI

All parameters/results are wasm32 `i32`; void imports have no result. Imports live
under `vinci_web`. `g`/`w` are C pointers, `id`/`parent` are adapter tokens.

| Import | Result / purpose |
| --- | --- |
| `init(g)` / `destroy(g)` | `i32` success / void: register or retire a context |
| `create(w,g,parent,width,height,visible)` | `i32` window token, zero on failure |
| `remove(id)` | Dispose window subtree |
| `resize(id,width,height)` | `i32` success, no notification of its own |
| `move(id,x,y)` / `show(id,visible)` | Update placement/visibility |
| `draw(id,data,dx,dy,dw,wx,wy,width,height)` | Copy an already clipped pixel rectangle |
| `notify(id,type,x,y,state)` | Queue an event |
| `pending(g)` | `i32` bounded count for this tick |
| `poll(g,event)` | `i32` success; write five `i32`: `[w,type,x,y,state]` |
| `present(g)` | Present dirty windows of this context |

Event types: resize=1, close=2, move=3, mouse move/press/release/wheel/enter/leave=4–9,
key press/release=10–11 (key usage in `x`). These imports are private glue: hosts
use the adapter methods and the UI's exports, not the event queue or C structures.
Only Vinci's import namespace is needed by the shipped Wasm binaries.

## Tests

Install Playwright in a separate directory if it is not already available:

```sh
npm install --prefix /tmp/vinci-browser-tests playwright
PLAYWRIGHT_MODULE=/tmp/vinci-browser-tests/node_modules/playwright/index.mjs \
  CHROMIUM=/usr/bin/chromium make test-web
```

Alternatively install Playwright and its Chromium normally, then run
`make test-web`. The runner starts/stops its own local HTTP server and browser.
It checks actual canvas pixels, clipping/stride/colour order, memory growth,
real pointer/keyboard/wheel input, resize, focus loss, multiple Wasm modules and
contexts, nested windows, callback-driven destruction, address reuse, bounded
idle dispatch and teardown. It also rejects unexpected imports (including WASI).

Browser API references: [Pointer events](https://developer.mozilla.org/en-US/docs/Web/API/PointerEvent),
[ResizeObserver](https://developer.mozilla.org/en-US/docs/Web/API/ResizeObserver),
[Wasm memory growth](https://developer.mozilla.org/en-US/docs/WebAssembly/Reference/JavaScript_interface/Memory/grow).
