/*
 * Test Server
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QTEST_H
#define QTEST_H

#include "qemu-common.h"
#include "qapi/error.h"

extern bool qtest_allowed;

static inline bool qtest_enabled(void)
{
    return qtest_allowed;
}

bool qtest_driver(void);

int qtest_init_accel(MachineClass *mc);
void qtest_init(const char *qtest_chrdev, const char *qtest_log, Error **errp);

static inline int qtest_available(void)
{
#ifdef CONFIG_POSIX
    return 1;
#else
    return 0;
#endif
}

#endif
