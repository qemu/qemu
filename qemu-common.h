/* Common header file that is included by all of qemu.  */
#ifndef QEMU_COMMON_H
#define QEMU_COMMON_H

#include "config-host.h"

#define QEMU_NORETURN __attribute__ ((__noreturn__))
#ifdef CONFIG_GCC_ATTRIBUTE_WARN_UNUSED_RESULT
#define QEMU_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define QEMU_WARN_UNUSED_RESULT
#endif

#define QEMU_BUILD_BUG_ON(x) typedef char __build_bug_on__##__LINE__[(x)?-1:1];
#define TFR(expr) do { if ((expr) != -1) break; } while (errno == EINTR)

typedef struct QEMUTimer QEMUTimer;
typedef struct QEMUFile QEMUFile;
typedef struct QEMUBH QEMUBH;
typedef struct DeviceState DeviceState;

struct Monitor;
typedef struct Monitor Monitor;

/* we put basic includes here to avoid repeating them in device drivers */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <assert.h>

#ifdef _WIN32
#include "qemu-os-win32.h"
#endif

#ifdef CONFIG_POSIX
#include "qemu-os-posix.h"
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
#ifndef TIME_MAX
#define TIME_MAX LONG_MAX
#endif

#ifndef CONFIG_IOVEC
#define CONFIG_IOVEC
struct iovec {
    void *iov_base;
    size_t iov_len;
};
/*
 * Use the same value as Linux for now.
 */
#define IOV_MAX		1024
#else
#include <sys/uio.h>
#endif

#if defined __GNUC__
# if (__GNUC__ < 4) || \
     defined(__GNUC_MINOR__) && (__GNUC__ == 4) && (__GNUC_MINOR__ < 4)
   /* gcc versions before 4.4.x don't support gnu_printf, so use printf. */
#  define GCC_ATTR __attribute__((__unused__, format(printf, 1, 2)))
#  define GCC_FMT_ATTR(n, m) __attribute__((format(printf, n, m)))
# else
   /* Use gnu_printf when supported (qemu uses standard format strings). */
#  define GCC_ATTR __attribute__((__unused__, format(gnu_printf, 1, 2)))
#  define GCC_FMT_ATTR(n, m) __attribute__((format(gnu_printf, n, m)))
# endif
#else
#define GCC_ATTR /**/
#define GCC_FMT_ATTR(n, m)
#endif

typedef int (*fprintf_function)(FILE *f, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

#ifdef _WIN32
#define fsync _commit
#define lseek _lseeki64
int qemu_ftruncate64(int, int64_t);
#define ftruncate qemu_ftruncate64

static inline char *realpath(const char *path, char *resolved_path)
{
    _fullpath(resolved_path, path, _MAX_PATH);
    return resolved_path;
}

#define PRId64 "I64d"
#define PRIx64 "I64x"
#define PRIu64 "I64u"
#define PRIo64 "I64o"
#endif

/* FIXME: Remove NEED_CPU_H.  */
#ifndef NEED_CPU_H

#include <setjmp.h>
#include "osdep.h"
#include "bswap.h"

#else

#include "cpu.h"

#endif /* !defined(NEED_CPU_H) */

/* bottom halves */
typedef void QEMUBHFunc(void *opaque);

void async_context_push(void);
void async_context_pop(void);
int get_async_context_id(void);

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque);
void qemu_bh_schedule(QEMUBH *bh);
/* Bottom halfs that are scheduled from a bottom half handler are instantly
 * invoked.  This can create an infinite loop if a bottom half handler
 * schedules itself.  qemu_bh_schedule_idle() avoids this infinite loop by
 * ensuring that the bottom half isn't executed until the next main loop
 * iteration.
 */
void qemu_bh_schedule_idle(QEMUBH *bh);
void qemu_bh_cancel(QEMUBH *bh);
void qemu_bh_delete(QEMUBH *bh);
int qemu_bh_poll(void);
void qemu_bh_update_timeout(int *timeout);

void qemu_get_timedate(struct tm *tm, int offset);
int qemu_timedate_diff(struct tm *tm);

/* cutils.c */
void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
int strstart(const char *str, const char *val, const char **ptr);
int stristart(const char *str, const char *val, const char **ptr);
int qemu_strnlen(const char *s, int max_len);
time_t mktimegm(struct tm *tm);
int qemu_fls(int i);
int qemu_fdatasync(int fd);
int fcntl_setfl(int fd, int flag);

