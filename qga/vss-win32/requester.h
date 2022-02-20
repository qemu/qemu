/*
 * QEMU Guest Agent VSS requester declarations
 *
 * Copyright Hitachi Data Systems Corp. 2013
 *
 * Authors:
 *  Tomoki Sekiyama   <tomoki.sekiyama@hds.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef VSS_WIN32_REQUESTER_H
#define VSS_WIN32_REQUESTER_H

#include <basetyps.h>           /* STDAPI */

#ifdef __cplusplus
extern "C" {
#endif

struct Error;

/* Callback to set Error; used to avoid linking glib to the DLL */
typedef void (*ErrorSetFunc)(struct Error **errp,
                             const char *src, int line, const char *func,
                             int win32_err, const char *fmt, ...)
    G_GNUC_PRINTF(6, 7);
typedef struct ErrorSet {
    ErrorSetFunc error_setg_win32_wrapper;
    struct Error **errp;        /* restriction: must not be null */
} ErrorSet;

STDAPI requester_init(void);
STDAPI requester_deinit(void);

typedef struct volList volList;

struct volList {
    volList *next;
    char *value;
};

typedef void (*QGAVSSRequesterFunc)(int *, void *, ErrorSet *);
void requester_freeze(int *num_vols, void *volList, ErrorSet *errset);
void requester_thaw(int *num_vols, void *volList, ErrorSet *errset);

#ifdef __cplusplus
}
#endif

#endif
