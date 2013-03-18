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

#if !defined(CONFIG_USER_ONLY)
extern bool qtest_allowed;
extern const char *qtest_chrdev;
extern const char *qtest_log;

static inline bool qtest_enabled(void)
{
    return qtest_allowed;
}

static inline int qtest_available(void)
{
    return 1;
}

int qtest_init(void);
#else
static inline bool qtest_enabled(void)
{
    return false;
}

static inline int qtest_available(void)
{
    return 0;
}

static inline int qtest_init(void)
{
    return 0;
}

#endif

#endif
