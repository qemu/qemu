#ifndef QEMU_OSDEP_H
#define QEMU_OSDEP_H

#include <stdarg.h>

#define qemu_printf printf

void *qemu_malloc(size_t size);
void *qemu_mallocz(size_t size);
void qemu_free(void *ptr);
char *qemu_strdup(const char *str);

void *qemu_vmalloc(size_t size);
void qemu_vfree(void *ptr);

void *get_mmap_addr(unsigned long size);

#endif
