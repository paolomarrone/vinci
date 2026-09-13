/* Browser integration tests, GPL-3.0-or-later. Run with make test-web. */
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || 'playwright');
const directory = resolve(process.argv[2] || 'build/web');
const server = createServer(async (req, res) => {
	const name = new URL(req.url, 'http://localhost').pathname.slice(1) || 'index.html';
	if (!['index.html', 'vinci-web.js', 'demo.wasm', 'test.wasm'].includes(name)) { res.writeHead(404).end(); return; }
	try {
		res.setHeader('Content-Type', name.endsWith('.wasm') ? 'application/wasm' : name.endsWith('.js') ? 'text/javascript' : 'text/html');
		res.end(await readFile(resolve(directory, name)));
	} catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
	browser = await chromium.launch({ headless: true, ...(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {}) });
	const page = await browser.newPage();
	const errors = [];
	page.on('pageerror', e => errors.push(e.message));
	await page.goto(`http://127.0.0.1:${server.address().port}/`);
	await page.waitForFunction(() => document.querySelectorAll('canvas').length === 2);
	const initial = await page.locator('canvas').first().evaluate(c => [...c.getContext('2d').getImageData(25, 0, 1, 1).data]);
	assert.deepEqual(initial, [50, 107, 137, 255]);
	await page.locator('canvas').first().click({ position: { x: 20, y: 20 } });
	await page.keyboard.press('a');
	await page.waitForFunction(() => document.querySelector('article output').textContent.includes('Last key: 4'));
	await page.getByRole('button', { name: 'Resize', exact: true }).first().click();
	await page.waitForFunction(() => document.querySelector('canvas').width === 256);
	await page.getByRole('button', { name: 'Close', exact: true }).first().click();
	await page.waitForFunction(() => document.querySelectorAll('canvas').length === 1);
	await page.getByRole('button', { name: 'Add a view' }).click();
	await page.waitForFunction(() => document.querySelectorAll('canvas').length === 2);
	console.log('PASS: demo pixels, real mouse/keyboard input, resize, close and recreate');

	// Load the adapter in a separate document without the demo's animation loop.
	await page.goto(`http://127.0.0.1:${server.address().port}/vinci-web.js`);
	await page.evaluate(async () => {
		document.body.innerHTML = '<div id="a"></div><div id="b"></div>';
		const { VinciWeb } = await import('./vinci-web.js');
		const bytes = await (await fetch('./test.wasm')).arrayBuffer();
		const module = await WebAssembly.compile(bytes);
		if (WebAssembly.Module.imports(module).some(i => i.module !== 'vinci_web')) throw new Error('Unexpected runtime dependency');
		window.modules = [];
		for (const root of document.querySelectorAll('div')) {
			const adapter = new VinciWeb(root);
			const instance = await WebAssembly.instantiate(module, adapter.imports);
			adapter.bind(instance);
			modules.push({ adapter, w: instance.exports });
		}
		const { adapter: a, w } = modules[0];
		window.a = a; window.w = w;
		window.f = w.test_new(0, 1);
		window.win = w.test_window(f);
		window.id = w.window_get_handle(win);
		a.getCanvas(id).id = 'target';
	});

	await page.evaluate(() => {
		const check = (value, message) => { if (!value) throw new Error(message); };
		const pixel = (x, y) => [...a.getCanvas(id).getContext('2d').getImageData(x, y, 1, 1).data].join(',');
		w.test_draw(f, 0, 0, 3, 2, 0, 0, 3, 2);
		a.idle();
		check(pixel(0, 0) === '255,0,0,255', 'red / byte order');
		check(pixel(1, 0) === '0,255,0,255', 'green');
		check(pixel(2, 1) === '171,205,239,255', 'source stride');
		w.test_draw(f, 0, 0, 3, 2, -1, 4, 3, 2);
		w.test_draw(f, -1, 0, 3, 2, 5, 4, 3, 2);
		w.test_draw(f, 0, -1, 3, 2, 10, 4, 3, 2);
		w.test_draw(f, 0, 0, 3, 2, 63, 47, 3, 2);
		w.test_draw(f, -2147483648, 0, 3, 2, 0, 0, 2147483647, 2);
		w.test_draw(f, 0, 0, 3, 2, -2147483648, 0, 2147483647, 2);
		a.idle();
		check(pixel(0, 4) === '0,255,0,255', 'negative destination clipping');
		check(pixel(5, 4) === '0,0,0,255' && pixel(6, 4) === '255,0,0,255', 'negative source clipping');
		check(pixel(10, 4) === '0,0,0,255' && pixel(10, 5) === '255,0,0,255', 'negative source y');
		check(pixel(63, 47) === '255,0,0,255', 'right/bottom clipping');
		w.memory.grow(1);
		w.test_draw(f, 1, 0, 3, 2, 10, 10, 1, 1);
		a.idle();
		check(pixel(10, 10) === '0,255,0,255', 'memory.grow invalidated views');
		w.window_hide(win);
		check(!a.getCanvas(id).getClientRects().length, 'hide');
		w.window_show(win);
		check(a.getCanvas(id).getClientRects().length, 'show');
		const n = w.test_count(f, 1);
		a.resize(id, 80, 60);
		check(w.test_count(f, 1) === n, 'resize callback must be deferred');
		check(w.window_get_width(win) === 80, 'immediate dimensions');
		a.idle();
		check(w.test_count(f, 1) === n + 1 && a.getCanvas(id).width === 80, 'resize callback');
		a.resize(id, 0, 0); a.idle();
		check(w.window_get_width(win) === 0, 'zero resize');
		a.resize(id, 64, 48); a.idle();
		w.window_resize(win, 2147483647, 2147483647);
		check(w.window_get_width(win) === 64, 'failed resize leaves dimensions');
		a.move(id, 3, 4); a.idle();
		check(w.test_count(f, 3) === 1 && w.test_last(f, 1) === 3, 'move callback');
	});
	console.log('PASS: clipping, byte order, stride, memory growth, show/hide, resize and move');

	await page.locator('#target').evaluate(c => { c.style.width = '96px'; c.style.height = '72px'; });
	await page.waitForFunction(() => a.contexts.get(w.test_context(f)).some(e => e[1] === 1));
	await page.evaluate(() => a.idle());
	assert.equal(await page.evaluate(() => w.window_get_width(win)), 96);
	await page.locator('#target').evaluate(c => { c.style.transformOrigin = 'top left'; c.style.transform = 'scale(2)'; });
	const rect = await page.locator('#target').boundingBox();
	await page.mouse.move(rect.x + 40, rect.y + 24);
	await page.mouse.down();
	assert.equal(await page.evaluate(() => w.test_count(f, 5)), 0);
	await page.evaluate(() => a.idle());
	assert.deepEqual(await page.evaluate(() => [0, 1, 2, 3].map(i => w.test_last(f, i))), [5, 20, 12, 1]);
	await page.mouse.move(rect.x + rect.width + 20, rect.y + 10);
	await page.mouse.up();
	await page.evaluate(() => a.idle());
	assert.ok(await page.evaluate(() => w.test_count(f, 6) >= 1));
	await page.mouse.move(rect.x + 20, rect.y + 20);
	await page.locator('#target').focus();
	await page.keyboard.down('Shift');
	const releases = await page.evaluate(() => w.test_count(f, 11));
	await page.mouse.down();
	await page.mouse.down({ button: 'right' });
	await page.evaluate(() => a.idle());
	assert.equal(await page.evaluate(() => w.test_count(f, 5)), 3);
	await page.mouse.up({ button: 'right' });
	await page.mouse.up();
	await page.evaluate(() => a.idle());
	assert.equal(await page.evaluate(() => w.test_count(f, 11)), releases);
	await page.keyboard.up('Shift');
	await page.locator('#target').focus();
	await page.keyboard.down('Shift');
	await page.keyboard.down('a');
	await page.evaluate(() => a.idle());
	assert.deepEqual(await page.evaluate(() => [w.test_last(f, 1), w.test_last(f, 3)]), [4, 1]);
	await page.locator('#target').evaluate(c => c.blur());
	await page.evaluate(() => a.idle());
	assert.equal(await page.evaluate(() => w.test_last(f, 0)), 11);
	await page.keyboard.up('a');
	await page.keyboard.up('Shift');
	await page.mouse.move(rect.x + 10, rect.y + 10);
	await page.mouse.wheel(0, 60);
	await page.waitForFunction(() => a.contexts.get(w.test_context(f)).some(e => e[1] === 7));
	await page.evaluate(() => a.idle());
	assert.equal(await page.evaluate(() => w.test_last(f, 3)), -60);
	console.log('PASS: CSS resize/scaling, deferred input, pointer capture, keyboard modifiers, blur releases and wheel');

	await page.evaluate(() => {
		const check = (value, message) => { if (!value) throw new Error(message); };
		const { adapter: b, w: other } = modules[1];
		const otherF = other.test_new(0, 1), otherId = other.window_get_handle(other.test_window(otherF));
		other.test_draw(otherF, 2, 0, 3, 2, 0, 0, 1, 1); b.idle();
		check(b.getCanvas(otherId).getContext('2d').getImageData(0, 0, 1, 1).data[2] === 255, 'second module pixels');
		const sibling = w.test_new(0, 1), siblingId = w.window_get_handle(w.test_window(sibling));
		a.close(siblingId); a.close(siblingId);
		w.vinci_idle(w.test_context(f));
		check(w.test_count(sibling, 2) === 0, 'context event isolation');
		w.vinci_idle(w.test_context(sibling));
		check(w.test_count(sibling, 2) === 1 && !a.getCanvas(siblingId), 'one close notification');
		w.test_free(sibling);
		const g = w.test_context(f);
		const child = w.window_new(g, id, 8, 8, 0, 0);
		const childId = w.window_get_handle(child);
		check(child && !a.getCanvas(childId).getClientRects().length, 'initially hidden child');
		w.window_show(child);
		check(a.getCanvas(id).parentElement.contains(a.getCanvas(childId)), 'parent window handle');
		const focusedChild = w.test_new(id, 1), focusedId = w.window_get_handle(w.test_window(focusedChild));
		a.getCanvas(focusedId).dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyA' }));
		a.idle();
		w.window_hide(win); a.idle();
		check(w.test_count(focusedChild, 11) === 1, 'hiding parent releases child input');
		w.window_show(win);
		w.test_free(focusedChild);
		const plain = w.window_new(g, 0, 8, 8, 1, 0), plainId = w.window_get_handle(plain);
		a.close(plainId); a.idle();
		check(!a.getCanvas(plainId), 'default close frees callback-free window');
		const oldCanvas = a.getCanvas(id);
		a.close(id);
		w.test_free(f);
		check(!a.getCanvas(id) && !a.getCanvas(childId), 'free removes descendants and stale events');
		check(b.getCanvas(otherId), 'module survives other teardown');
		window.f = w.test_new(0, 1);
		window.win = w.test_window(f); window.id = w.window_get_handle(win);
		oldCanvas.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyA' }));
		a.idle();
		check(w.test_count(f, 10) === 0, 'listener cleanup/address reuse');
		for (const action of [1, 2, 3]) {
			const item = w.test_new(0, 1), handle = w.window_get_handle(w.test_window(item));
			w.test_action(item, action);
			const canvas = a.getCanvas(handle);
			canvas.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyB' }));
			canvas.dispatchEvent(new KeyboardEvent('keyup', { code: 'KeyB' }));
			a.idle();
			check(w.test_count(item, 10) === 1, 'callback dispatched');
			check(w.test_count(item, 11) === (action === 3 ? 1 : 0), 'callback free/destroy/reentry');
			w.test_free(item);
		}
		const canvas = a.getCanvas(id);
		for (let i = 0; i < 300; i++) canvas.dispatchEvent(new KeyboardEvent('keydown', { code: 'KeyC' }));
		a.idle();
		check(w.test_count(f, 10) === 256, 'bounded idle dispatch');
		a.idle();
		check(w.test_count(f, 10) === 300, 'remaining events retained');
		const parent = a.registerParent(document.createElement('div'));
		const embedded = w.window_new(w.test_context(f), parent, 8, 8, 1, 0);
		const embeddedId = w.window_get_handle(embedded);
		a.unregisterParent(parent);
		check(!a.getCanvas(embeddedId), 'unregister releases embedded windows');
		check(w.window_new(w.test_context(f), parent, 8, 8, 1, 0) === 0, 'invalid parent fails');
		const bytes = w.memory.buffer.byteLength;
		for (let i = 0; i < 100; i++) { const item = w.test_new(0, 1); w.test_free(item); }
		check(w.memory.buffer.byteLength === bytes, 'allocator reuse after repeated lifecycle');
		const p = w.test_alloc(400000); check(p, 'allocator grows memory'); w.test_release(p);
		w.test_free(f); other.test_free(otherF);
		// Leave raw contexts/windows to exercise adapter-wide disposal.
		const remaining = w.vinci_new(); w.window_new(remaining, 0, 8, 8, 1, 0);
		a.start(); a.dispose(); a.dispose(); b.dispose();
		check(a.frame === null && !a.contexts.size && !a.windows.size && !a.parents.size, 'dispose clears resources');
		check(document.querySelectorAll('canvas').length === 0, 'dispose removes DOM');
		check(w.vinci_new() === 0, 'cannot create on disposed adapter');
	});
	assert.deepEqual(errors, []);
	console.log('PASS: multiple modules/contexts/windows, nested windows, close/free/destroy during callbacks, stale events, bounded idle, allocator reuse and disposal');
} finally {
	await browser?.close();
	await new Promise(resolve => server.close(resolve));
}
