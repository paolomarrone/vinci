/* Minimal declarations for the demo's freestanding allocator, GPL-3.0-or-later. */
#ifndef VINCI_DEMO_STDLIB_H
#define VINCI_DEMO_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void free(void *ptr);
#ifdef __cplusplus
}
#endif
#endif
