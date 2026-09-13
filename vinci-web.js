/* Vinci - GPL-3.0-or-later. Copyright (C) 2026 Orastron Srl unipersonale. */

const RESIZE = 1, CLOSE = 2, MOVE = 3, MOUSE_MOVE = 4, MOUSE_PRESS = 5,
	MOUSE_RELEASE = 6, MOUSE_WHEEL = 7, MOUSE_ENTER = 8, MOUSE_LEAVE = 9,
	KEY_PRESS = 10, KEY_RELEASE = 11;

// USB HID keyboard usages: stable physical keys, independent of keyboard layout.
const keys = {
	Enter: 40, Escape: 41, Backspace: 42, Tab: 43, Space: 44, Minus: 45,
	Equal: 46, BracketLeft: 47, BracketRight: 48, Backslash: 49, Semicolon: 51,
	Quote: 52, Backquote: 53, Comma: 54, Period: 55, Slash: 56, CapsLock: 57,
	PrintScreen: 70, ScrollLock: 71, Pause: 72, Insert: 73, Home: 74, PageUp: 75,
	Delete: 76, End: 77, PageDown: 78, ArrowRight: 79, ArrowLeft: 80,
	ArrowDown: 81, ArrowUp: 82, NumLock: 83, NumpadDivide: 84, NumpadMultiply: 85,
	NumpadSubtract: 86, NumpadAdd: 87, NumpadEnter: 88, NumpadDecimal: 99,
	IntlBackslash: 100, ContextMenu: 101, ControlLeft: 224, ShiftLeft: 225,
	AltLeft: 226, MetaLeft: 227, ControlRight: 228, ShiftRight: 229,
	AltRight: 230, MetaRight: 231
};
for (let i = 0; i < 26; i++) keys[`Key${String.fromCharCode(65 + i)}`] = 4 + i;
for (let i = 1; i <= 10; i++) {
	keys[`Digit${i % 10}`] = 29 + i;
	keys[`Numpad${i % 10}`] = 88 + i;
}
for (let i = 1; i <= 12; i++) keys[`F${i}`] = 57 + i;
const modifiers = e => (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) |
	(e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0);

export class VinciWeb {
	constructor(root = document.body) {
		if (!(root instanceof HTMLElement) || root instanceof HTMLCanvasElement)
			throw new TypeError('Vinci needs an HTML container');
		this.root = root;
		this.contexts = new Map();
		this.windows = new Map();
		this.parents = new Map();
		this.nextId = 1;
		this.wasm = null;
		this.disposed = false;
		this.frame = null;
		this.imports = { vinci_web: {
			init: g => {
				if (this.disposed || !this.wasm) return 0;
				this.contexts.set(g >>> 0, []);
				return 1;
			},
			destroy: g => this.contexts.delete(g >>> 0),
			create: (w, g, p, width, height, visible) => this.create(w >>> 0, g >>> 0, p >>> 0, width, height, visible),
			remove: id => this.remove(id >>> 0),
			resize: (id, width, height) => this.setSize(this.windows.get(id >>> 0), width, height),
			move: (id, x, y) => {
				const r = this.windows.get(id >>> 0);
				r.element.style.left = `${x >>> 0}px`;
				r.element.style.top = `${y >>> 0}px`;
			},
			show: (id, visible) => {
				const r = this.windows.get(id >>> 0);
				if (!visible)
					for (const child of this.windows.values())
						if (r.element.contains(child.canvas)) this.releaseInput(child);
				r.element.hidden = !visible;
				r.element.style.display = visible ? 'inline-block' : 'none';
				r.dirty = true;
			},
			draw: (...args) => this.draw(...args),
			notify: (id, type, x, y, state) => this.enqueue(id >>> 0, type, x, y, state),
			pending: g => Math.min(this.contexts.get(g >>> 0)?.length || 0, 256),
			poll: (g, ptr) => {
				const queue = this.contexts.get(g >>> 0);
				while (queue?.length) {
					const [id, ...event] = queue.shift();
					const r = this.windows.get(id);
					if (!r) continue;
					if (event[0] === CLOSE) r.closing = false;
					new Int32Array(this.wasm.memory.buffer, ptr >>> 0, 5).set([r.pointer, ...event]);
					return 1;
				}
				return 0;
			},
			present: g => {
				for (const r of this.windows.values()) {
					if (r.context !== (g >>> 0) || !r.dirty || r.element.hidden || !r.image) continue;
					r.ctx.putImageData(r.image, 0, 0);
					r.dirty = false;
				}
			}
		} };
	}

	bind(instance) {
		if (this.wasm || this.disposed) throw new Error('Vinci adapter already bound or disposed');
		const wasm = instance.exports;
		if (!(wasm.memory instanceof WebAssembly.Memory)) throw new Error('Export Wasm memory');
		for (const name of ['vinci_idle', 'vinci_destroy', 'window_free', 'window_resize', 'window_move'])
			if (typeof wasm[name] !== 'function') throw new Error(`Export ${name}`);
		this.wasm = wasm;
		return this;
	}

