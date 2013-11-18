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
 *
 */
#include "libqtest.h"

#include <glib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "qemu/compiler.h"
#include "qemu/osdep.h"
#include "qapi/qmp/json-streamer.h"
#include "qapi/qmp/json-parser.h"

#define MAX_IRQ 256

QTestState *global_qtest;

struct QTestState
{
    int fd;
    int qmp_fd;
    bool irq_level[MAX_IRQ];
    GString *rx;
    gchar *pid_file; /* QEMU PID file */
    int child_pid;   /* Child process created to execute QEMU */
    char *socket_path, *qmp_socket_path;
};

#define g_assert_no_errno(ret) do { \
    g_assert_cmpint(ret, !=, -1); \
} while (0)

static int init_socket(const char *socket_path)
{
    struct sockaddr_un addr;
    int sock;
    int ret;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert_no_errno(sock);

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);
    qemu_set_cloexec(sock);

    do {
        ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    } while (ret == -1 && errno == EINTR);
    g_assert_no_errno(ret);
    listen(sock, 1);

    return sock;
}

static int socket_accept(int sock)
{
    struct sockaddr_un addr;
    socklen_t addrlen;
    int ret;

    addrlen = sizeof(addr);
    do {
        ret = accept(sock, (struct sockaddr *)&addr, &addrlen);
    } while (ret == -1 && errno == EINTR);
    g_assert_no_errno(ret);
    close(sock);

    return ret;
}

static pid_t qtest_qemu_pid(QTestState *s)
{
    FILE *f;
    char buffer[1024];
    pid_t pid = -1;

    f = fopen(s->pid_file, "r");
    if (f) {
        if (fgets(buffer, sizeof(buffer), f)) {
            pid = atoi(buffer);
        }
        fclose(f);
    }
    return pid;
}

QTestState *qtest_init(const char *extra_args)
{
    QTestState *s;
    int sock, qmpsock, i;
    gchar *pid_file;
    gchar *command;
    const char *qemu_binary;
    pid_t pid;

    qemu_binary = getenv("QTEST_QEMU_BINARY");
    g_assert(qemu_binary != NULL);

    s = g_malloc(sizeof(*s));

    s->socket_path = g_strdup_printf("/tmp/qtest-%d.sock", getpid());
    s->qmp_socket_path = g_strdup_printf("/tmp/qtest-%d.qmp", getpid());
    pid_file = g_strdup_printf("/tmp/qtest-%d.pid", getpid());

    sock = init_socket(s->socket_path);
    qmpsock = init_socket(s->qmp_socket_path);

    pid = fork();
    if (pid == 0) {
        command = g_strdup_printf("%s "
                                  "-qtest unix:%s,nowait "
                                  "-qtest-log /dev/null "
                                  "-qmp unix:%s,nowait "
                                  "-pidfile %s "
                                  "-machine accel=qtest "
                                  "-display none "
                                  "%s", qemu_binary, s->socket_path,
                                  s->qmp_socket_path, pid_file,
                                  extra_args ?: "");
        execlp("/bin/sh", "sh", "-c", command, NULL);
        exit(1);
    }

    s->fd = socket_accept(sock);
    s->qmp_fd = socket_accept(qmpsock);

    s->rx = g_string_new("");
    s->pid_file = pid_file;
    s->child_pid = pid;
    for (i = 0; i < MAX_IRQ; i++) {
        s->irq_level[i] = false;
    }

    /* Read the QMP greeting and then do the handshake */
    qtest_qmp_discard_response(s, "");
    qtest_qmp_discard_response(s, "{ 'execute': 'qmp_capabilities' }");

    if (getenv("QTEST_STOP")) {
        kill(qtest_qemu_pid(s), SIGSTOP);
    }

    return s;
}

void qtest_quit(QTestState *s)
{
    int status;

    pid_t pid = qtest_qemu_pid(s);
    if (pid != -1) {
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
    }

    close(s->fd);
    close(s->qmp_fd);
    g_string_free(s->rx, true);
    unlink(s->pid_file);
    unlink(s->socket_path);
    unlink(s->qmp_socket_path);
    g_free(s->pid_file);
    g_free(s->socket_path);
    g_free(s->qmp_socket_path);
    g_free(s);
}

static void socket_sendf(int fd, const char *fmt, va_list ap)
{
    gchar *str;
    size_t size, offset;

    str = g_strdup_vprintf(fmt, ap);
    size = strlen(str);

    offset = 0;
    while (offset < size) {
        ssize_t len;

        len = write(fd, str + offset, size - offset);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        g_assert_no_errno(len);
        g_assert_cmpint(len, >, 0);

        offset += len;
    }
}

static void GCC_FMT_ATTR(2, 3) qtest_sendf(QTestState *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    socket_sendf(s->fd, fmt, ap);
    va_end(ap);
}

static GString *qtest_recv_line(QTestState *s)
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
            exit(1);
        }

        g_string_append_len(s->rx, buffer, len);
    }

    offset = eol - s->rx->str;
    line = g_string_new_len(s->rx->str, offset);
    g_string_erase(s->rx, 0, offset + 1);

    return line;
}