/*
 * strtosz() suffixes used to specify the default treatment of an
 * argument passed to strtosz() without an explicit suffix.
 * These should be defined using upper case characters in the range
 * A-Z, as strtosz() will use qemu_toupper() on the given argument
 * prior to comparison.
 */
#define STRTOSZ_DEFSUFFIX_TB	'T'
#define STRTOSZ_DEFSUFFIX_GB	'G'
#define STRTOSZ_DEFSUFFIX_MB	'M'
#define STRTOSZ_DEFSUFFIX_KB	'K'
#define STRTOSZ_DEFSUFFIX_B	'B'
int64_t strtosz(const char *nptr, char **end);
int64_t strtosz_suffix(const char *nptr, char **end, const char default_suffix);

/* path.c */
void init_paths(const char *prefix);
const char *path(const char *pathname);

#define qemu_isalnum(c)		isalnum((unsigned char)(c))
#define qemu_isalpha(c)		isalpha((unsigned char)(c))
#define qemu_iscntrl(c)		iscntrl((unsigned char)(c))
#define qemu_isdigit(c)		isdigit((unsigned char)(c))
#define qemu_isgraph(c)		isgraph((unsigned char)(c))
#define qemu_islower(c)		islower((unsigned char)(c))
#define qemu_isprint(c)		isprint((unsigned char)(c))
#define qemu_ispunct(c)		ispunct((unsigned char)(c))
#define qemu_isspace(c)		isspace((unsigned char)(c))
#define qemu_isupper(c)		isupper((unsigned char)(c))
#define qemu_isxdigit(c)	isxdigit((unsigned char)(c))
#define qemu_tolower(c)		tolower((unsigned char)(c))
#define qemu_toupper(c)		toupper((unsigned char)(c))
#define qemu_isascii(c)		isascii((unsigned char)(c))
#define qemu_toascii(c)		toascii((unsigned char)(c))

#ifdef _WIN32
/* ffs() in oslib-win32.c for WIN32, strings.h for the rest of the world */
int ffs(int i);
#endif

void *qemu_oom_check(void *ptr);
void *qemu_malloc(size_t size);
void *qemu_realloc(void *ptr, size_t size);
void *qemu_mallocz(size_t size);
void qemu_free(void *ptr);
char *qemu_strdup(const char *str);
char *qemu_strndup(const char *str, size_t size);

void qemu_mutex_lock_iothread(void);
void qemu_mutex_unlock_iothread(void);

int qemu_open(const char *name, int flags, ...);
ssize_t qemu_write_full(int fd, const void *buf, size_t count)
    QEMU_WARN_UNUSED_RESULT;
void qemu_set_cloexec(int fd);

#ifndef _WIN32
int qemu_add_child_watch(pid_t pid);
int qemu_eventfd(int pipefd[2]);
int qemu_pipe(int pipefd[2]);
#endif

/* Error handling.  */

