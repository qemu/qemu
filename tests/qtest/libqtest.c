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

#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>

#include "libqos/libqtest.h"
#include "qemu-common.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qstring.h"

#define MAX_IRQ 256
#define SOCKET_TIMEOUT 50
#define SOCKET_MAX_FDS 16


typedef void (*QTestSendFn)(QTestState *s, const char *buf);
typedef void (*ExternalSendFn)(void *s, const char *buf);
typedef GString* (*QTestRecvFn)(QTestState *);

typedef struct QTestClientTransportOps {
    QTestSendFn     send;      /* for sending qtest commands */

    /*
     * use external_send to send qtest command strings through functions which
     * do not accept a QTestState as the first parameter.
     */
    ExternalSendFn  external_send;

    QTestRecvFn     recv_line; /* for receiving qtest command responses */
} QTestTransportOps;

struct QTestState
{
    int fd;
    int qmp_fd;
    pid_t qemu_pid;  /* our child QEMU process */
    int wstatus;
    int expected_status;
    bool big_endian;
    bool irq_level[MAX_IRQ];
    GString *rx;
    QTestTransportOps ops;
    GList *pending_events;
};

static GHookList abrt_hooks;
static struct sigaction sigact_old;

static int qtest_query_target_endianness(QTestState *s);

static void qtest_client_socket_send(QTestState*, const char *buf);
static void socket_send(int fd, const char *buf, size_t size);

static GString *qtest_client_socket_recv_line(QTestState *);

static void qtest_client_set_tx_handler(QTestState *s, QTestSendFn send);
static void qtest_client_set_rx_handler(QTestState *s, QTestRecvFn recv);

static int init_socket(const char *socket_path)
{
    int sock = qtest_socket_server(socket_path);
    qemu_set_cloexec(sock);
    return sock;
}

static int socket_accept(int sock)
{
    struct sockaddr_un addr;
    socklen_t addrlen;
    int ret;
    struct timeval timeout = { .tv_sec = SOCKET_TIMEOUT,
                               .tv_usec = 0 };

    if (qemu_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                        (void *)&timeout, sizeof(timeout))) {
        fprintf(stderr, "%s failed to set SO_RCVTIMEO: %s\n",
                __func__, strerror(errno));
        close(sock);
        return -1;
    }

    do {
        addrlen = sizeof(addr);
        ret = accept(sock, (struct sockaddr *)&addr, &addrlen);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1) {
        fprintf(stderr, "%s failed: %s\n", __func__, strerror(errno));
    }
    close(sock);

    return ret;
}

bool qtest_probe_child(QTestState *s)
{
    pid_t pid = s->qemu_pid;

    if (pid != -1) {
        pid = waitpid(pid, &s->wstatus, WNOHANG);
        if (pid == 0) {
            return true;
        }
        s->qemu_pid = -1;
    }
    return false;
}

void qtest_set_expected_status(QTestState *s, int status)
{
    s->expected_status = status;
}

void qtest_kill_qemu(QTestState *s)
{
    pid_t pid = s->qemu_pid;
    int wstatus;

    /* Skip wait if qtest_probe_child already reaped.  */
    if (pid != -1) {
        kill(pid, SIGTERM);
        TFR(pid = waitpid(s->qemu_pid, &s->wstatus, 0));
        assert(pid == s->qemu_pid);
        s->qemu_pid = -1;
    }

    /*
     * Check whether qemu exited with expected exit status; anything else is
     * fishy and should be logged with as much detail as possible.
     */
    wstatus = s->wstatus;
    if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != s->expected_status) {
        fprintf(stderr, "%s:%d: kill_qemu() tried to terminate QEMU "
                "process but encountered exit status %d (expected %d)\n",
                __FILE__, __LINE__, WEXITSTATUS(wstatus), s->expected_status);
        abort();
    } else if (WIFSIGNALED(wstatus)) {
        int sig = WTERMSIG(wstatus);
        const char *signame = strsignal(sig) ?: "unknown ???";
        const char *dump = WCOREDUMP(wstatus) ? " (core dumped)" : "";

        fprintf(stderr, "%s:%d: kill_qemu() detected QEMU death "
                "from signal %d (%s)%s\n",
                __FILE__, __LINE__, sig, signame, dump);
        abort();
    }
}

static void kill_qemu_hook_func(void *s)
{
    qtest_kill_qemu(s);
}

static void sigabrt_handler(int signo)
{
    g_hook_list_invoke(&abrt_hooks, FALSE);
}

