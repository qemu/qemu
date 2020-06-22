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

#include "config-host.h"
#ifdef NEED_CPU_H
#include "config-target.h"
#else
#include "exec/poison.h"
#endif

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
extern int daemon(int, int);
#endif

#ifdef _WIN32
/* as defined in sdkddkver.h */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 /* Vista */
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

#ifdef __OpenBSD__
#include <sys/signal.h>
#endif

#ifndef _WIN32
#include <sys/wait.h>
#else
#define WIFEXITED(x)   1
#define WEXITSTATUS(x) (x)
#endif

#ifdef _WIN32
#include "sysemu/os-win32.h"
#endif

#ifdef CONFIG_POSIX
#include "sysemu/os-posix.h"
#endif

#include "glib-compat.h"
#include "qemu/typedefs.h"

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

/* HOST_LONG_BITS is the size of a native pointer in bits. */
#if UINTPTR_MAX == UINT32_MAX
# define HOST_LONG_BITS 32
#elif UINTPTR_MAX == UINT64_MAX
# define HOST_LONG_BITS 64
#else
# error Unknown pointer size
#endif

/* Mac OSX has a <stdint.h> bug that incorrectly defines SIZE_MAX with
 * the wrong type. Our replacement isn't usable in preprocessor
 * expressions, but it is sufficient for our needs. */
#if defined(HAVE_BROKEN_SIZE_MAX) && HAVE_BROKEN_SIZE_MAX
#undef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

/* Minimum function that returns zero only iff both values are zero.
 * Intended for use with unsigned values only. */
#ifndef MIN_NON_ZERO
#define MIN_NON_ZERO(a, b) ((a) == 0 ? (b) : \
                                ((b) == 0 ? (a) : (MIN(a, b))))
#endif

/* Round number down to multiple */
#define QEMU_ALIGN_DOWN(n, m) ((n) / (m) * (m))

/* Round number up to multiple. Safe when m is not a power of 2 (see
 * ROUND_UP for a faster version when a power of 2 is guaranteed) */
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

/* Round number up to multiple. Requires that d be a power of 2 (see
 * QEMU_ALIGN_UP for a safer but slower version on arbitrary
 * numbers); works even if d is a smaller type than n.  */
#ifndef ROUND_UP
#define ROUND_UP(n, d) (((n) + (d) - 1) & -(0 ? (n) : (d)))
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
void *qemu_try_memalign(size_t alignment, size_t size);
void *qemu_memalign(size_t alignment, size_t size);
void *qemu_anon_ram_alloc(size_t size, uint64_t *align, bool shared);
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
#ifdef MADV_UNMERGEABLE
#define QEMU_MADV_UNMERGEABLE MADV_UNMERGEABLE
#else
#define QEMU_MADV_UNMERGEABLE QEMU_MADV_INVALID
#endif
#ifdef MADV_DODUMP
#define QEMU_MADV_DODUMP MADV_DODUMP
#else
#define QEMU_MADV_DODUMP QEMU_MADV_INVALID
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
#ifdef MADV_NOHUGEPAGE
#define QEMU_MADV_NOHUGEPAGE MADV_NOHUGEPAGE
#else
#define QEMU_MADV_NOHUGEPAGE QEMU_MADV_INVALID
#endif
#ifdef MADV_REMOVE
#define QEMU_MADV_REMOVE MADV_REMOVE
#else
#define QEMU_MADV_REMOVE QEMU_MADV_INVALID
#endif

#elif defined(CONFIG_POSIX_MADVISE)

#define QEMU_MADV_WILLNEED  POSIX_MADV_WILLNEED
#define QEMU_MADV_DONTNEED  POSIX_MADV_DONTNEED
#define QEMU_MADV_DONTFORK  QEMU_MADV_INVALID
#define QEMU_MADV_MERGEABLE QEMU_MADV_INVALID
#define QEMU_MADV_UNMERGEABLE QEMU_MADV_INVALID
#define QEMU_MADV_DODUMP QEMU_MADV_INVALID
#define QEMU_MADV_DONTDUMP QEMU_MADV_INVALID
#define QEMU_MADV_HUGEPAGE  QEMU_MADV_INVALID
#define QEMU_MADV_NOHUGEPAGE  QEMU_MADV_INVALID
#define QEMU_MADV_REMOVE QEMU_MADV_INVALID

#else /* no-op */