void QEMU_NORETURN hw_error(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

/* IO callbacks.  */
typedef void IOReadHandler(void *opaque, const uint8_t *buf, int size);
typedef int IOCanReadHandler(void *opaque);
typedef void IOHandler(void *opaque);

void qemu_iohandler_fill(int *pnfds, fd_set *readfds, fd_set *writefds, fd_set *xfds);
void qemu_iohandler_poll(fd_set *readfds, fd_set *writefds, fd_set *xfds, int rc);

struct ParallelIOArg {
    void *buffer;
    int count;
};

typedef int (*DMA_transfer_handler) (void *opaque, int nchan, int pos, int size);

/* A load of opaque types so that device init declarations don't have to
   pull in all the real definitions.  */
typedef struct NICInfo NICInfo;
typedef struct HCIInfo HCIInfo;
typedef struct AudioState AudioState;
typedef struct BlockDriverState BlockDriverState;
typedef struct DriveInfo DriveInfo;
typedef struct DisplayState DisplayState;
typedef struct DisplayChangeListener DisplayChangeListener;
typedef struct DisplaySurface DisplaySurface;
typedef struct DisplayAllocator DisplayAllocator;
typedef struct PixelFormat PixelFormat;
typedef struct TextConsole TextConsole;
typedef TextConsole QEMUConsole;
typedef struct CharDriverState CharDriverState;
typedef struct MACAddr MACAddr;
typedef struct VLANState VLANState;
typedef struct VLANClientState VLANClientState;
typedef struct i2c_bus i2c_bus;
typedef struct i2c_slave i2c_slave;
typedef struct SMBusDevice SMBusDevice;
typedef struct PCIHostState PCIHostState;
typedef struct PCIExpressHost PCIExpressHost;
typedef struct PCIBus PCIBus;
typedef struct PCIDevice PCIDevice;
typedef struct PCIExpressDevice PCIExpressDevice;
typedef struct PCIBridge PCIBridge;
typedef struct PCIEAERMsg PCIEAERMsg;
typedef struct PCIEAERLog PCIEAERLog;
typedef struct PCIEAERErr PCIEAERErr;
typedef struct PCIEPort PCIEPort;
typedef struct PCIESlot PCIESlot;
typedef struct SerialState SerialState;
typedef struct IRQState *qemu_irq;
typedef struct PCMCIACardState PCMCIACardState;
typedef struct MouseTransformInfo MouseTransformInfo;
typedef struct uWireSlave uWireSlave;
typedef struct I2SCodec I2SCodec;
typedef struct SSIBus SSIBus;
typedef struct EventNotifier EventNotifier;
typedef struct VirtIODevice VirtIODevice;

typedef uint64_t pcibus_t;

void cpu_exec_init_all(unsigned long tb_size);

/* CPU save/load.  */
void cpu_save(QEMUFile *f, void *opaque);
int cpu_load(QEMUFile *f, void *opaque, int version_id);

/* Force QEMU to stop what it's doing and service IO */
void qemu_service_io(void);

/* Force QEMU to process pending events */
void qemu_notify_event(void);

/* Unblock cpu */
void qemu_cpu_kick(void *env);
void qemu_cpu_kick_self(void);
int qemu_cpu_is_self(void *env);
bool all_cpu_threads_idle(void);

/* work queue */
struct qemu_work_item {
    struct qemu_work_item *next;
    void (*func)(void *data);
    void *data;
    int done;
};

#ifdef CONFIG_USER_ONLY
#define qemu_init_vcpu(env) do { } while (0)
#else
void qemu_init_vcpu(void *env);
#endif

typedef struct QEMUIOVector {
    struct iovec *iov;
    int niov;
    int nalloc;
    size_t size;
} QEMUIOVector;

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint);
void qemu_iovec_init_external(QEMUIOVector *qiov, struct iovec *iov, int niov);
void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len);
void qemu_iovec_copy(QEMUIOVector *dst, QEMUIOVector *src, uint64_t skip,
    size_t size);
void qemu_iovec_concat(QEMUIOVector *dst, QEMUIOVector *src, size_t size);
void qemu_iovec_destroy(QEMUIOVector *qiov);
void qemu_iovec_reset(QEMUIOVector *qiov);
void qemu_iovec_to_buffer(QEMUIOVector *qiov, void *buf);
void qemu_iovec_from_buffer(QEMUIOVector *qiov, const void *buf, size_t count);
void qemu_iovec_memset(QEMUIOVector *qiov, int c, size_t count);
void qemu_iovec_memset_skip(QEMUIOVector *qiov, int c, size_t count,
                            size_t skip);

void qemu_progress_init(int enabled, float min_skip);
void qemu_progress_end(void);
void qemu_progress_print(float percent, int max);

#define QEMU_FILE_TYPE_BIOS   0
#define QEMU_FILE_TYPE_KEYMAP 1
char *qemu_find_file(int type, const char *name);

/* OS specific functions */
void os_setup_early_signal_handling(void);
char *os_find_datadir(const char *argv0);
void os_parse_cmd_args(int index, const char *optarg);
void os_pidfile_error(void);

/* Convert a byte between binary and BCD.  */
static inline uint8_t to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static inline uint8_t from_bcd(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0f);
}

/* compute with 96 bit intermediate result: (a*b)/c */
static inline uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c)
{
    union {
        uint64_t ll;
        struct {
#ifdef HOST_WORDS_BIGENDIAN
            uint32_t high, low;
#else
            uint32_t low, high;
#endif
        } l;
    } u, res;
    uint64_t rl, rh;

    u.ll = a;
    rl = (uint64_t)u.l.low * (uint64_t)b;
    rh = (uint64_t)u.l.high * (uint64_t)b;
    rh += (rl >> 32);
    res.l.high = rh / c;
    res.l.low = (((rh % c) << 32) + (rl & 0xffffffff)) / c;
    return res.ll;
}

#include "module.h"

#endif
