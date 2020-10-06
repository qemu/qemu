/*
 * qtest stubs
 *
 * Copyright (c) 2014 Linaro Limited
 * Written by Peter Maydell
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/qtest.h"

/* Needed for qtest_allowed() */
bool qtest_allowed;

bool qtest_driver(void)
{
    return false;
}

int64_t qtest_get_virtual_clock(void)
{
    return 0;
}
