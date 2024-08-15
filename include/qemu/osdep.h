/*
 * OS includes and handling of OS dependencies
 *
 * This header exists to pull in some common system headers that
 * most code in QEMU will want, and to fix up some possible issues with
 * it (missing defines, Windows weirdness, and so on).
 *
 * To avoid getting into possible circular include dependencies, this
 * file should not include any other QEMU headers, with the exceptions
 * of config-host.h, config-target.h, qemu/compiler.h,
 * sysemu/os-posix.h, sysemu/os-win32.h, glib-compat.h and
 * qemu/typedefs.h, all of which are doing a similar job to this file
 * and are under similar constraints.
 *
 * This header also contains prototypes for functions defined in
 * os-*.c and util/oslib-*.c; those would probably be better split
 * out into separate header files.
 *
 * In an ideal world this header would contain only:
 *  (1) things which everybody needs
 *  (2) things without which code would work on most platforms but
 *      fail to compile or misbehave on a minority of host OSes
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_OSDEP_H
#define QEMU_OSDEP_H

#if !defined _FORTIFY_SOURCE && defined __OPTIMIZE__ && __OPTIMIZE__ && defined __linux__
# define _FORTIFY_SOURCE 2
#endif

#include "config-host.h"
#ifdef COMPILING_PER_TARGET
#include CONFIG_TARGET
#else
#include "exec/poison.h"
#endif

/*
 * HOST_WORDS_BIGENDIAN was replaced with HOST_BIG_ENDIAN. Prevent it from
 * creeping back in.
 */
#pragma GCC poison HOST_WORDS_BIGENDIAN

/*
 * TARGET_WORDS_BIGENDIAN was replaced with TARGET_BIG_ENDIAN. Prevent it from
 * creeping back in.
 */
#pragma GCC poison TARGET_WORDS_BIGENDIAN

#include "qemu/compiler.h"

/* Older versions of C++ don't get definitions of various macros from
 * stdlib.h unless we define these macros before first inclusion of
 * that system header.
 */
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

/* The following block of code temporarily renames the daemon() function so the
 * compiler does not see the warning associated with it in stdlib.h on OSX
 */
#ifdef __APPLE__
#define daemon qemu_fake_daemon_function
#include <stdlib.h>
#undef daemon
QEMU_EXTERN_C int daemon(int, int);
#endif

#ifdef _WIN32
/* as defined in sdkddkver.h */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602 /* Windows 8 API (should be >= the one from glib) */
#endif
/* reduces the number of implicitly included headers */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

/* enable C99/POSIX format strings (needs mingw32-runtime 3.15 or later) */
#ifdef __MINGW32__
#define __USE_MINGW_ANSI_STDIO 1
#endif

/*
 * We need the FreeBSD "legacy" definitions. Rust needs the FreeBSD 11 system
 * calls since it doesn't use libc at all, so we have to emulate that despite
 * FreeBSD 11 being EOL'd.
 */
#ifdef __FreeBSD__
#define _WANT_FREEBSD11_STAT
#define _WANT_FREEBSD11_STATFS
#define _WANT_FREEBSD11_DIRENT
#define _WANT_KERNEL_ERRNO
#define _WANT_SEMUN
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <limits.h>
/* Put unistd.h before time.h as that triggers localtime_r/gmtime_r
 * function availability on recentish Mingw-w64 platforms. */
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <assert.h>
/* setjmp must be declared before sysemu/os-win32.h
 * because it is redefined there. */
#include <setjmp.h>
#include <signal.h>

#ifdef CONFIG_IOVEC
#include <sys/uio.h>
#endif

#if defined(__linux__) && defined(__sparc__)
/* The SPARC definition of QEMU_VMALLOC_ALIGN needs SHMLBA */
#include <sys/shm.h>
#endif

#ifndef _WIN32
#include <sys/wait.h>
#else
#define WIFEXITED(x)   1
#define WEXITSTATUS(x) (x)
#endif

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

/*
 * This is somewhat like a system header; it must be outside any extern "C"
 * block because it includes system headers itself, including glib.h,
 * which will not compile if inside an extern "C" block.
 */
#include "glib-compat.h"

#ifdef _WIN32
#include "sysemu/os-win32.h"
#endif

#ifdef CONFIG_POSIX
#include "sysemu/os-posix.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "qemu/typedefs.h"