static void setup_sigabrt_handler(void)
{
    struct sigaction sigact;

    /* Catch SIGABRT to clean up on g_assert() failure */
    sigact = (struct sigaction){
        .sa_handler = sigabrt_handler,
        .sa_flags = SA_RESETHAND,
    };
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGABRT, &sigact, &sigact_old);
}

static void cleanup_sigabrt_handler(void)
{
    sigaction(SIGABRT, &sigact_old, NULL);
}

static bool hook_list_is_empty(GHookList *hook_list)
{
    GHook *hook = g_hook_first_valid(hook_list, TRUE);

    if (!hook) {
        return false;
    }

    g_hook_unref(hook_list, hook);
    return true;
}

void qtest_add_abrt_handler(GHookFunc fn, const void *data)
{
    GHook *hook;

    if (!abrt_hooks.is_setup) {
        g_hook_list_init(&abrt_hooks, sizeof(GHook));
    }

    /* Only install SIGABRT handler once */
    if (hook_list_is_empty(&abrt_hooks)) {
        setup_sigabrt_handler();
    }

    hook = g_hook_alloc(&abrt_hooks);
    hook->func = fn;
    hook->data = (void *)data;

    g_hook_prepend(&abrt_hooks, hook);
}

void qtest_remove_abrt_handler(void *data)
{
    GHook *hook = g_hook_find_data(&abrt_hooks, TRUE, data);
    g_hook_destroy_link(&abrt_hooks, hook);

    /* Uninstall SIGABRT handler on last instance */
    if (hook_list_is_empty(&abrt_hooks)) {
        cleanup_sigabrt_handler();
    }
}

static const char *qtest_qemu_binary(void)
{
    const char *qemu_bin;

    qemu_bin = getenv("QTEST_QEMU_BINARY");
    if (!qemu_bin) {
        fprintf(stderr, "Environment variable QTEST_QEMU_BINARY required\n");
        exit(1);
    }

    return qemu_bin;
}

QTestState *qtest_init_without_qmp_handshake(const char *extra_args)
{
    QTestState *s;
    int sock, qmpsock, i;
    gchar *socket_path;
    gchar *qmp_socket_path;
    gchar *command;
    const char *qemu_binary = qtest_qemu_binary();

    s = g_new(QTestState, 1);

    socket_path = g_strdup_printf("/tmp/qtest-%d.sock", getpid());
    qmp_socket_path = g_strdup_printf("/tmp/qtest-%d.qmp", getpid());

    /* It's possible that if an earlier test run crashed it might
     * have left a stale unix socket lying around. Delete any
     * stale old socket to avoid spurious test failures with
     * tests/libqtest.c:70:init_socket: assertion failed (ret != -1): (-1 != -1)
     */
    unlink(socket_path);
    unlink(qmp_socket_path);

    sock = init_socket(socket_path);
    qmpsock = init_socket(qmp_socket_path);

    qtest_client_set_rx_handler(s, qtest_client_socket_recv_line);
    qtest_client_set_tx_handler(s, qtest_client_socket_send);

    qtest_add_abrt_handler(kill_qemu_hook_func, s);

    command = g_strdup_printf("exec %s "
                              "-qtest unix:%s "
                              "-qtest-log %s "
                              "-chardev socket,path=%s,id=char0 "
                              "-mon chardev=char0,mode=control "
                              "-display none "
                              "%s"
                              " -accel qtest", qemu_binary, socket_path,
                              getenv("QTEST_LOG") ? "/dev/fd/2" : "/dev/null",
                              qmp_socket_path,
                              extra_args ?: "");

    g_test_message("starting QEMU: %s", command);

    s->pending_events = NULL;
    s->wstatus = 0;
    s->expected_status = 0;
    s->qemu_pid = fork();
    if (s->qemu_pid == 0) {
        if (!g_setenv("QEMU_AUDIO_DRV", "none", true)) {
            exit(1);
        }
        execlp("/bin/sh", "sh", "-c", command, NULL);
        exit(1);
    }

    g_free(command);
    s->fd = socket_accept(sock);
    if (s->fd >= 0) {
        s->qmp_fd = socket_accept(qmpsock);
    }
    unlink(socket_path);
    unlink(qmp_socket_path);
    g_free(socket_path);
    g_free(qmp_socket_path);

    g_assert(s->fd >= 0 && s->qmp_fd >= 0);

    s->rx = g_string_new("");
    for (i = 0; i < MAX_IRQ; i++) {
        s->irq_level[i] = false;
    }

    if (getenv("QTEST_STOP")) {
        kill(s->qemu_pid, SIGSTOP);
    }

    /* ask endianness of the target */

    s->big_endian = qtest_query_target_endianness(s);

    return s;
}

