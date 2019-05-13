/*
 * systemd socket activation support
 *
 * Copyright 2017 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Richard W.M. Jones <rjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_SYSTEMD_H
#define QEMU_SYSTEMD_H

#define FIRST_SOCKET_ACTIVATION_FD 3 /* defined by systemd ABI */

/*
 * Check if socket activation was requested via use of the
 * LISTEN_FDS and LISTEN_PID environment variables.
 *
 * Returns 0 if no socket activation, or the number of FDs.
 */
unsigned int check_socket_activation(void);

#endif
