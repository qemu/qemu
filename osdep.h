#ifndef QEMU_OSDEP_H
#define QEMU_OSDEP_H

#include <stdarg.h>

#ifndef glue
#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s
#endif

#ifndef likely
#if __GNUC__ < 3
#define __builtin_expect(x, n) (x)
#endif

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x)   __builtin_expect(!!(x), 0)
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef always_inline
#if (__GNUC__ < 3) || defined(__APPLE__)
#define always_inline inline
#else
#define always_inline __attribute__ (( always_inline )) __inline__
#endif
#endif
#define inline always_inline

#ifdef __i386__
#define REGPARM(n) __attribute((regparm(n)))
#else
#define REGPARM(n)
#endif

#define qemu_printf printf

void *qemu_malloc(size_t size);
void *qemu_mallocz(size_t size);
void qemu_free(void *ptr);
char *qemu_strdup(const char *str);

void *qemu_memalign(size_t alignment, size_t size);
void *qemu_vmalloc(size_t size);
void qemu_vfree(void *ptr);

void *get_mmap_addr(unsigned long size);

int qemu_create_pidfile(const char *filename);

#ifdef _WIN32
int ffs(int i);

typedef struct {
    long tv_sec;
    long tv_usec;
} qemu_timeval;
int qemu_gettimeofday(qemu_timeval *tp);
#else
typedef struct timeval qemu_timeval;
#define qemu_gettimeofday(tp) gettimeofday(tp, NULL);
#endif /* !_WIN32 */

#endif