/**
 * Mark a function that executes in coroutine context
 *
 * Functions that execute in coroutine context cannot be called directly from
 * normal functions.  In the future it would be nice to enable compiler or
 * static checker support for catching such errors.  This annotation might make
 * it possible and in the meantime it serves as documentation.
 *
 * For example:
 *
 *   static void coroutine_fn foo(void) {
 *       ....
 *   }
 */
#ifdef __clang__
#define coroutine_fn QEMU_ANNOTATE("coroutine_fn")
#else
#define coroutine_fn
#endif

/**
 * Mark a function that can suspend when executed in coroutine context,
 * but can handle running in non-coroutine context too.
 */
#ifdef __clang__
#define coroutine_mixed_fn QEMU_ANNOTATE("coroutine_mixed_fn")
#else
#define coroutine_mixed_fn
#endif

/**
 * Mark a function that should not be called from a coroutine context.
 * Usually there will be an analogous, coroutine_fn function that should
 * be used instead.
 *
 * When the function is also marked as coroutine_mixed_fn, the function should
 * only be called if the caller does not know whether it is in coroutine
 * context.
 *
 * Functions that are only no_coroutine_fn, on the other hand, should not
 * be called from within coroutines at all.  This for example includes
 * functions that block.
 *
 * In the future it would be nice to enable compiler or static checker
 * support for catching such errors.  This annotation is the first step
 * towards this, and in the meantime it serves as documentation.
 *
 * For example:
 *
 *   static void no_coroutine_fn foo(void) {
 *       ....
 *   }
 */
#ifdef __clang__
#define no_coroutine_fn QEMU_ANNOTATE("no_coroutine_fn")
#else
#define no_coroutine_fn
#endif


/*
 * For mingw, as of v6.0.0, the function implementing the assert macro is
 * not marked as noreturn, so the compiler cannot delete code following an
 * assert(false) as unused.  We rely on this within the code base to delete
 * code that is unreachable when features are disabled.
 * All supported versions of Glib's g_assert() satisfy this requirement.
 */
#ifdef __MINGW32__
#undef assert
#define assert(x)  g_assert(x)
#endif

/**
 * qemu_build_not_reached()
 *
 * The compiler, during optimization, is expected to prove that a call
 * to this function cannot be reached and remove it.  If the compiler
 * supports QEMU_ERROR, this will be reported at compile time; otherwise
 * this will be reported at link time due to the missing symbol.
 */
G_NORETURN
void QEMU_ERROR("code path is reachable")
    qemu_build_not_reached_always(void);
#if defined(__OPTIMIZE__) && !defined(__NO_INLINE__)
#define qemu_build_not_reached()  qemu_build_not_reached_always()
#else
#define qemu_build_not_reached()  g_assert_not_reached()
#endif

/**
 * qemu_build_assert()
 *
 * The compiler, during optimization, is expected to prove that the
 * assertion is true.
 */
#define qemu_build_assert(test)  while (!(test)) qemu_build_not_reached()

/*
 * According to waitpid man page:
 * WCOREDUMP
 *  This  macro  is  not  specified  in POSIX.1-2001 and is not
 *  available on some UNIX implementations (e.g., AIX, SunOS).
 *  Therefore, enclose its use inside #ifdef WCOREDUMP ... #endif.
 */
#ifndef WCOREDUMP
#define WCOREDUMP(status) 0
#endif
/*
 * We have a lot of unaudited code that may fail in strange ways, or
 * even be a security risk during migration, if you disable assertions
 * at compile-time.  You may comment out these safety checks if you
 * absolutely want to disable assertion overhead, but it is not
 * supported upstream so the risk is all yours.  Meanwhile, please
 * submit patches to remove any side-effects inside an assertion, or
 * fixing error handling that should use Error instead of assert.
 */
#ifdef NDEBUG
#error building with NDEBUG is not supported
#endif
#ifdef G_DISABLE_ASSERT
#error building with G_DISABLE_ASSERT is not supported
#endif

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#ifndef ENOMEDIUM
#define ENOMEDIUM ENODEV
#endif
#if !defined(ENOTSUP)
#define ENOTSUP 4096
#endif
#if !defined(ECANCELED)
#define ECANCELED 4097
#endif
#if !defined(EMEDIUMTYPE)
#define EMEDIUMTYPE 4098
#endif
#if !defined(ESHUTDOWN)
#define ESHUTDOWN 4099
#endif

#define RETRY_ON_EINTR(expr) \
    (__extension__                                          \
        ({ typeof(expr) __result;                               \
           do {                                             \
                __result = (expr);         \
           } while (__result == -1 && errno == EINTR);     \
           __result; }))

