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

struct qemu_signalfd_siginfo {
    uint32_t ssi_signo;
    uint8_t pad[124];
};

int qemu_signalfd(const sigset_t *mask);

int qemu_eventfd(int *fds);

#endif
