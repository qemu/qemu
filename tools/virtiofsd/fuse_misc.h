/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include <pthread.h>

/*
 * Versioned symbols cannot be used in some cases because it
 *   - confuse the dynamic linker in uClibc
 *   - not supported on MacOSX (in MachO binary format)
 */
#if (!defined(__UCLIBC__) && !defined(__APPLE__))
#define FUSE_SYMVER(x) __asm__(x)
#else
#define FUSE_SYMVER(x)
#endif

#ifndef USE_UCLIBC
#define fuse_mutex_init(mut) pthread_mutex_init(mut, NULL)
#else
/* Is this hack still needed? */
static inline void fuse_mutex_init(pthread_mutex_t *mut)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
    pthread_mutex_init(mut, &attr);
    pthread_mutexattr_destroy(&attr);
}
#endif

#ifdef HAVE_STRUCT_STAT_ST_ATIM
/* Linux */
#define ST_ATIM_NSEC(stbuf) ((stbuf)->st_atim.tv_nsec)
#define ST_CTIM_NSEC(stbuf) ((stbuf)->st_ctim.tv_nsec)
#define ST_MTIM_NSEC(stbuf) ((stbuf)->st_mtim.tv_nsec)
#define ST_ATIM_NSEC_SET(stbuf, val) (stbuf)->st_atim.tv_nsec = (val)
#define ST_CTIM_NSEC_SET(stbuf, val) (stbuf)->st_ctim.tv_nsec = (val)
#define ST_MTIM_NSEC_SET(stbuf, val) (stbuf)->st_mtim.tv_nsec = (val)
#elif defined(HAVE_STRUCT_STAT_ST_ATIMESPEC)
/* FreeBSD */
#define ST_ATIM_NSEC(stbuf) ((stbuf)->st_atimespec.tv_nsec)
#define ST_CTIM_NSEC(stbuf) ((stbuf)->st_ctimespec.tv_nsec)
#define ST_MTIM_NSEC(stbuf) ((stbuf)->st_mtimespec.tv_nsec)
#define ST_ATIM_NSEC_SET(stbuf, val) (stbuf)->st_atimespec.tv_nsec = (val)
#define ST_CTIM_NSEC_SET(stbuf, val) (stbuf)->st_ctimespec.tv_nsec = (val)
#define ST_MTIM_NSEC_SET(stbuf, val) (stbuf)->st_mtimespec.tv_nsec = (val)
#else
#define ST_ATIM_NSEC(stbuf) 0
#define ST_CTIM_NSEC(stbuf) 0
#define ST_MTIM_NSEC(stbuf) 0
#define ST_ATIM_NSEC_SET(stbuf, val) do { } while (0)
#define ST_CTIM_NSEC_SET(stbuf, val) do { } while (0)
#define ST_MTIM_NSEC_SET(stbuf, val) do { } while (0)
#endif