	registerParent(element) {
		if (this.disposed || !(element instanceof HTMLElement) || element instanceof HTMLCanvasElement)
			throw new TypeError('Register an HTML container on a live adapter');
		const id = this.id();
		this.parents.set(id, element);
		return id;
	}

	unregisterParent(id) {
		if (!this.parents.has(id)) return;
		for (const r of [...this.windows.values()])
			if (r.parent === id && this.windows.has(r.id)) this.wasm.window_free(r.pointer);
		this.parents.delete(id);
	}

	id() {
		if (this.nextId > 0xffffffff) throw new Error('Vinci handle space exhausted');
		return this.nextId++;
	}

	create(pointer, context, parent, width, height, visible) {
		const container = parent ? this.parents.get(parent) || this.windows.get(parent)?.element : this.root;
		if (!container || !this.contexts.has(context)) return 0;
		let r;
		try {
			const element = document.createElement('div');
			const canvas = document.createElement('canvas');
			element.style.cssText = 'position:relative;display:inline-block;vertical-align:top';
			element.hidden = !visible;
			// The hidden attribute must win over the inline display declaration.
			element.style.display = visible ? 'inline-block' : 'none';
			canvas.style.cssText = 'display:block;touch-action:none';
			canvas.tabIndex = 0;
			canvas.setAttribute('aria-label', 'Vinci window');
			r = { id: this.id(), pointer, context, parent, element, canvas,
				ctx: canvas.getContext('2d'), controller: new AbortController(),
				keys: new Map(), pointerId: null, buttons: 0, x: 0, y: 0 };
			if (!r.ctx || !this.setSize(r, width, height)) return 0;
			element.append(canvas);
			container.append(element);
			this.windows.set(r.id, r);
			this.listen(r);
			r.observer = new ResizeObserver(entries => {
				if (!this.windows.has(r.id) || !r.canvas.getClientRects().length) return;
				const { width, height } = entries[0].contentRect;
				const w = Math.round(width), h = Math.round(height);
				if (w !== r.width || h !== r.height) this.enqueue(r.id, RESIZE, w, h, 0);
			});
			r.observer.observe(canvas);
			return r.id;
		} catch {
			if (r) this.remove(r.id);
			return 0;
		}
	}

	setSize(r, width, height) {
		// Bound allocation and canvas dimensions; failed resizes leave the old image intact.
		if (!r || width < 0 || height < 0 || width > 16384 || height > 16384 || width * height > 16777216) return 0;
		try {
			const image = width && height ? r.ctx.createImageData(width, height) : null;
			if (image) for (let i = 3; i < image.data.length; i += 4) image.data[i] = 255;
			r.canvas.width = width;
			r.canvas.height = height;
			r.canvas.style.width = `${width}px`;
			r.canvas.style.height = `${height}px`;
			r.width = width;
			r.height = height;
			r.image = image;
			r.dirty = true;
			return 1;
		} catch { return 0; }
	}

	draw(id, pointer, dx, dy, dw, wx, wy, width, height) {
		const r = this.windows.get(id >>> 0);
		if (!r?.image) return;
		// Do not retain Wasm views: memory.grow detaches the previous buffer.
		const source = new Uint8Array(this.wasm.memory.buffer);
		const start = (pointer >>> 0) + 4 * (dy * dw + dx);
		const end = start + 4 * ((height - 1) * dw + width);
		if (end > source.length) return;
		const target = r.image.data;
		for (let y = 0; y < height; y++) {
			let src = start + 4 * y * dw, dst = 4 * ((wy + y) * r.width + wx);
			for (let x = 0; x < width; x++, src += 4, dst += 4) {
				target[dst] = source[src + 2];
				target[dst + 1] = source[src + 1];
				target[dst + 2] = source[src];
				target[dst + 3] = 255;
			}
		}
		r.dirty = true;
	}

	enqueue(id, type, x = 0, y = 0, state = 0) {
		const r = this.windows.get(id);
		if (!r) return;
		const queue = this.contexts.get(r.context);
		if (type === CLOSE && r.closing) return;
		if (type === CLOSE) r.closing = true;
		const event = [id, type, x | 0, y | 0, state | 0];
		const last = queue[queue.length - 1];
		if ((type === MOUSE_MOVE || type === RESIZE) && last?.[0] === id && last[1] === type)
			queue[queue.length - 1] = event;
		else queue.push(event);
	}

