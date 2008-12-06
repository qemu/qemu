/* Common header file that is included by all of qemu.  */
#ifndef QEMU_COMMON_H
#define QEMU_COMMON_H

/* we put basic includes here to avoid repeating them in device drivers */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
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
#include "config-host.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef ENOMEDIUM
#define ENOMEDIUM ENODEV
#endif

#ifndef HAVE_IOVEC
#define HAVE_IOVEC
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#else
#include <sys/uio.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0501  /* needed for ipv6 bits */
#include <windows.h>
#define fsync _commit
#define lseek _lseeki64
#define ENOTSUP 4096
extern int qemu_ftruncate64(int, int64_t);
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
typedef struct QEMUBH QEMUBH;

typedef void QEMUBHFunc(void *opaque);

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

uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c);

void qemu_get_timedate(struct tm *tm, int offset);
int qemu_timedate_diff(struct tm *tm);

/* cutils.c */
void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
int strstart(const char *str, const char *val, const char **ptr);
int stristart(const char *str, const char *val, const char **ptr);
time_t mktimegm(struct tm *tm);
int fls(int i);

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

void *qemu_malloc(size_t size);
void *qemu_realloc(void *ptr, size_t size);
void *qemu_mallocz(size_t size);
void qemu_free(void *ptr);
char *qemu_strdup(const char *str);
char *qemu_strndup(const char *str, size_t size);

void *get_mmap_addr(unsigned long size);


/* Error handling.  */

void hw_error(const char *fmt, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)))
    __attribute__ ((__noreturn__));

/* IO callbacks.  */
typedef void IOReadHandler(void *opaque, const uint8_t *buf, int size);
typedef int IOCanRWHandler(void *opaque);
typedef void IOHandler(void *opaque);

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
typedef struct DisplayState DisplayState;
typedef struct TextConsole TextConsole;
typedef TextConsole QEMUConsole;
typedef struct CharDriverState CharDriverState;
typedef struct VLANState VLANState;
typedef struct QEMUFile QEMUFile;
typedef struct i2c_bus i2c_bus;
typedef struct i2c_slave i2c_slave;
typedef struct SMBusDevice SMBusDevice;
typedef struct QEMUTimer QEMUTimer;
typedef struct PCIBus PCIBus;
typedef struct PCIDevice PCIDevice;
typedef struct SerialState SerialState;
typedef struct IRQState *qemu_irq;
struct pcmcia_card_s;

/* CPU save/load.  */
void cpu_save(QEMUFile *f, void *opaque);
int cpu_load(QEMUFile *f, void *opaque, int version_id);

/* Force QEMU to stop what it's doing and service IO */
void qemu_service_io(void);

#endif