QTestState *qtest_init(const char *extra_args)
{
    QTestState *s = qtest_init_without_qmp_handshake(extra_args);
    QDict *greeting;

    /* Read the QMP greeting and then do the handshake */
    greeting = qtest_qmp_receive(s);
    qobject_unref(greeting);
    qobject_unref(qtest_qmp(s, "{ 'execute': 'qmp_capabilities' }"));

    return s;
}

QTestState *qtest_vinitf(const char *fmt, va_list ap)
{
    char *args = g_strdup_vprintf(fmt, ap);
    QTestState *s;

    s = qtest_init(args);
    g_free(args);
    return s;
}

QTestState *qtest_initf(const char *fmt, ...)
{
    va_list ap;
    QTestState *s;

    va_start(ap, fmt);
    s = qtest_vinitf(fmt, ap);
    va_end(ap);
    return s;
}

QTestState *qtest_init_with_serial(const char *extra_args, int *sock_fd)
{
    int sock_fd_init;
    char *sock_path, sock_dir[] = "/tmp/qtest-serial-XXXXXX";
    QTestState *qts;

    g_assert_true(mkdtemp(sock_dir) != NULL);
    sock_path = g_strdup_printf("%s/sock", sock_dir);

    sock_fd_init = init_socket(sock_path);

    qts = qtest_initf("-chardev socket,id=s0,path=%s -serial chardev:s0 %s",
                      sock_path, extra_args);

    *sock_fd = socket_accept(sock_fd_init);

    unlink(sock_path);
    g_free(sock_path);
    rmdir(sock_dir);

    g_assert_true(*sock_fd >= 0);

    return qts;
}

void qtest_quit(QTestState *s)
{
    qtest_remove_abrt_handler(s);

    qtest_kill_qemu(s);
    close(s->fd);
    close(s->qmp_fd);
    g_string_free(s->rx, true);

    for (GList *it = s->pending_events; it != NULL; it = it->next) {
        qobject_unref((QDict *)it->data);
    }

    g_list_free(s->pending_events);

    g_free(s);
}

static void socket_send(int fd, const char *buf, size_t size)
{
    size_t offset;

    offset = 0;
    while (offset < size) {
        ssize_t len;

        len = write(fd, buf + offset, size - offset);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        g_assert_cmpint(len, >, 0);

        offset += len;
    }
}

static void qtest_client_socket_send(QTestState *s, const char *buf)
{
    socket_send(s->fd, buf, strlen(buf));
}

static void GCC_FMT_ATTR(2, 3) qtest_sendf(QTestState *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    gchar *str = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    s->ops.send(s, str);
    g_free(str);
}

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

static GString *qtest_client_socket_recv_line(QTestState *s)
{
    GString *line;
    size_t offset;
    char *eol;

    while ((eol = strchr(s->rx->str, '\n')) == NULL) {
        ssize_t len;
        char buffer[1024];

        len = read(s->fd, buffer, sizeof(buffer));
        if (len == -1 && errno == EINTR) {
            continue;
        }

        if (len == -1 || len == 0) {
            fprintf(stderr, "Broken pipe\n");
            abort();
        }

        g_string_append_len(s->rx, buffer, len);
    }

    offset = eol - s->rx->str;
    line = g_string_new_len(s->rx->str, offset);
    g_string_erase(s->rx, 0, offset + 1);

    return line;
}

static gchar **qtest_rsp_args(QTestState *s, int expected_args)
{
    GString *line;
    gchar **words;
    int i;

redo:
    line = s->ops.recv_line(s);
    words = g_strsplit(line->str, " ", 0);
    g_string_free(line, TRUE);

    if (strcmp(words[0], "IRQ") == 0) {
        long irq;
        int ret;

        g_assert(words[1] != NULL);
        g_assert(words[2] != NULL);

        ret = qemu_strtol(words[2], NULL, 0, &irq);
        g_assert(!ret);
        g_assert_cmpint(irq, >=, 0);
        g_assert_cmpint(irq, <, MAX_IRQ);

        if (strcmp(words[1], "raise") == 0) {
            s->irq_level[irq] = true;
        } else {
            s->irq_level[irq] = false;
        }

        g_strfreev(words);
        goto redo;
    }

    g_assert(words[0] != NULL);
    g_assert_cmpstr(words[0], ==, "OK");

    for (i = 0; i < expected_args; i++) {
        g_assert(words[i] != NULL);
    }

    return words;
}

