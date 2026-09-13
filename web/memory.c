/* Demo-only freestanding allocator. Hosts should link their own C runtime.
 * Vinci - GPL-3.0-or-later. Copyright (C) 2026 Orastron Srl unipersonale.
 */
#include <stdint.h>
#include "stdlib.h"

void *memcpy(void *dest, const void *src, size_t size);
void *memset(void *dest, int value, size_t size);

void *memcpy(void *dest, const void *src, size_t size) {
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	for (size_t i = 0; i < size; i++) d[i] = s[i];
	return dest;
}

void *memset(void *dest, int value, size_t size) {
	unsigned char *d = (unsigned char *)dest;
	for (size_t i = 0; i < size; i++) d[i] = (unsigned char)value;
	return dest;
}

typedef struct block {
	size_t size;
	struct block *next;
	uint32_t available, pad;
} block;

extern unsigned char __heap_base;
static block *blocks;
static uintptr_t end;

void *malloc(size_t size) {
	if (!size || size > SIZE_MAX - 31) return NULL;
	size = (size + 15) & ~(size_t)15;
	block **link = &blocks;
	for (; *link; link = &(*link)->next) {
		block *b = *link;
		if (!b->available || b->size < size) continue;
		if (b->size >= size + sizeof(block) + 16) {
			block *tail = (block *)((unsigned char *)(b + 1) + size);
			tail->size = b->size - size - sizeof(block);
			tail->next = b->next;
			tail->available = 1;
			b->next = tail;
			b->size = size;
		}
		b->available = 0;
		return b + 1;
	}
	if (!end) end = ((uintptr_t)&__heap_base + 15) & ~(uintptr_t)15;
	if (size > SIZE_MAX - end - sizeof(block)) return NULL;
	uintptr_t next = end + sizeof(block) + size;
	size_t pages = next / 65536 + (next % 65536 != 0);
	size_t current = __builtin_wasm_memory_size(0);
	if (pages > current && __builtin_wasm_memory_grow(0, pages - current) == (size_t)-1) return NULL;
	block *b = (block *)end;
	b->size = size;
	b->next = NULL;
	b->available = 0;
	*link = b;
	end = next;
	return b + 1;
}

void free(void *ptr) {
	if (!ptr) return;
	((block *)ptr - 1)->available = 1;
	for (block *b = blocks; b && b->next;) {
		if (b->available && b->next->available) {
			b->size += sizeof(block) + b->next->size;
			b->next = b->next->next;
		} else b = b->next;
	}
}

void *calloc(size_t count, size_t size) {
	if (size && count > SIZE_MAX / size) return NULL;
	void *p = malloc(count * size);
	if (p) memset(p, 0, count * size);
	return p;
}
