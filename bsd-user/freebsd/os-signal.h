/*
 * FreeBSD signal system call shims
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FREEBSD_OS_SIGNAL_H
#define FREEBSD_OS_SIGNAL_H

#include <sys/procdesc.h>

/* pdkill(2) */
static inline abi_long do_freebsd_pdkill(abi_long arg1, abi_long arg2)
{

    return get_errno(pdkill(arg1, target_to_host_signal(arg2)));
}

#endif /* FREEBSD_OS_SIGNAL_H */