static void qtest_rsp(QTestState *s)
{
    gchar **words = qtest_rsp_args(s, 0);

    g_strfreev(words);
}

static int qtest_query_target_endianness(QTestState *s)
{
    gchar **args;
    int big_endian;

    qtest_sendf(s, "endianness\n");
    args = qtest_rsp_args(s, 1);
    g_assert(strcmp(args[1], "big") == 0 || strcmp(args[1], "little") == 0);
    big_endian = strcmp(args[1], "big") == 0;
    g_strfreev(args);

    return big_endian;
}

typedef struct {
    JSONMessageParser parser;
    QDict *response;
} QMPResponseParser;

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

        len = read(fd, &c, 1);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        if (len == -1 || len == 0) {
            fprintf(stderr, "Broken pipe\n");
            abort();
        }

        if (log) {
            len = write(2, &c, 1);
        }
        json_message_parser_feed(&qmp.parser, &c, 1);
    }
    json_message_parser_destroy(&qmp.parser);

    return qmp.response;
}

QDict *qtest_qmp_receive(QTestState *s)
{
    while (true) {
        QDict *response = qtest_qmp_receive_dict(s);

        if (!qdict_get_try_str(response, "event")) {
            return response;
        }
        /* Stash the event for a later consumption */
        s->pending_events = g_list_append(s->pending_events, response);
    }
}

QDict *qtest_qmp_receive_dict(QTestState *s)
{
    return qmp_fd_receive(s->qmp_fd);
}

int qtest_socket_server(const char *socket_path)
{
    struct sockaddr_un addr;
    int sock;
    int ret;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert_cmpint(sock, !=, -1);

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

    do {
        ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    } while (ret == -1 && errno == EINTR);
    g_assert_cmpint(ret, !=, -1);
    ret = listen(sock, 1);
    g_assert_cmpint(ret, !=, -1);

    return sock;
}

/**
 * Allow users to send a message without waiting for the reply,
 * in the case that they choose to discard all replies up until
 * a particular EVENT is received.
 */
void qmp_fd_vsend_fds(int fd, int *fds, size_t fds_num,
                      const char *fmt, va_list ap)
{
    QObject *qobj;

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
        /* Send QMP request */
        if (fds && fds_num > 0) {
            socket_send_fds(fd, fds, fds_num, str->str, str->len);
        } else {
            socket_send(fd, str->str, str->len);
        }

        g_string_free(str, true);
        qobject_unref(qobj);
    }
}

void qmp_fd_vsend(int fd, const char *fmt, va_list ap)
{
    qmp_fd_vsend_fds(fd, NULL, 0, fmt, ap);
}

void qtest_qmp_vsend_fds(QTestState *s, int *fds, size_t fds_num,
                         const char *fmt, va_list ap)
{
    qmp_fd_vsend_fds(s->qmp_fd, fds, fds_num, fmt, ap);
}

void qtest_qmp_vsend(QTestState *s, const char *fmt, va_list ap)
{
    qmp_fd_vsend_fds(s->qmp_fd, NULL, 0, fmt, ap);
}

QDict *qmp_fdv(int fd, const char *fmt, va_list ap)
{
    qmp_fd_vsend_fds(fd, NULL, 0, fmt, ap);

    return qmp_fd_receive(fd);
}

QDict *qtest_vqmp_fds(QTestState *s, int *fds, size_t fds_num,
                      const char *fmt, va_list ap)
{
    qtest_qmp_vsend_fds(s, fds, fds_num, fmt, ap);

    /* Receive reply */
    return qtest_qmp_receive(s);
}