#define QEMU_MADV_WILLNEED  QEMU_MADV_INVALID
#define QEMU_MADV_DONTNEED  QEMU_MADV_INVALID
#define QEMU_MADV_DONTFORK  QEMU_MADV_INVALID
#define QEMU_MADV_MERGEABLE QEMU_MADV_INVALID
#define QEMU_MADV_UNMERGEABLE QEMU_MADV_INVALID
#define QEMU_MADV_DODUMP QEMU_MADV_INVALID
#define QEMU_MADV_DONTDUMP QEMU_MADV_INVALID
#define QEMU_MADV_HUGEPAGE  QEMU_MADV_INVALID
#define QEMU_MADV_NOHUGEPAGE  QEMU_MADV_INVALID
#define QEMU_MADV_REMOVE QEMU_MADV_INVALID

#endif

#ifdef _WIN32
#define HAVE_CHARDEV_SERIAL 1
#elif defined(__linux__) || defined(__sun__) || defined(__FreeBSD__)    \
    || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) \
    || defined(__GLIBC__)
#define HAVE_CHARDEV_SERIAL 1
#endif

#if defined(__linux__) || defined(__FreeBSD__) ||               \
    defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#define HAVE_CHARDEV_PARPORT 1
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
#include <sys/shm.h>
#  define QEMU_VMALLOC_ALIGN MAX(qemu_real_host_page_size, SHMLBA)
#else
#  define QEMU_VMALLOC_ALIGN qemu_real_host_page_size
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

int qemu_madvise(void *addr, size_t len, int advice);
int qemu_mprotect_rwx(void *addr, size_t size);
int qemu_mprotect_none(void *addr, size_t size);

int qemu_open(const char *name, int flags, ...);
int qemu_close(int fd);
int qemu_unlink(const char *name);
#ifndef _WIN32
int qemu_dup(int fd);
#endif
int qemu_lock_fd(int fd, int64_t start, int64_t len, bool exclusive);
int qemu_unlock_fd(int fd, int64_t start, int64_t len);
int qemu_lock_fd_test(int fd, int64_t start, int64_t len, bool exclusive);
bool qemu_has_ofd_lock(void);

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

/* Starting on QEMU 2.5, qemu_hw_version() returns "2.5+" by default
 * instead of QEMU_VERSION, so setting hw_version on MachineClass
 * is no longer mandatory.
 *
 * Do NOT change this string, or it will break compatibility on all
 * machine classes that don't set hw_version.
 */
#define QEMU_HW_VERSION "2.5+"

/* QEMU "hardware version" setting. Used to replace code that exposed
 * QEMU_VERSION to guests in the past and need to keep compatibility.
 * Do not use qemu_hw_version() in new code.
 */
void qemu_set_hw_version(const char *);
const char *qemu_hw_version(void);

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
unsigned long qemu_getauxval(unsigned long type);

void qemu_set_tty_echo(int fd, bool echo);

void os_mem_prealloc(int fd, char *area, size_t sz, int smp_cpus,
                     Error **errp);

/**
 * qemu_get_pid_name:
 * @pid: pid of a process
 *
 * For given @pid fetch its name. Caller is responsible for
 * freeing the string when no longer needed.
 * Returns allocated string on success, NULL on failure.
 */
char *qemu_get_pid_name(pid_t pid);

/**
 * qemu_fork:
 *
 * A version of fork that avoids signal handler race
 * conditions that can lead to child process getting
 * signals that are otherwise only expected by the
 * parent. It also resets all signal handlers to the
 * default settings.
 *
 * Returns 0 to child process, pid number to parent
 * or -1 on failure.
 */
pid_t qemu_fork(Error **errp);

/* Using intptr_t ensures that qemu_*_page_mask is sign-extended even
 * when intptr_t is 32-bit and we are aligning a long long.
 */
extern uintptr_t qemu_real_host_page_size;
extern intptr_t qemu_real_host_page_mask;

extern int qemu_icache_linesize;
extern int qemu_icache_linesize_log;
extern int qemu_dcache_linesize;
extern int qemu_dcache_linesize_log;

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

/**
 * qemu_get_host_name:
 * @errp: Error object
 *
 * Operating system agnostic way of querying host name.
 *
 * Returns allocated hostname (caller should free), NULL on failure.
 */
char *qemu_get_host_name(Error **errp);

#endif