/* time_t may be either 32 or 64 bits depending on the host OS, and
 * can be either signed or unsigned, so we can't just hardcode a
 * specific maximum value. This is not a C preprocessor constant,
 * so you can't use TIME_MAX in an #ifdef, but for our purposes
 * this isn't a problem.
 */

/* The macros TYPE_SIGNED, TYPE_WIDTH, and TYPE_MAXIMUM are from
 * Gnulib, and are under the LGPL v2.1 or (at your option) any
 * later version.
 */

/* True if the real type T is signed.  */
#define TYPE_SIGNED(t) (!((t)0 < (t)-1))

/* The width in bits of the integer type or expression T.
 * Padding bits are not supported.
 */
#define TYPE_WIDTH(t) (sizeof(t) * CHAR_BIT)

/* The maximum and minimum values for the integer type T.  */
#define TYPE_MAXIMUM(t)                                                \
  ((t) (!TYPE_SIGNED(t)                                                \
        ? (t)-1                                                        \
        : ((((t)1 << (TYPE_WIDTH(t) - 2)) - 1) * 2 + 1)))

#ifndef TIME_MAX
#define TIME_MAX TYPE_MAXIMUM(time_t)
#endif

/* Mac OSX has a <stdint.h> bug that incorrectly defines SIZE_MAX with
 * the wrong type. Our replacement isn't usable in preprocessor
 * expressions, but it is sufficient for our needs. */
#ifdef HAVE_BROKEN_SIZE_MAX
#undef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/*
 * Two variations of MIN/MAX macros. The first is for runtime use, and
 * evaluates arguments only once (so it is safe even with side
 * effects), but will not work in constant contexts (such as array
 * size declarations) because of the '{}'.  The second is for constant
 * expression use, where evaluating arguments twice is safe because
 * the result is going to be constant anyway, but will not work in a
 * runtime context because of a void expression where a value is
 * expected.  Thus, both gcc and clang will fail to compile if you use
 * the wrong macro (even if the error may seem a bit cryptic).
 *
 * Note that neither form is usable as an #if condition; if you truly
 * need to write conditional code that depends on a minimum or maximum
 * determined by the pre-processor instead of the compiler, you'll
 * have to open-code it.  Sadly, Coverity is severely confused by the
 * constant variants, so we have to dumb things down there.
 *
 * Preprocessor sorcery ahead: use different identifiers for the local
 * variables in each expansion, so we can nest macro calls without
 * shadowing variables.
 */
#define MIN_INTERNAL(a, b, _a, _b)                      \
    ({                                                  \
        typeof(1 ? (a) : (b)) _a = (a), _b = (b);       \
        _a < _b ? _a : _b;                              \
    })
#undef MIN
#define MIN(a, b) \
    MIN_INTERNAL((a), (b), MAKE_IDENTIFIER(_a), MAKE_IDENTIFIER(_b))

#define MAX_INTERNAL(a, b, _a, _b)                      \
    ({                                                  \
        typeof(1 ? (a) : (b)) _a = (a), _b = (b);       \
        _a > _b ? _a : _b;                              \
    })
#undef MAX
#define MAX(a, b) \
    MAX_INTERNAL((a), (b), MAKE_IDENTIFIER(_a), MAKE_IDENTIFIER(_b))

#ifdef __COVERITY__
# define MIN_CONST(a, b) ((a) < (b) ? (a) : (b))
# define MAX_CONST(a, b) ((a) > (b) ? (a) : (b))
#else
# define MIN_CONST(a, b)                                        \
    __builtin_choose_expr(                                      \
        __builtin_constant_p(a) && __builtin_constant_p(b),     \
        (a) < (b) ? (a) : (b),                                  \
        ((void)0))
# define MAX_CONST(a, b)                                        \
    __builtin_choose_expr(                                      \
        __builtin_constant_p(a) && __builtin_constant_p(b),     \
        (a) > (b) ? (a) : (b),                                  \
        ((void)0))
#endif

/*
 * Minimum function that returns zero only if both values are zero.
 * Intended for use with unsigned values only.
 *
 * Preprocessor sorcery ahead: use different identifiers for the local
 * variables in each expansion, so we can nest macro calls without
 * shadowing variables.
 */
