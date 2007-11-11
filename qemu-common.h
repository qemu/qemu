/* Common header file that is included by all of qemu.  */
#ifndef QEMU_COMMON_H
#define QEMU_COMMON_H

/* we put basic includes here to avoid repeating them in device drivers */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef ENOMEDIUM
#define ENOMEDIUM ENODEV
#endif

#ifdef _WIN32
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

#include "config-host.h"
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
void qemu_bh_cancel(QEMUBH *bh);
void qemu_bh_delete(QEMUBH *bh);
int qemu_bh_poll(void);

/* cutils.c */
void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
int strstart(const char *str, const char *val, const char **ptr);
int stristart(const char *str, const char *val, const char **ptr);
time_t mktimegm(struct tm *tm);

#endif
