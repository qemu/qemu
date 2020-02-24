/*
 * QOS-assisted fuzzing helpers
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef _QOS_FUZZ_H_
#define _QOS_FUZZ_H_

#include "tests/qtest/fuzz/fuzz.h"
#include "tests/qtest/libqos/qgraph.h"

int qos_fuzz(const unsigned char *Data, size_t Size);
void qos_setup(void);

extern void *fuzz_qos_obj;
extern QGuestAllocator *fuzz_qos_alloc;

void fuzz_add_qos_target(
        FuzzTarget *fuzz_opts,
        const char *interface,
        QOSGraphTestOptions *opts
        );

void qos_init_path(QTestState *);

#endif
