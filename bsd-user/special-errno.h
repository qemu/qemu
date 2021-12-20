/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * QEMU internal errno values for implementing user-only POSIX.
 *
 *  Copyright (c) 2021 Linaro, Ltd.
 */

#ifndef SPECIAL_ERRNO_H
#define SPECIAL_ERRNO_H

/*
 * All of these are QEMU internal, not visible to the guest.
 * They should be chosen so as to not overlap with any host
 * or guest errno.
 */

/*
 * This is returned when a system call should be restarted, to tell the
 * main loop that it should wind the guest PC backwards so it will
 * re-execute the syscall after handling any pending signals.
 */
#define QEMU_ERESTARTSYS  255

#endif /* SPECIAL_ERRNO_H */
