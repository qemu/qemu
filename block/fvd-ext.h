/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this header file contains functions of the FVD block
 *  device driver that are used by other external modules. These functions are
 *  mainly for testing and debugging urposes.
 *============================================================================*/

#ifndef __fvd_debug_h__
#define __fvd_debug_h__

//#define FVD_DEBUG

int fvd_get_copy_on_read (BlockDriverState *bs);
void fvd_set_copy_on_read (BlockDriverState *bs, int copy_on_read);
void fvd_check_memory_usage (void);
void fvd_init_prefetch(void * bs);
void fvd_enable_host_crash_test (void);

#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

#ifndef FVD_DEBUG
# define QDEBUG(format,...) do {} while (0)
# define ASSERT(x) do {} while (0)
# define FVD_DEBUG_ACB(...) do {} while (0)
# define QPAUSE(...) do {} while (0)

#else

extern FILE *__fvd_debug_fp;
void init_fvd_debug_fp (void);
void FVD_DEBUG_ACB (void *p);
# define QDEBUG(format,...) \
    do { \
        if (__fvd_debug_fp==NULL) init_fvd_debug_fp(); \
        fprintf (__fvd_debug_fp, format, ##__VA_ARGS__); \
        fflush(__fvd_debug_fp); \
    } while(0)

# define ASSERT(x) \
    do { \
        if (!(x)) { \
            fprintf (stderr, "Assertion failed in process %d at %s:%d. " \
                "Waiting for debugging...\n", getpid(),__FILE__, __LINE__); \
            fgetc (stdin); exit (1);  \
        } \
    } while (0) \

# define QPAUSE(format,...) \
    do { \
        printf (format, ##__VA_ARGS__); \
        printf (" Pause process %d for debugging...\n", getpid()); \
        fgetc (stdin); \
    } while (0)

#endif

#endif
