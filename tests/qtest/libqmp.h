/*
 * libqmp test unit
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 * Copyright SUSE LINUX Products GmbH 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *  Andreas Färber    <afaerber@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef LIBQMP_H
#define LIBQMP_H

#include "qobject/qdict.h"

QDict *qmp_fd_receive(int fd);
#ifndef _WIN32
void qmp_fd_vsend_fds(int fd, int *fds, size_t fds_num,
                      const char *fmt, va_list ap) G_GNUC_PRINTF(4, 0);
#endif
void qmp_fd_vsend(int fd, const char *fmt, va_list ap) G_GNUC_PRINTF(2, 0);
void qmp_fd_send(int fd, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void qmp_fd_send_raw(int fd, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void qmp_fd_vsend_raw(int fd, const char *fmt, va_list ap) G_GNUC_PRINTF(2, 0);
QDict *qmp_fdv(int fd, const char *fmt, va_list ap) G_GNUC_PRINTF(2, 0);
QDict *qmp_fd(int fd, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

/**
 * qmp_rsp_is_err:
 * @rsp: QMP response to check for error
 *
 * Test @rsp for error and discard @rsp.
 * Returns 'true' if there is error in @rsp and 'false' otherwise.
 */
bool qmp_rsp_is_err(QDict *rsp);

/**
 * qmp_expect_error_and_unref:
 * @rsp: QMP response to check for error
 * @class: an error class
 *
 * Assert the response has the given error class and discard @rsp.
 */
void qmp_expect_error_and_unref(QDict *rsp, const char *class);

#endif /* LIBQMP_H */
