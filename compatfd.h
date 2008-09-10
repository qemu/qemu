/*
 * signalfd/eventfd compatibility
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_COMPATFD_H
#define QEMU_COMPATFD_H

#include <signal.h>

#if defined(__linux__) && !defined(SYS_signalfd)
struct signalfd_siginfo {
    uint32_t ssi_signo;
    uint8_t pad[124];
};
#else
#include <linux/signalfd.h>
#endif

int qemu_signalfd(const sigset_t *mask);

int qemu_eventfd(int *fds);

#endif
