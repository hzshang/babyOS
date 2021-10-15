#ifndef __INCLUDE_LIBCC_H
#define __INCLUDE_LIBCC_H
#include <types.h>

void *memset(void *s, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
int memcmp(const void *v1, const void *v2, size_t n);

#endif
