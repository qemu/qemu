#ifndef QEMU_OSDEP_H
#define QEMU_OSDEP_H

#include "config-host.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#ifdef __OpenBSD__
#include <sys/signal.h>
#endif

#ifndef _WIN32
#include <sys/wait.h>
#else
#define WIFEXITED(x)   1
#define WEXITSTATUS(x) (x)
#endif

#include <sys/time.h>

#if defined(CONFIG_SOLARIS) && CONFIG_SOLARIS_VERSION < 10
/* [u]int_fast*_t not in <sys/int_types.h> */
typedef unsigned char           uint_fast8_t;
typedef unsigned int            uint_fast16_t;
typedef signed int              int_fast16_t;
#endif

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

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member));})
#endif

/* Convert from a base type to a parent type, with compile time checking.  */
#ifdef __GNUC__
#define DO_UPCAST(type, field, dev) ( __extension__ ( { \
    char __attribute__((unused)) offset_must_be_zero[ \
        -offsetof(type, field)]; \
    container_of(dev, type, field);}))
#else
#define DO_UPCAST(type, field, dev) container_of(dev, type, field)
#endif

#define typeof_field(type, field) typeof(((type *)0)->field)
#define type_check(t1,t2) ((t1*)0 - (t2*)0)

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef ROUND_UP
#define ROUND_UP(n,d) (((n) + (d) - 1) & -(d))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef always_inline
#if !((__GNUC__ < 3) || defined(__APPLE__))
#ifdef __OPTIMIZE__
#undef inline
#define inline __attribute__ (( always_inline )) __inline__
#endif
#endif
#else
#undef inline
#define inline always_inline
#endif

#define qemu_printf printf

int qemu_daemon(int nochdir, int noclose);
void *qemu_memalign(size_t alignment, size_t size);
void *qemu_anon_ram_alloc(size_t size);
void qemu_vfree(void *ptr);
void qemu_anon_ram_free(void *ptr, size_t size);

#define QEMU_MADV_INVALID -1

#if defined(CONFIG_MADVISE)

#define QEMU_MADV_WILLNEED  MADV_WILLNEED
#define QEMU_MADV_DONTNEED  MADV_DONTNEED
#ifdef MADV_DONTFORK
#define QEMU_MADV_DONTFORK  MADV_DONTFORK
#else
#define QEMU_MADV_DONTFORK  QEMU_MADV_INVALID
#endif
#ifdef MADV_MERGEABLE
#define QEMU_MADV_MERGEABLE MADV_MERGEABLE
#else
#define QEMU_MADV_MERGEABLE QEMU_MADV_INVALID
#endif
#ifdef MADV_DONTDUMP
#define QEMU_MADV_DONTDUMP MADV_DONTDUMP
#else
#define QEMU_MADV_DONTDUMP QEMU_MADV_INVALID
#endif
#ifdef MADV_HUGEPAGE
#define QEMU_MADV_HUGEPAGE MADV_HUGEPAGE
#else
#define QEMU_MADV_HUGEPAGE QEMU_MADV_INVALID
#endif

#elif defined(CONFIG_POSIX_MADVISE)

#define QEMU_MADV_WILLNEED  POSIX_MADV_WILLNEED
#define QEMU_MADV_DONTNEED  POSIX_MADV_DONTNEED
#define QEMU_MADV_DONTFORK  QEMU_MADV_INVALID
#define QEMU_MADV_MERGEABLE QEMU_MADV_INVALID
#define QEMU_MADV_DONTDUMP QEMU_MADV_INVALID
#define QEMU_MADV_HUGEPAGE  QEMU_MADV_INVALID

#else /* no-op */

#define QEMU_MADV_WILLNEED  QEMU_MADV_INVALID
#define QEMU_MADV_DONTNEED  QEMU_MADV_INVALID
#define QEMU_MADV_DONTFORK  QEMU_MADV_INVALID
#define QEMU_MADV_MERGEABLE QEMU_MADV_INVALID
#define QEMU_MADV_DONTDUMP QEMU_MADV_INVALID
#define QEMU_MADV_HUGEPAGE  QEMU_MADV_INVALID

#endif

int qemu_madvise(void *addr, size_t len, int advice);

int qemu_open(const char *name, int flags, ...);
int qemu_close(int fd);

#if defined(__HAIKU__) && defined(__i386__)
#define FMT_pid "%ld"
#elif defined(WIN64)
#define FMT_pid "%" PRId64
#else
#define FMT_pid "%d"
#endif

int qemu_create_pidfile(const char *filename);
int qemu_get_thread_id(void);

#ifndef CONFIG_IOVEC
struct iovec {
    void *iov_base;
    size_t iov_len;
};
/*
 * Use the same value as Linux for now.
 */
#define IOV_MAX 1024

ssize_t readv(int fd, const struct iovec *iov, int iov_cnt);
ssize_t writev(int fd, const struct iovec *iov, int iov_cnt);
#else
#include <sys/uio.h>
#endif

#ifdef _WIN32
static inline void qemu_timersub(const struct timeval *val1,
                                 const struct timeval *val2,
                                 struct timeval *res)
{
    res->tv_sec = val1->tv_sec - val2->tv_sec;
    if (val1->tv_usec < val2->tv_usec) {
        res->tv_sec--;
        res->tv_usec = val1->tv_usec - val2->tv_usec + 1000 * 1000;
    } else {
        res->tv_usec = val1->tv_usec - val2->tv_usec;
    }
}
#else
#define qemu_timersub timersub
#endif

void qemu_set_cloexec(int fd);

void qemu_set_version(const char *);
const char *qemu_get_version(void);

void fips_set_state(bool requested);
bool fips_get_state(void);

/* Return a dynamically allocated pathname denoting a file or directory that is
 * appropriate for storing local state.
 *
 * @relative_pathname need not start with a directory separator; one will be
 * added automatically.
 *
 * The caller is responsible for releasing the value returned with g_free()
 * after use.
 */
char *qemu_get_local_state_pathname(const char *relative_pathname);

/* Find program directory, and save it for later usage with
 * qemu_get_exec_dir().
 * Try OS specific API first, if not working, parse from argv0. */
void qemu_init_exec_dir(const char *argv0);

/* Get the saved exec dir.
 * Caller needs to release the returned string by g_free() */
char *qemu_get_exec_dir(void);

/**
 * qemu_getauxval:
 * @type: the auxiliary vector key to lookup
 *
 * Search the auxiliary vector for @type, returning the value
 * or 0 if @type is not present.
 */
#if defined(CONFIG_GETAUXVAL) || defined(__linux__)
unsigned long qemu_getauxval(unsigned long type);
#else
static inline unsigned long qemu_getauxval(unsigned long type) { return 0; }
#endif

/**
 * qemu_init_auxval:
 * @envp: the third argument to main
 *
 * If supported and required, locate the auxiliary vector at program startup.
 */
#if defined(CONFIG_GETAUXVAL) || !defined(__linux__)
static inline void qemu_init_auxval(char **envp) { }
#else
void qemu_init_auxval(char **envp);
#endif

void qemu_set_tty_echo(int fd, bool echo);

#endif
