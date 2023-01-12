/*
 * QTest
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
 */

#include "qemu/osdep.h"

#include "libqmp.h"

#ifndef _WIN32
#include <sys/socket.h>
#endif

#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qjson.h"

#define SOCKET_MAX_FDS 16

typedef struct {
    JSONMessageParser parser;
    QDict *response;
} QMPResponseParser;

static void socket_send(int fd, const char *buf, size_t size)
{
    ssize_t res = qemu_send_full(fd, buf, size);

    assert(res == size);
}

static void qmp_response(void *opaque, QObject *obj, Error *err)
{
    QMPResponseParser *qmp = opaque;

    assert(!obj != !err);

    if (err) {
        error_prepend(&err, "QMP JSON response parsing failed: ");
        error_report_err(err);
        abort();
    }

    g_assert(!qmp->response);
    qmp->response = qobject_to(QDict, obj);
    g_assert(qmp->response);
}

QDict *qmp_fd_receive(int fd)
{
    QMPResponseParser qmp;
    bool log = getenv("QTEST_LOG") != NULL;

    qmp.response = NULL;
    json_message_parser_init(&qmp.parser, qmp_response, &qmp, NULL);
    while (!qmp.response) {
        ssize_t len;
        char c;

        len = recv(fd, &c, 1, 0);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        if (len == -1 || len == 0) {
            fprintf(stderr, "Broken pipe\n");
            abort();
        }

        if (log) {
            g_assert(write(2, &c, 1) == 1);
        }
        json_message_parser_feed(&qmp.parser, &c, 1);
    }
    if (log) {
        g_assert(write(2, "\n", 1) == 1);
    }
    json_message_parser_destroy(&qmp.parser);

    return qmp.response;
}

#ifndef _WIN32
/* Sends a message and file descriptors to the socket.
 * It's needed for qmp-commands like getfd/add-fd */
static void socket_send_fds(int socket_fd, int *fds, size_t fds_num,
                            const char *buf, size_t buf_size)
{
    ssize_t ret;
    struct msghdr msg = { 0 };
    char control[CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS)] = { 0 };
    size_t fdsize = sizeof(int) * fds_num;
    struct cmsghdr *cmsg;
    struct iovec iov = { .iov_base = (char *)buf, .iov_len = buf_size };

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (fds && fds_num > 0) {
        g_assert_cmpuint(fds_num, <, SOCKET_MAX_FDS);

        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(fdsize);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(fdsize);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), fds, fdsize);
    }

    do {
        ret = sendmsg(socket_fd, &msg, 0);
    } while (ret < 0 && errno == EINTR);
    g_assert_cmpint(ret, >, 0);
}
#endif

/**
 * Allow users to send a message without waiting for the reply,
 * in the case that they choose to discard all replies up until
 * a particular EVENT is received.
 */
static G_GNUC_PRINTF(4, 0) void
_qmp_fd_vsend_fds(int fd, int *fds, size_t fds_num,
                  const char *fmt, va_list ap)
{
    QObject *qobj;

#ifdef _WIN32
    assert(fds_num == 0);
#endif

    /* Going through qobject ensures we escape strings properly */
    qobj = qobject_from_vjsonf_nofail(fmt, ap);

    /* No need to send anything for an empty QObject.  */
    if (qobj) {
        int log = getenv("QTEST_LOG") != NULL;
        GString *str = qobject_to_json(qobj);

        /*
         * BUG: QMP doesn't react to input until it sees a newline, an
         * object, or an array.  Work-around: give it a newline.
         */
        g_string_append_c(str, '\n');

        if (log) {
            fprintf(stderr, "%s", str->str);
        }

#ifndef _WIN32
        /* Send QMP request */
        if (fds && fds_num > 0) {
            socket_send_fds(fd, fds, fds_num, str->str, str->len);
        } else
#endif
        {
            socket_send(fd, str->str, str->len);
        }

        g_string_free(str, true);
        qobject_unref(qobj);
    }
}

#ifndef _WIN32
void qmp_fd_vsend_fds(int fd, int *fds, size_t fds_num,
                      const char *fmt, va_list ap)
{
    _qmp_fd_vsend_fds(fd, fds, fds_num, fmt, ap);
}
#endif

void qmp_fd_vsend(int fd, const char *fmt, va_list ap)
{
    _qmp_fd_vsend_fds(fd, NULL, 0, fmt, ap);
}


QDict *qmp_fdv(int fd, const char *fmt, va_list ap)
{
    _qmp_fd_vsend_fds(fd, NULL, 0, fmt, ap);

    return qmp_fd_receive(fd);
}

QDict *qmp_fd(int fd, const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qmp_fdv(fd, fmt, ap);
    va_end(ap);
    return response;
}

void qmp_fd_send(int fd, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qmp_fd_vsend(fd, fmt, ap);
    va_end(ap);
}

void qmp_fd_vsend_raw(int fd, const char *fmt, va_list ap)
{
    bool log = getenv("QTEST_LOG") != NULL;
    char *str = g_strdup_vprintf(fmt, ap);

    if (log) {
        fprintf(stderr, "%s", str);
    }
    socket_send(fd, str, strlen(str));
    g_free(str);
}

void qmp_fd_send_raw(int fd, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qmp_fd_vsend_raw(fd, fmt, ap);
    va_end(ap);
}

bool qmp_rsp_is_err(QDict *rsp)
{
    QDict *error = qdict_get_qdict(rsp, "error");
    qobject_unref(rsp);
    return !!error;
}

void qmp_expect_error_and_unref(QDict *rsp, const char *class)
{
    QDict *error = qdict_get_qdict(rsp, "error");

    g_assert_cmpstr(qdict_get_try_str(error, "class"), ==, class);
    g_assert_nonnull(qdict_get_try_str(error, "desc"));
    g_assert(!qdict_haskey(rsp, "return"));

    qobject_unref(rsp);
}