QDict *qtest_vqmp(QTestState *s, const char *fmt, va_list ap)
{
    qtest_qmp_vsend(s, fmt, ap);

    /* Receive reply */
    return qtest_qmp_receive(s);
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

QDict *qtest_qmp_fds(QTestState *s, int *fds, size_t fds_num,
                     const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_vqmp_fds(s, fds, fds_num, fmt, ap);
    va_end(ap);
    return response;
}

QDict *qtest_qmp(QTestState *s, const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_vqmp(s, fmt, ap);
    va_end(ap);
    return response;
}

void qtest_qmp_send(QTestState *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qtest_qmp_vsend(s, fmt, ap);
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

void qtest_qmp_send_raw(QTestState *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qmp_fd_vsend_raw(s->qmp_fd, fmt, ap);
    va_end(ap);
}

QDict *qtest_qmp_event_ref(QTestState *s, const char *event)
{
    while (s->pending_events) {

        GList *first = s->pending_events;
        QDict *response = (QDict *)first->data;

        s->pending_events = g_list_delete_link(s->pending_events, first);

        if (!strcmp(qdict_get_str(response, "event"), event)) {
            return response;
        }
        qobject_unref(response);
    }
    return NULL;
}

QDict *qtest_qmp_eventwait_ref(QTestState *s, const char *event)
{
    QDict *response = qtest_qmp_event_ref(s, event);

    if (response) {
        return response;
    }

    for (;;) {
        response = qtest_qmp_receive_dict(s);
        if ((qdict_haskey(response, "event")) &&
            (strcmp(qdict_get_str(response, "event"), event) == 0)) {
            return response;
        }
        qobject_unref(response);
    }
}

void qtest_qmp_eventwait(QTestState *s, const char *event)
{
    QDict *response;

    response = qtest_qmp_eventwait_ref(s, event);
    qobject_unref(response);
}

char *qtest_vhmp(QTestState *s, const char *fmt, va_list ap)
{
    char *cmd;
    QDict *resp;
    char *ret;

    cmd = g_strdup_vprintf(fmt, ap);
    resp = qtest_qmp(s, "{'execute': 'human-monitor-command',"
                     " 'arguments': {'command-line': %s}}",
                     cmd);
    ret = g_strdup(qdict_get_try_str(resp, "return"));
    g_assert(ret);
    qobject_unref(resp);
    g_free(cmd);
    return ret;
}

char *qtest_hmp(QTestState *s, const char *fmt, ...)
{
    va_list ap;
    char *ret;

    va_start(ap, fmt);
    ret = qtest_vhmp(s, fmt, ap);
    va_end(ap);
    return ret;
}

const char *qtest_get_arch(void)
{
    const char *qemu = qtest_qemu_binary();
    const char *end = strrchr(qemu, '-');

    if (!end) {
        fprintf(stderr, "Can't determine architecture from binary name.\n");
        exit(1);
    }

    if (!strstr(qemu, "-system-")) {
        fprintf(stderr, "QTEST_QEMU_BINARY must end with *-system-<arch> "
                "where 'arch' is the target\narchitecture (x86_64, aarch64, "
                "etc).\n");
        exit(1);
    }

    return end + 1;
}

bool qtest_get_irq(QTestState *s, int num)
{
    /* dummy operation in order to make sure irq is up to date */
    qtest_inb(s, 0);

    return s->irq_level[num];
}

void qtest_module_load(QTestState *s, const char *prefix, const char *libname)
{
    qtest_sendf(s, "module_load %s %s\n", prefix, libname);
    qtest_rsp(s);
}

static int64_t qtest_clock_rsp(QTestState *s)
{
    gchar **words;
    int64_t clock;
    words = qtest_rsp_args(s, 2);
    clock = g_ascii_strtoll(words[1], NULL, 0);
    g_strfreev(words);
    return clock;
}

int64_t qtest_clock_step_next(QTestState *s)
{
    qtest_sendf(s, "clock_step\n");
    return qtest_clock_rsp(s);
}

int64_t qtest_clock_step(QTestState *s, int64_t step)
{
    qtest_sendf(s, "clock_step %"PRIi64"\n", step);
    return qtest_clock_rsp(s);
}

int64_t qtest_clock_set(QTestState *s, int64_t val)
{
    qtest_sendf(s, "clock_set %"PRIi64"\n", val);
    return qtest_clock_rsp(s);
}

void qtest_irq_intercept_out(QTestState *s, const char *qom_path)
{
    qtest_sendf(s, "irq_intercept_out %s\n", qom_path);
    qtest_rsp(s);
}

void qtest_irq_intercept_in(QTestState *s, const char *qom_path)
{
    qtest_sendf(s, "irq_intercept_in %s\n", qom_path);
    qtest_rsp(s);
}

void qtest_set_irq_in(QTestState *s, const char *qom_path, const char *name,
                      int num, int level)
{
    if (!name) {
        name = "unnamed-gpio-in";
    }
    qtest_sendf(s, "set_irq_in %s %s %d %d\n", qom_path, name, num, level);
    qtest_rsp(s);
}

static void qtest_out(QTestState *s, const char *cmd, uint16_t addr, uint32_t value)
{
    qtest_sendf(s, "%s 0x%x 0x%x\n", cmd, addr, value);
    qtest_rsp(s);
}

void qtest_outb(QTestState *s, uint16_t addr, uint8_t value)
{
    qtest_out(s, "outb", addr, value);
}

void qtest_outw(QTestState *s, uint16_t addr, uint16_t value)
{
    qtest_out(s, "outw", addr, value);
}

void qtest_outl(QTestState *s, uint16_t addr, uint32_t value)
{
    qtest_out(s, "outl", addr, value);
}

static uint32_t qtest_in(QTestState *s, const char *cmd, uint16_t addr)
{
    gchar **args;
    int ret;
    unsigned long value;

    qtest_sendf(s, "%s 0x%x\n", cmd, addr);
    args = qtest_rsp_args(s, 2);
    ret = qemu_strtoul(args[1], NULL, 0, &value);
    g_assert(!ret && value <= UINT32_MAX);
    g_strfreev(args);

    return value;
}

uint8_t qtest_inb(QTestState *s, uint16_t addr)
{
    return qtest_in(s, "inb", addr);
}

uint16_t qtest_inw(QTestState *s, uint16_t addr)
{
    return qtest_in(s, "inw", addr);
}

uint32_t qtest_inl(QTestState *s, uint16_t addr)
{
    return qtest_in(s, "inl", addr);
}

static void qtest_write(QTestState *s, const char *cmd, uint64_t addr,
                        uint64_t value)
{
    qtest_sendf(s, "%s 0x%" PRIx64 " 0x%" PRIx64 "\n", cmd, addr, value);
    qtest_rsp(s);
}

void qtest_writeb(QTestState *s, uint64_t addr, uint8_t value)
{
    qtest_write(s, "writeb", addr, value);
}

void qtest_writew(QTestState *s, uint64_t addr, uint16_t value)
{
    qtest_write(s, "writew", addr, value);
}

void qtest_writel(QTestState *s, uint64_t addr, uint32_t value)
{
    qtest_write(s, "writel", addr, value);
}

void qtest_writeq(QTestState *s, uint64_t addr, uint64_t value)
{
    qtest_write(s, "writeq", addr, value);
}

static uint64_t qtest_read(QTestState *s, const char *cmd, uint64_t addr)
{
    gchar **args;
    int ret;
    uint64_t value;

    qtest_sendf(s, "%s 0x%" PRIx64 "\n", cmd, addr);
    args = qtest_rsp_args(s, 2);
    ret = qemu_strtou64(args[1], NULL, 0, &value);
    g_assert(!ret);
    g_strfreev(args);

    return value;
}

uint8_t qtest_readb(QTestState *s, uint64_t addr)
{
    return qtest_read(s, "readb", addr);
}

uint16_t qtest_readw(QTestState *s, uint64_t addr)
{
    return qtest_read(s, "readw", addr);
}

uint32_t qtest_readl(QTestState *s, uint64_t addr)
{
    return qtest_read(s, "readl", addr);
}

uint64_t qtest_readq(QTestState *s, uint64_t addr)
{
    return qtest_read(s, "readq", addr);
}

static int hex2nib(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    } else if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'a');
    } else {
        return -1;
    }
}

