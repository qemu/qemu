/*
 * safe-syscall-error.c: errno setting fragment
 * This is intended to be invoked by safe-syscall.S
 *
 * Written by Richard Henderson <rth@twiddle.net>
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "user/safe-syscall.h"

/*
 * This is intended to be invoked via tail-call on the error path
 * from the assembly in host/arch/safe-syscall.inc.S.  This takes
 * care of the host specific addressing of errno.
 * Return -1 to finalize the return value for safe_syscall_base.
 */
long safe_syscall_set_errno_tail(int value)
{
    errno = value;
    return -1;
}