#define MIN_NON_ZERO_INTERNAL(a, b, _a, _b)             \
    ({                                                  \
        typeof(1 ? (a) : (b)) _a = (a), _b = (b);       \
        _a == 0 ? _b : (_b == 0 || _b > _a) ? _a : _b;  \
    })
#define MIN_NON_ZERO(a, b) \
    MIN_NON_ZERO_INTERNAL((a), (b), MAKE_IDENTIFIER(_a), MAKE_IDENTIFIER(_b))

/*
 * Round number down to multiple. Safe when m is not a power of 2 (see
 * ROUND_DOWN for a faster version when a power of 2 is guaranteed).
 */
#define QEMU_ALIGN_DOWN(n, m) ((n) / (m) * (m))

/*
 * Round number up to multiple. Safe when m is not a power of 2 (see
 * ROUND_UP for a faster version when a power of 2 is guaranteed).
 */
#define QEMU_ALIGN_UP(n, m) QEMU_ALIGN_DOWN((n) + (m) - 1, (m))

/* Check if n is a multiple of m */
#define QEMU_IS_ALIGNED(n, m) (((n) % (m)) == 0)

/* n-byte align pointer down */
#define QEMU_ALIGN_PTR_DOWN(p, n) \
    ((typeof(p))QEMU_ALIGN_DOWN((uintptr_t)(p), (n)))

/* n-byte align pointer up */
#define QEMU_ALIGN_PTR_UP(p, n) \
    ((typeof(p))QEMU_ALIGN_UP((uintptr_t)(p), (n)))

/* Check if pointer p is n-bytes aligned */
#define QEMU_PTR_IS_ALIGNED(p, n) QEMU_IS_ALIGNED((uintptr_t)(p), (n))

/*
 * Round number down to multiple. Requires that d be a power of 2 (see
 * QEMU_ALIGN_UP for a safer but slower version on arbitrary
 * numbers); works even if d is a smaller type than n.
 */
#ifndef ROUND_DOWN
#define ROUND_DOWN(n, d) ((n) & -(0 ? (n) : (d)))
#endif

/*
 * Round number up to multiple. Requires that d be a power of 2 (see
 * QEMU_ALIGN_UP for a safer but slower version on arbitrary
 * numbers); works even if d is a smaller type than n.
 */
#ifndef ROUND_UP
#define ROUND_UP(n, d) ROUND_DOWN((n) + (d) - 1, (d))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

/*
 * &(x)[0] is always a pointer - if it's same type as x then the argument is a
 * pointer, not an array.
 */
#define QEMU_IS_ARRAY(x) (!__builtin_types_compatible_p(typeof(x), \
                                                        typeof(&(x)[0])))
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof(x) / sizeof((x)[0])) + \
                       QEMU_BUILD_BUG_ON_ZERO(!QEMU_IS_ARRAY(x)))
#endif

int qemu_daemon(int nochdir, int noclose);
void *qemu_anon_ram_alloc(size_t size, uint64_t *align, bool shared,
                          bool noreserve);
void qemu_anon_ram_free(void *ptr, size_t size);

#ifdef _WIN32
#define HAVE_CHARDEV_SERIAL 1
#define HAVE_CHARDEV_PARALLEL 1
#else
#if defined(__linux__) || defined(__sun__) || defined(__FreeBSD__)   \
    || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) \
    || defined(__GLIBC__) || defined(__APPLE__)
#define HAVE_CHARDEV_SERIAL 1
#endif
#if defined(__linux__) || defined(__FreeBSD__) \
    || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#define HAVE_CHARDEV_PARALLEL 1
#endif
#endif

#if defined(__HAIKU__)
#define SIGIO SIGPOLL
#endif

#ifdef HAVE_MADVISE_WITHOUT_PROTOTYPE
/*
 * See MySQL bug #7156 (http://bugs.mysql.com/bug.php?id=7156) for discussion
 * about Solaris missing the madvise() prototype.
 */
int madvise(char *, size_t, int);
#endif

#if defined(CONFIG_LINUX)
#ifndef BUS_MCEERR_AR
#define BUS_MCEERR_AR 4
#endif
#ifndef BUS_MCEERR_AO
#define BUS_MCEERR_AO 5
#endif
#endif

#if defined(__linux__) && \
    (defined(__x86_64__) || defined(__arm__) || defined(__aarch64__) \
     || defined(__powerpc64__))
   /* Use 2 MiB alignment so transparent hugepages can be used by KVM.
      Valgrind does not support alignments larger than 1 MiB,
      therefore we need special code which handles running on Valgrind. */
