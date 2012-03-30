/*
 * QTest
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
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

#include "osdep.h"

#define MAX_IRQ 256

QTestState *global_qtest;

struct QTestState
{
    int fd;
    bool irq_level[MAX_IRQ];
    GString *rx;
    gchar *pid_file;
};

#define g_assert_no_errno(ret) do { \
    g_assert_cmpint(ret, !=, -1); \
} while (0)

QTestState *qtest_init(const char *extra_args)
{
    QTestState *s;
    struct sockaddr_un addr;
    int sock, ret, i;
    gchar *socket_path;
    gchar *pid_file;
    gchar *command;
    const char *qemu_binary;
    pid_t pid;
    socklen_t addrlen;

    qemu_binary = getenv("QTEST_QEMU_BINARY");
    g_assert(qemu_binary != NULL);

    socket_path = g_strdup_printf("/tmp/qtest-%d.sock", getpid());
    pid_file = g_strdup_printf("/tmp/qtest-%d.pid", getpid());

    s = g_malloc(sizeof(*s));

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

    pid = fork();
    if (pid == 0) {
        command = g_strdup_printf("%s "
                                  "-qtest unix:%s,nowait "
                                  "-qtest-log /dev/null "
                                  "-pidfile %s "
                                  "-machine accel=qtest "
                                  "%s", qemu_binary, socket_path,
                                  pid_file,
                                  extra_args ?: "");

        ret = system(command);
        exit(ret);
        g_free(command);
    }

    do {
        ret = accept(sock, (struct sockaddr *)&addr, &addrlen);
    } while (ret == -1 && errno == EINTR);
    g_assert_no_errno(ret);
    close(sock);

    s->fd = ret;
    s->rx = g_string_new("");
    s->pid_file = pid_file;
    for (i = 0; i < MAX_IRQ; i++) {
        s->irq_level[i] = false;
    }

    g_free(socket_path);

    return s;
}

void qtest_quit(QTestState *s)
{
    FILE *f;
    char buffer[1024];

    f = fopen(s->pid_file, "r");
    if (f) {
        if (fgets(buffer, sizeof(buffer), f)) {
            pid_t pid = atoi(buffer);
            int status = 0;

            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
        }

        fclose(f);
    }
}

static void qtest_sendf(QTestState *s, const char *fmt, ...)
{
    va_list ap;
    gchar *str;
    size_t size, offset;

    va_start(ap, fmt);
    str = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    size = strlen(str);

    offset = 0;
    while (offset < size) {
        ssize_t len;

        len = write(s->fd, str + offset, size - offset);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        g_assert_no_errno(len);
        g_assert_cmpint(len, >, 0);

        offset += len;
    }
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

    qtest_sendf(s, "read 0x%x 0x%x\n", addr, size);
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

    qtest_sendf(s, "write 0x%x 0x%x 0x", addr, size);
    for (i = 0; i < size; i++) {
        qtest_sendf(s, "%02x", ptr[i]);
    }
    qtest_sendf(s, "\n");
    qtest_rsp(s, 0);
}
