#ifndef QEMU_OSDEP_H
#define QEMU_OSDEP_H

#include <stdarg.h>

int qemu_vsnprintf(char *buf, int buflen, const char *fmt, va_list args);
void qemu_vprintf(const char *fmt, va_list ap);
void qemu_printf(const char *fmt, ...);

void *qemu_malloc(size_t size);
void *qemu_mallocz(size_t size);
void qemu_free(void *ptr);

void *get_mmap_addr(unsigned long size);

/* specific kludges for OS compatibility (should be moved elsewhere) */
#if defined(__i386__) && !defined(CONFIG_SOFTMMU) && !defined(CONFIG_USER_ONLY)

/* disabled pthread version of longjmp which prevent us from using an
   alternative signal stack */
extern void __longjmp(jmp_buf env, int val);
#define longjmp __longjmp

#endif

#endif