	listen(r) {
		const on = (name, fn, options = {}) => r.canvas.addEventListener(name, fn, { ...options, signal: r.controller.signal });
		const point = e => {
			const rect = r.canvas.getBoundingClientRect();
			r.x = rect.width ? Math.floor((e.clientX - rect.left) * r.width / rect.width) : 0;
			r.y = rect.height ? Math.floor((e.clientY - rect.top) * r.height / rect.height) : 0;
		};
		for (const [name, type] of [['pointermove', MOUSE_MOVE], ['pointerenter', MOUSE_ENTER], ['pointerleave', MOUSE_LEAVE], ['pointerdown', MOUSE_PRESS], ['pointerup', MOUSE_RELEASE]]) {
			on(name, e => {
				if (!e.isPrimary || (r.pointerId !== null && e.pointerId !== r.pointerId)) return;
				point(e);
				const previous = r.buttons;
				r.buttons = e.buttons;
				if (name === 'pointerdown') {
					r.canvas.focus({ preventScroll: true });
					r.pointerId = e.pointerId;
					r.canvas.setPointerCapture(e.pointerId);
				}
				// Chorded buttons produce pointermove, not another pointerdown/up.
				if (name === 'pointermove' && r.pointerId !== null) {
					if (e.buttons & ~previous) this.enqueue(r.id, MOUSE_PRESS, r.x, r.y, e.buttons);
					if (previous & ~e.buttons) this.enqueue(r.id, MOUSE_RELEASE, r.x, r.y, e.buttons);
				}
				this.enqueue(r.id, type, r.x, r.y, e.buttons);
				if (name === 'pointerup' && !e.buttons) {
					r.pointerId = null;
					if (r.canvas.hasPointerCapture(e.pointerId)) r.canvas.releasePointerCapture(e.pointerId);
				}
				if (e.cancelable) e.preventDefault();
			});
		}
		on('pointercancel', () => this.releaseInput(r, false));
		on('lostpointercapture', () => this.releaseInput(r, false));
		on('blur', () => this.releaseInput(r));
		window.addEventListener('blur', () => this.releaseInput(r), { signal: r.controller.signal });
		on('contextmenu', e => e.preventDefault());
		on('wheel', e => {
			point(e);
			const delta = -e.deltaY * (e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? r.height : 1);
			this.enqueue(r.id, MOUSE_WHEEL, r.x, r.y, Math.max(-2147483648, Math.min(2147483647, Math.round(delta))));
			e.preventDefault();
		}, { passive: false });
		for (const [name, type] of [['keydown', KEY_PRESS], ['keyup', KEY_RELEASE]]) {
			on(name, e => {
				const code = keys[e.code];
				if (!code) return;
				if (type === KEY_PRESS) r.keys.set(e.code, code);
				else r.keys.delete(e.code);
				this.enqueue(r.id, type, code, 0, modifiers(e));
				e.preventDefault();
			});
		}
	}

	releaseInput(r, keyboard = true) {
		if (keyboard) {
			for (const code of r.keys.values()) this.enqueue(r.id, KEY_RELEASE, code, 0, 0);
			r.keys.clear();
		}
		if (r.buttons) this.enqueue(r.id, MOUSE_RELEASE, r.x, r.y, 0);
		r.buttons = 0;
		const pointer = r.pointerId;
		r.pointerId = null;
		if (pointer !== null && r.canvas.hasPointerCapture(pointer)) r.canvas.releasePointerCapture(pointer);
	}

	remove(id) {
		const r = this.windows.get(id);
		if (!r) return;
		this.windows.delete(id);
		r.controller.abort();
		r.observer?.disconnect();
		this.releaseInput(r);
		for (const child of [...this.windows.values()])
			if (child.parent === id && this.windows.has(child.id)) this.wasm.window_free(child.pointer);
		r.element.remove();
		const queue = this.contexts.get(r.context);
		if (queue) this.contexts.set(r.context, queue.filter(event => event[0] !== id));
	}

	getCanvas(id) { return this.windows.get(id)?.canvas || null; }
	close(id) { this.enqueue(id, CLOSE); }
	resize(id, width, height) {
		if (!Number.isInteger(width) || !Number.isInteger(height) || width < 0 || height < 0 || width > 16384 || height > 16384)
			throw new RangeError('Invalid Vinci size');
		const r = this.windows.get(id);
		if (r) this.wasm.window_resize(r.pointer, width, height);
	}
	move(id, x, y) {
		if (![x, y].every(n => Number.isInteger(n) && n >= 0 && n <= 0xffffffff)) throw new RangeError('Invalid Vinci position');
		const r = this.windows.get(id);
		if (r) this.wasm.window_move(r.pointer, x, y);
	}
	idle() {
		for (const g of [...this.contexts.keys()])
			if (this.contexts.has(g)) this.wasm.vinci_idle(g);
	}
	start() {
		if (this.disposed || this.frame !== null) return;
		const tick = () => {
			this.frame = requestAnimationFrame(tick);
			this.idle();
		};
		this.frame = requestAnimationFrame(tick);
	}
	stop() {
		if (this.frame !== null) cancelAnimationFrame(this.frame);
		this.frame = null;
	}
	dispose() {
		if (this.disposed) return;
		this.disposed = true;
		this.stop();
		for (const g of [...this.contexts.keys()])
			if (this.contexts.has(g)) this.wasm.vinci_destroy(g);
		this.parents.clear();
	}
}