void qtest_memread(QTestState *s, uint64_t addr, void *data, size_t size)
{
    uint8_t *ptr = data;
    gchar **args;
    size_t i;

    if (!size) {
        return;
    }

    qtest_sendf(s, "read 0x%" PRIx64 " 0x%zx\n", addr, size);
    args = qtest_rsp_args(s, 2);

    for (i = 0; i < size; i++) {
        ptr[i] = hex2nib(args[1][2 + (i * 2)]) << 4;
        ptr[i] |= hex2nib(args[1][2 + (i * 2) + 1]);
    }

    g_strfreev(args);
}

uint64_t qtest_rtas_call(QTestState *s, const char *name,
                         uint32_t nargs, uint64_t args,
                         uint32_t nret, uint64_t ret)
{
    qtest_sendf(s, "rtas %s %u 0x%"PRIx64" %u 0x%"PRIx64"\n",
                name, nargs, args, nret, ret);
    qtest_rsp(s);
    return 0;
}

void qtest_add_func(const char *str, void (*fn)(void))
{
    gchar *path = g_strdup_printf("/%s/%s", qtest_get_arch(), str);
    g_test_add_func(path, fn);
    g_free(path);
}

void qtest_add_data_func_full(const char *str, void *data,
                              void (*fn)(const void *),
                              GDestroyNotify data_free_func)
{
    gchar *path = g_strdup_printf("/%s/%s", qtest_get_arch(), str);
    g_test_add_data_func_full(path, data, fn, data_free_func);
    g_free(path);
}

void qtest_add_data_func(const char *str, const void *data,
                         void (*fn)(const void *))
{
    gchar *path = g_strdup_printf("/%s/%s", qtest_get_arch(), str);
    g_test_add_data_func(path, data, fn);
    g_free(path);
}

