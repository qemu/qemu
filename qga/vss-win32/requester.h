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
#include "qemu/compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callback to set Error; used to avoid linking glib to the DLL */
typedef void (*ErrorSetFunc)(void **errp, int win32_err, int err_class,
                             const char *fmt, ...) GCC_FMT_ATTR(4, 5);
typedef struct ErrorSet {
    ErrorSetFunc error_set;
    void **errp;
    int err_class;
} ErrorSet;

STDAPI requester_init(void);
STDAPI requester_deinit(void);

typedef void (*QGAVSSRequesterFunc)(int *, ErrorSet *);
void requester_freeze(int *num_vols, ErrorSet *errset);
void requester_thaw(int *num_vols, ErrorSet *errset);

#ifdef __cplusplus
}
#endif

#endif