static gchar **qtest_rsp(QTestState *s, int expected_args)
{
    GString *line;
    gchar **words;
    int i;

redo:
    line = qtest_recv_line(s);
    words = g_strsplit(line->str, " ", 0);
    g_string_free(line, TRUE);

    if (strcmp(words[0], "IRQ") == 0) {
        int irq;

        g_assert(words[1] != NULL);
        g_assert(words[2] != NULL);

        irq = strtoul(words[2], NULL, 0);
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

    if (expected_args) {
        for (i = 0; i < expected_args; i++) {
            g_assert(words[i] != NULL);
        }
    } else {
        g_strfreev(words);
    }

    return words;
}

typedef struct {
    JSONMessageParser parser;
    QDict *response;
} QMPResponseParser;

static void qmp_response(JSONMessageParser *parser, QList *tokens)
{
    QMPResponseParser *qmp = container_of(parser, QMPResponseParser, parser);
    QObject *obj;

    obj = json_parser_parse(tokens, NULL);
    if (!obj) {
        fprintf(stderr, "QMP JSON response parsing failed\n");
        exit(1);
    }

    g_assert(qobject_type(obj) == QTYPE_QDICT);
    g_assert(!qmp->response);
    qmp->response = (QDict *)obj;
}

QDict *qtest_qmpv(QTestState *s, const char *fmt, va_list ap)
{
    QMPResponseParser qmp;

    /* Send QMP request */
    socket_sendf(s->qmp_fd, fmt, ap);

    /* Receive reply */
    qmp.response = NULL;
    json_message_parser_init(&qmp.parser, qmp_response);
    while (!qmp.response) {
        ssize_t len;
        char c;

        len = read(s->qmp_fd, &c, 1);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        if (len == -1 || len == 0) {
            fprintf(stderr, "Broken pipe\n");
            exit(1);
        }

        json_message_parser_feed(&qmp.parser, &c, 1);
    }
    json_message_parser_destroy(&qmp.parser);

    return qmp.response;
}

QDict *qtest_qmp(QTestState *s, const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_qmpv(s, fmt, ap);
    va_end(ap);
    return response;
}

void qtest_qmpv_discard_response(QTestState *s, const char *fmt, va_list ap)
{
    QDict *response = qtest_qmpv(s, fmt, ap);
    QDECREF(response);
}

void qtest_qmp_discard_response(QTestState *s, const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_qmpv(s, fmt, ap);
    va_end(ap);
    QDECREF(response);
}

const char *qtest_get_arch(void)
{
    const char *qemu = getenv("QTEST_QEMU_BINARY");
    const char *end = strrchr(qemu, '/');

    return end + strlen("/qemu-system-");
}

bool qtest_get_irq(QTestState *s, int num)
{
    /* dummy operation in order to make sure irq is up to date */
    qtest_inb(s, 0);

    return s->irq_level[num];
}

static int64_t qtest_clock_rsp(QTestState *s)
{
    gchar **words;
    int64_t clock;
    words = qtest_rsp(s, 2);
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
    qtest_rsp(s, 0);
}

void qtest_irq_intercept_in(QTestState *s, const char *qom_path)
{
    qtest_sendf(s, "irq_intercept_in %s\n", qom_path);
    qtest_rsp(s, 0);
}

static void qtest_out(QTestState *s, const char *cmd, uint16_t addr, uint32_t value)
{
    qtest_sendf(s, "%s 0x%x 0x%x\n", cmd, addr, value);
    qtest_rsp(s, 0);
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
    uint32_t value;

    qtest_sendf(s, "%s 0x%x\n", cmd, addr);
    args = qtest_rsp(s, 2);
    value = strtoul(args[1], NULL, 0);
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
    qtest_rsp(s, 0);
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
    uint64_t value;

    qtest_sendf(s, "%s 0x%" PRIx64 "\n", cmd, addr);
    args = qtest_rsp(s, 2);
    value = strtoull(args[1], NULL, 0);
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

    qtest_sendf(s, "read 0x%" PRIx64 " 0x%zx\n", addr, size);
    args = qtest_rsp(s, 2);

    for (i = 0; i < size; i++) {
        ptr[i] = hex2nib(args[1][2 + (i * 2)]) << 4;
        ptr[i] |= hex2nib(args[1][2 + (i * 2) + 1]);
    }

    g_strfreev(args);
}

void qtest_add_func(const char *str, void (*fn))
{
    gchar *path = g_strdup_printf("/%s/%s", qtest_get_arch(), str);
    g_test_add_func(path, fn);
}

void qtest_memwrite(QTestState *s, uint64_t addr, const void *data, size_t size)
{
    const uint8_t *ptr = data;
    size_t i;

    qtest_sendf(s, "write 0x%" PRIx64 " 0x%zx 0x", addr, size);
    for (i = 0; i < size; i++) {
        qtest_sendf(s, "%02x", ptr[i]);
    }
    qtest_sendf(s, "\n");
    qtest_rsp(s, 0);
}