void qtest_bufwrite(QTestState *s, uint64_t addr, const void *data, size_t size)
{
    gchar *bdata;

    bdata = g_base64_encode(data, size);
    qtest_sendf(s, "b64write 0x%" PRIx64 " 0x%zx ", addr, size);
    s->ops.send(s, bdata);
    s->ops.send(s, "\n");
    qtest_rsp(s);
    g_free(bdata);
}

void qtest_bufread(QTestState *s, uint64_t addr, void *data, size_t size)
{
    gchar **args;
    size_t len;

    qtest_sendf(s, "b64read 0x%" PRIx64 " 0x%zx\n", addr, size);
    args = qtest_rsp_args(s, 2);

    g_base64_decode_inplace(args[1], &len);
    if (size != len) {
        fprintf(stderr, "bufread: asked for %zu bytes but decoded %zu\n",
                size, len);
        len = MIN(len, size);
    }

    memcpy(data, args[1], len);
    g_strfreev(args);
}

void qtest_memwrite(QTestState *s, uint64_t addr, const void *data, size_t size)
{
    const uint8_t *ptr = data;
    size_t i;
    char *enc;

    if (!size) {
        return;
    }

    enc = g_malloc(2 * size + 1);

    for (i = 0; i < size; i++) {
        sprintf(&enc[i * 2], "%02x", ptr[i]);
    }

    qtest_sendf(s, "write 0x%" PRIx64 " 0x%zx 0x%s\n", addr, size, enc);
    qtest_rsp(s);
    g_free(enc);
}

void qtest_memset(QTestState *s, uint64_t addr, uint8_t pattern, size_t size)
{
    qtest_sendf(s, "memset 0x%" PRIx64 " 0x%zx 0x%02x\n", addr, size, pattern);
    qtest_rsp(s);
}