#  define QEMU_VMALLOC_ALIGN (512 * 4096)
#elif defined(__linux__) && defined(__s390x__)
   /* Use 1 MiB (segment size) alignment so gmap can be used by KVM. */
#  define QEMU_VMALLOC_ALIGN (256 * 4096)
#elif defined(__linux__) && defined(__sparc__)
#  define QEMU_VMALLOC_ALIGN MAX(qemu_real_host_page_size(), SHMLBA)
#elif defined(__linux__) && defined(__loongarch__)
   /*
    * For transparent hugepage optimization, it has better be huge page
    * aligned. LoongArch host system supports two kinds of pagesize: 4K
    * and 16K, here calculate huge page size from host page size
    */
#  define QEMU_VMALLOC_ALIGN (qemu_real_host_page_size() * \
                         qemu_real_host_page_size() / sizeof(long))
#else
#  define QEMU_VMALLOC_ALIGN qemu_real_host_page_size()
#endif

#ifdef CONFIG_POSIX
struct qemu_signalfd_siginfo {
    uint32_t ssi_signo;   /* Signal number */
    int32_t  ssi_errno;   /* Error number (unused) */
    int32_t  ssi_code;    /* Signal code */
    uint32_t ssi_pid;     /* PID of sender */
    uint32_t ssi_uid;     /* Real UID of sender */
    int32_t  ssi_fd;      /* File descriptor (SIGIO) */
    uint32_t ssi_tid;     /* Kernel timer ID (POSIX timers) */
    uint32_t ssi_band;    /* Band event (SIGIO) */
    uint32_t ssi_overrun; /* POSIX timer overrun count */
    uint32_t ssi_trapno;  /* Trap number that caused signal */
    int32_t  ssi_status;  /* Exit status or signal (SIGCHLD) */
    int32_t  ssi_int;     /* Integer sent by sigqueue(2) */
    uint64_t ssi_ptr;     /* Pointer sent by sigqueue(2) */
    uint64_t ssi_utime;   /* User CPU time consumed (SIGCHLD) */
    uint64_t ssi_stime;   /* System CPU time consumed (SIGCHLD) */
    uint64_t ssi_addr;    /* Address that generated signal
                             (for hardware-generated signals) */
    uint8_t  pad[48];     /* Pad size to 128 bytes (allow for
                             additional fields in the future) */
};

int qemu_signalfd(const sigset_t *mask);
void sigaction_invoke(struct sigaction *action,
                      struct qemu_signalfd_siginfo *info);
#endif

/*
 * Don't introduce new usage of this function, prefer the following
 * qemu_open/qemu_create that take an "Error **errp"
 */
int qemu_open_old(const char *name, int flags, ...);
int qemu_open(const char *name, int flags, Error **errp);
int qemu_create(const char *name, int flags, mode_t mode, Error **errp);
int qemu_close(int fd);
int qemu_unlink(const char *name);
#ifndef _WIN32
int qemu_dup_flags(int fd, int flags);
int qemu_dup(int fd);
int qemu_lock_fd(int fd, int64_t start, int64_t len, bool exclusive);
int qemu_unlock_fd(int fd, int64_t start, int64_t len);
int qemu_lock_fd_test(int fd, int64_t start, int64_t len, bool exclusive);
bool qemu_has_ofd_lock(void);
#endif

bool qemu_has_direct_io(void);

#if defined(__HAIKU__) && defined(__i386__)
#define FMT_pid "%ld"
#elif defined(WIN64)
#define FMT_pid "%" PRId64
#else
#define FMT_pid "%d"
#endif

bool qemu_write_pidfile(const char *pidfile, Error **errp);

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

ssize_t qemu_write_full(int fd, const void *buf, size_t count)
    G_GNUC_WARN_UNUSED_RESULT;

void qemu_set_cloexec(int fd);

/* Return a dynamically allocated directory path that is appropriate for storing
 * local state.
 *
 * The caller is responsible for releasing the value returned with g_free()
 * after use.
 */
char *qemu_get_local_state_dir(void);

/**
 * qemu_getauxval:
 * @type: the auxiliary vector key to lookup
 *
 * Search the auxiliary vector for @type, returning the value
 * or 0 if @type is not present.
 */
unsigned long qemu_getauxval(unsigned long type);

void qemu_set_tty_echo(int fd, bool echo);

typedef struct ThreadContext ThreadContext;