void qtest_qmp_assert_success(QTestState *qts, const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_vqmp(qts, fmt, ap);
    va_end(ap);

    g_assert(response);
    if (!qdict_haskey(response, "return")) {
        GString *s = qobject_to_json_pretty(QOBJECT(response), true);
        g_test_message("%s", s->str);
        g_string_free(s, true);
    }
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

bool qtest_big_endian(QTestState *s)
{
    return s->big_endian;
}

static bool qtest_check_machine_version(const char *mname, const char *basename,
                                        int major, int minor)
{
    char *newname;
    bool is_equal;

    newname = g_strdup_printf("%s-%i.%i", basename, major, minor);
    is_equal = g_str_equal(mname, newname);
    g_free(newname);

    return is_equal;
}

static bool qtest_is_old_versioned_machine(const char *mname)
{
    const char *dash = strrchr(mname, '-');
    const char *dot = strrchr(mname, '.');
    const char *chr;
    char *bname;
    const int major = QEMU_VERSION_MAJOR;
    const int minor = QEMU_VERSION_MINOR;
    bool res = false;

    if (dash && dot && dot > dash) {
        for (chr = dash + 1; *chr; chr++) {
            if (!qemu_isdigit(*chr) && *chr != '.') {
                return false;
            }
        }
        /*
         * Now check if it is one of the latest versions. Check major + 1
         * and minor + 1 versions as well, since they might already exist
         * in the development branch.
         */
        bname = g_strdup(mname);
        bname[dash - mname] = 0;
        res = !qtest_check_machine_version(mname, bname, major + 1, 0) &&
              !qtest_check_machine_version(mname, bname, major, minor + 1) &&
              !qtest_check_machine_version(mname, bname, major, minor);
        g_free(bname);
    }

    return res;
}

void qtest_cb_for_every_machine(void (*cb)(const char *machine),
                                bool skip_old_versioned)
{
    QDict *response, *minfo;
    QList *list;
    const QListEntry *p;
    QObject *qobj;
    QString *qstr;
    const char *mname;
    QTestState *qts;

    qts = qtest_init("-machine none");
    response = qtest_qmp(qts, "{ 'execute': 'query-machines' }");
    g_assert(response);
    list = qdict_get_qlist(response, "return");
    g_assert(list);

    for (p = qlist_first(list); p; p = qlist_next(p)) {
        minfo = qobject_to(QDict, qlist_entry_obj(p));
        g_assert(minfo);
        qobj = qdict_get(minfo, "name");
        g_assert(qobj);
        qstr = qobject_to(QString, qobj);
        g_assert(qstr);
        mname = qstring_get_str(qstr);
        /* Ignore machines that cannot be used for qtests */
        if (!strncmp("xenfv", mname, 5) || g_str_equal("xenpv", mname)) {
            continue;
        }
        if (!skip_old_versioned || !qtest_is_old_versioned_machine(mname)) {
            cb(mname);
        }
    }

    qtest_quit(qts);
    qobject_unref(response);
}

/*
 * Generic hot-plugging test via the device_add QMP commands.
 */
void qtest_qmp_device_add_qdict(QTestState *qts, const char *drv,
                                const QDict *arguments)
{
    QDict *resp;
    QDict *args = arguments ? qdict_clone_shallow(arguments) : qdict_new();

    g_assert(!qdict_haskey(args, "driver"));
    qdict_put_str(args, "driver", drv);
    resp = qtest_qmp(qts, "{'execute': 'device_add', 'arguments': %p}", args);
    g_assert(resp);
    g_assert(!qdict_haskey(resp, "event")); /* We don't expect any events */
    g_assert(!qdict_haskey(resp, "error"));
    qobject_unref(resp);
}

void qtest_qmp_device_add(QTestState *qts, const char *driver, const char *id,
                          const char *fmt, ...)
{
    QDict *args;
    va_list ap;

    va_start(ap, fmt);
    args = qdict_from_vjsonf_nofail(fmt, ap);
    va_end(ap);

    g_assert(!qdict_haskey(args, "id"));
    qdict_put_str(args, "id", id);

    qtest_qmp_device_add_qdict(qts, driver, args);
    qobject_unref(args);
}


/*
 * Generic hot-unplugging test via the device_del QMP command.
 * Device deletion will get one response and one event. For example:
 *
 * {'execute': 'device_del','arguments': { 'id': 'scsi-hd'}}
 *
 * will get this one:
 *
 * {"timestamp": {"seconds": 1505289667, "microseconds": 569862},
 *  "event": "DEVICE_DELETED", "data": {"device": "scsi-hd",
 *  "path": "/machine/peripheral/scsi-hd"}}
 *
 * and this one:
 *
 * {"return": {}}
 */
void qtest_qmp_device_del(QTestState *qts, const char *id)
{
    QDict *rsp;

    rsp = qtest_qmp(qts, "{'execute': 'device_del', 'arguments': {'id': %s}}",
                    id);

    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
    qtest_qmp_eventwait(qts, "DEVICE_DELETED");
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

static void qtest_client_set_tx_handler(QTestState *s,
                    QTestSendFn send)
{
    s->ops.send = send;
}
static void qtest_client_set_rx_handler(QTestState *s, QTestRecvFn recv)
{
    s->ops.recv_line = recv;
}
/* A type-safe wrapper for s->send() */
static void send_wrapper(QTestState *s, const char *buf)
{
    s->ops.external_send(s, buf);
}

static GString *qtest_client_inproc_recv_line(QTestState *s)
{
    GString *line;
    size_t offset;
    char *eol;

    eol = strchr(s->rx->str, '\n');
    offset = eol - s->rx->str;
    line = g_string_new_len(s->rx->str, offset);
    g_string_erase(s->rx, 0, offset + 1);
    return line;
}

QTestState *qtest_inproc_init(QTestState **s, bool log, const char* arch,
                    void (*send)(void*, const char*))
{
    QTestState *qts;
    qts = g_new0(QTestState, 1);
    qts->pending_events = NULL;
    *s = qts; /* Expose qts early on, since the query endianness relies on it */
    qts->wstatus = 0;
    for (int i = 0; i < MAX_IRQ; i++) {
        qts->irq_level[i] = false;
    }

    qtest_client_set_rx_handler(qts, qtest_client_inproc_recv_line);

    /* send() may not have a matching protoype, so use a type-safe wrapper */
    qts->ops.external_send = send;
    qtest_client_set_tx_handler(qts, send_wrapper);

    qts->big_endian = qtest_query_target_endianness(qts);

    /*
     * Set a dummy path for QTEST_QEMU_BINARY. Doesn't need to exist, but this
     * way, qtest_get_arch works for inproc qtest.
     */
    gchar *bin_path = g_strconcat("/qemu-system-", arch, NULL);
    setenv("QTEST_QEMU_BINARY", bin_path, 0);
    g_free(bin_path);

    return qts;
}

void qtest_client_inproc_recv(void *opaque, const char *str)
{
    QTestState *qts = *(QTestState **)opaque;

    if (!qts->rx) {
        qts->rx = g_string_new(NULL);
    }
    g_string_append(qts->rx, str);
    return;
}