/**
 * qemu_prealloc_mem:
 * @fd: the fd mapped into the area, -1 for anonymous memory
 * @area: start address of the are to preallocate
 * @sz: the size of the area to preallocate
 * @max_threads: maximum number of threads to use
 * @tc: prealloc context threads pointer, NULL if not in use
 * @async: request asynchronous preallocation, requires @tc
 * @errp: returns an error if this function fails
 *
 * Preallocate memory (populate/prefault page tables writable) for the virtual
 * memory area starting at @area with the size of @sz. After a successful call,
 * each page in the area was faulted in writable at least once, for example,
 * after allocating file blocks for mapped files.
 *
 * When setting @async, allocation might be performed asynchronously.
 * qemu_finish_async_prealloc_mem() must be called to finish any asynchronous
 * preallocation.
 *
 * Return: true on success, else false setting @errp with error.
 */
bool qemu_prealloc_mem(int fd, char *area, size_t sz, int max_threads,
                       ThreadContext *tc, bool async, Error **errp);

/**
 * qemu_finish_async_prealloc_mem:
 * @errp: returns an error if this function fails
 *
 * Finish all outstanding asynchronous memory preallocation.
 *
 * Return: true on success, else false setting @errp with error.
 */
bool qemu_finish_async_prealloc_mem(Error **errp);

/**
 * qemu_get_pid_name:
 * @pid: pid of a process
 *
 * For given @pid fetch its name. Caller is responsible for
 * freeing the string when no longer needed.
 * Returns allocated string on success, NULL on failure.
 */
char *qemu_get_pid_name(pid_t pid);

/* Using intptr_t ensures that qemu_*_page_mask is sign-extended even
 * when intptr_t is 32-bit and we are aligning a long long.
 */
static inline uintptr_t qemu_real_host_page_size(void)
{
    return getpagesize();
}

static inline intptr_t qemu_real_host_page_mask(void)
{
    return -(intptr_t)qemu_real_host_page_size();
}

/*
 * After using getopt or getopt_long, if you need to parse another set
 * of options, then you must reset optind.  Unfortunately the way to
 * do this varies between implementations of getopt.
 */
static inline void qemu_reset_optind(void)
{
#ifdef HAVE_OPTRESET
    optind = 1;
    optreset = 1;
#else
    optind = 0;
#endif
}

int qemu_fdatasync(int fd);

/**
 * qemu_close_all_open_fd:
 *
 * Close all open file descriptors except the ones supplied in the @skip array
 *
 * @skip: ordered array of distinct file descriptors that should not be closed
 *        if any, or NULL.
 * @nskip: number of entries in the @skip array or 0 if @skip is NULL.
 */
void qemu_close_all_open_fd(const int *skip, unsigned int nskip);

/**
 * Sync changes made to the memory mapped file back to the backing
 * storage. For POSIX compliant systems this will fallback
 * to regular msync call. Otherwise it will trigger whole file sync
 * (including the metadata case there is no support to skip that otherwise)
 *
 * @addr   - start of the memory area to be synced
 * @length - length of the are to be synced
 * @fd     - file descriptor for the file to be synced
 *           (mandatory only for POSIX non-compliant systems)
 */
int qemu_msync(void *addr, size_t length, int fd);

/**
 * qemu_get_host_physmem:
 *
 * Operating system agnostic way of querying host memory.
 *
 * Returns amount of physical memory on the system. This is purely
 * advisery and may return 0 if we can't work it out. At the other
 * end we saturate to SIZE_MAX if you are lucky enough to have that
 * much memory.
 */
size_t qemu_get_host_physmem(void);

/*
 * Toggle write/execute on the pages marked MAP_JIT
 * for the current thread.
 */
#ifdef __APPLE__
static inline void qemu_thread_jit_execute(void)
{
    pthread_jit_write_protect_np(true);
}

static inline void qemu_thread_jit_write(void)
{
    pthread_jit_write_protect_np(false);
}
#else
static inline void qemu_thread_jit_write(void) {}
static inline void qemu_thread_jit_execute(void) {}
#endif

/**
 * Platforms which do not support system() return ENOSYS
 */
#ifndef HAVE_SYSTEM_FUNCTION
#define system platform_does_not_support_system
static inline int platform_does_not_support_system(const char *command)
{
    errno = ENOSYS;
    return -1;
}
#endif /* !HAVE_SYSTEM_FUNCTION */

#ifdef __cplusplus
}
#endif

#endif
