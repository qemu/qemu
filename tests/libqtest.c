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
#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/json-streamer.h"
#include "qapi/qmp/qjson.h"

#define MAX_GPIO_INTERCEPTS 20
#define MAX_IRQ 256
#define SOCKET_TIMEOUT 5

QTestState *global_qtest;

typedef struct SocketInfo
{
    int sock;
    int fd;
    const char *path;
} SocketInfo;

struct QTestState
{
    gpio_id last_intercept_gpio_id;
    bool irq_level[MAX_GPIO_INTERCEPTS][MAX_IRQ];
    GString *rx;
    pid_t qemu_pid;  /* our child QEMU process */
    struct sigaction sigact_old; /* restored on exit */
    SocketInfo qtest_socket, qmp_socket;
    int num_serial_ports;
    SocketInfo *serial_port_sockets;
};

static GList *qtest_instances;
static struct sigaction sigact_old;

#define g_assert_no_errno(ret) do { \
    g_assert_cmpint(ret, !=, -1); \
} while (0)

static gchar *get_temp_file_path(const gchar *name)
{
   return g_strdup_printf("/tmp/qtest-%d.%s", getpid(), name);
}

static void init_socket(SocketInfo *socket_info)
{
    struct sockaddr_un addr;
    int sock;
    int ret;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    g_assert_no_errno(sock);

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_info->path);
    qemu_set_cloexec(sock);

    do {
        ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    } while (ret == -1 && errno == EINTR);
    g_assert_no_errno(ret);
    ret = listen(sock, 1);
    g_assert_no_errno(ret);

    socket_info->sock = sock;
}

static void socket_accept(SocketInfo *socket_info)
{
    struct sockaddr_un addr;
    socklen_t addrlen;
    int sock = socket_info->sock;
    int ret;
    struct timeval timeout = { .tv_sec = SOCKET_TIMEOUT,
                               .tv_usec = 0 };

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout,
               sizeof(timeout));

    do {
        addrlen = sizeof(addr);
        ret = accept(sock, (struct sockaddr *)&addr, &addrlen);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1) {
        fprintf(stderr, "%s failed: %s\n", __func__, strerror(errno));
    }
    close(sock);

    socket_info->fd = ret;
}

static void kill_qemu(QTestState *s)
{
    if (s->qemu_pid != -1) {
        kill(s->qemu_pid, SIGTERM);
        waitpid(s->qemu_pid, NULL, 0);
    }
}

static void sigabrt_handler(int signo)
{
    GList *elem;
    for (elem = qtest_instances; elem; elem = elem->next) {
        kill_qemu(elem->data);
    }
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

QTestState *qtest_init(const char *extra_args, int num_serial_ports)
{
    QTestState *s;
    int i, j;
    gchar *command;
    GString *extra_socket_args;
    const char *qemu_binary, *external_args, *qtest_log_path;

    qemu_binary = getenv("QTEST_QEMU_BINARY");
    g_assert(qemu_binary != NULL);

    external_args = getenv("QTEST_QEMU_ARGS");
    qtest_log_path = getenv("QTEST_LOG_FILE");

    s = g_malloc(sizeof(*s));

    s->qtest_socket.path = get_temp_file_path("sock");
    s->qmp_socket.path = get_temp_file_path("qmp");

    init_socket(&s->qtest_socket);
    init_socket(&s->qmp_socket);

    /* Only install SIGABRT handler once */
    if (!qtest_instances) {
        setup_sigabrt_handler();
    }

    qtest_instances = g_list_prepend(qtest_instances, s);

    s->num_serial_ports = num_serial_ports;
    s->serial_port_sockets = g_malloc(num_serial_ports * sizeof(SocketInfo));
    extra_socket_args = g_string_new("");
    for(i = 0; i < num_serial_ports; i++) {
        gchar *serial_socket_path;
        gchar *socket_name = g_strdup_printf("serial%d", i);
        serial_socket_path = get_temp_file_path(socket_name);
        s->serial_port_sockets[i].path = serial_socket_path;
        g_string_append_printf(extra_socket_args,
                               "-serial unix:%s,nowait ",
                               serial_socket_path);
        init_socket(&s->serial_port_sockets[i]);
    }

    s->qemu_pid = fork();
    if (s->qemu_pid == 0) {
        command = g_strdup_printf("exec %s "
                                  "-qtest unix:%s,nowait "
                                  "-qtest-log %s "
                                  "-qmp unix:%s,nowait "
                                  "-machine accel=qtest "
                                  "-display none "
                                  "%s "
                                  "%s "
                                  "%s",
                                  qemu_binary,
                                  s->qtest_socket.path,
                                  qtest_log_path ?: "/dev/null",
                                  s->qmp_socket.path,
                                  extra_socket_args->str,
                                  extra_args ?: "",
                                  external_args ?: "");
        //printf("%s\n", command);
        execlp("/bin/sh", "sh", "-c", command, NULL);
        exit(1);
    }

    socket_accept(&s->qtest_socket);
    socket_accept(&s->qmp_socket);
    for(i = 0; i < num_serial_ports; i++) {
        socket_accept(&s->serial_port_sockets[i]);
    }

    s->rx = g_string_new("");

    s->last_intercept_gpio_id = -1;
    for(i = 0; i < MAX_GPIO_INTERCEPTS; i++) {
        for (j = 0; j < MAX_IRQ; j++) {
            s->irq_level[i][j] = false;
        }
    }


    /* Read the QMP greeting and then do the handshake */
    qtest_qmp_discard_response(s, "");
    qtest_qmp_discard_response(s, "{ 'execute': 'qmp_capabilities' }");

    if (getenv("QTEST_STOP")) {
        kill(s->qemu_pid, SIGSTOP);
    }

    g_string_free(extra_socket_args, true);

    return s;
}

void qtest_quit(QTestState *s)
{
    int i;

    /* Uninstall SIGABRT handler on last instance */
    if (qtest_instances && !qtest_instances->next) {
        cleanup_sigabrt_handler();
    }

    qtest_instances = g_list_remove(qtest_instances, s);

    kill_qemu(s);
    unlink(s->qtest_socket.path);
    unlink(s->qmp_socket.path);
    close(s->qtest_socket.fd);
    close(s->qmp_socket.fd);
    for(i = 0; i < s->num_serial_ports; i++) {
        unlink(s->serial_port_sockets[i].path);
    }
    g_free(s->serial_port_sockets);
    g_string_free(s->rx, true);
    g_free(s);
}

static void socket_send(SocketInfo *socket_info, const char *buf, size_t size)
{
    size_t offset;

    offset = 0;
    while (offset < size) {
        ssize_t len;

        len = write(socket_info->fd, buf + offset, size - offset);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        g_assert_no_errno(len);
        g_assert_cmpint(len, >, 0);

        offset += len;
    }
}

static void socket_sendf(SocketInfo *socket_info, const char *fmt, va_list ap)
{
    gchar *str = g_strdup_vprintf(fmt, ap);
    size_t size = strlen(str);

    socket_send(socket_info, str, size);
    g_free(str);
}

static void GCC_FMT_ATTR(2, 3) qtest_sendf(QTestState *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    socket_sendf(&s->qtest_socket, fmt, ap);
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

        len = read(s->qtest_socket.fd, buffer, sizeof(buffer));
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
    if (strcmp(words[0], "FAIL") == 0) {
        g_assert_cmpstr(line->str, ==, "OK");
    }
    g_string_free(line, TRUE);

    if (strcmp(words[0], "IRQ") == 0) {
        int irq;
        gpio_id id;

        g_assert(words[1] != NULL);
        g_assert(words[2] != NULL);
        g_assert(words[3] != NULL);

        id = strtoul(words[2], NULL, 0);
        g_assert_cmpint(id, >=, 0);
        g_assert_cmpint(id, <, MAX_GPIO_INTERCEPTS);

        irq = strtoul(words[3], NULL, 0);
        g_assert_cmpint(irq, >=, 0);
        g_assert_cmpint(irq, <, MAX_IRQ);

        if (strcmp(words[1], "raise") == 0) {
            s->irq_level[id][irq] = true;
        } else {
            s->irq_level[id][irq] = false;
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

QDict *qtest_qmp_receive(QTestState *s)
{
    QMPResponseParser qmp;

    qmp.response = NULL;
    json_message_parser_init(&qmp.parser, qmp_response);
    while (!qmp.response) {
        ssize_t len;
        char c;

        len = read(s->qmp_socket.fd, &c, 1);
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

QDict *qtest_qmpv(QTestState *s, const char *fmt, va_list ap)
{
    va_list ap_copy;
    QObject *qobj;

    /* Going through qobject ensures we escape strings properly.
     * This seemingly unnecessary copy is required in case va_list
     * is an array type.
     */
    va_copy(ap_copy, ap);
    qobj = qobject_from_jsonv(fmt, &ap_copy);
    va_end(ap_copy);

    /* No need to send anything for an empty QObject.  */
    if (qobj) {
        QString *qstr = qobject_to_json(qobj);
        const char *str = qstring_get_str(qstr);
        size_t size = qstring_get_length(qstr);

        /* Send QMP request */
        socket_send(&s->qmp_socket, str, size);

        QDECREF(qstr);
        qobject_decref(qobj);
    }

    /* Receive reply */
    return qtest_qmp_receive(s);
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
    g_assert(s->last_intercept_gpio_id >= 0);
    return qtest_get_irq_for_gpio(s, 0, num);
}

bool qtest_get_irq_for_gpio(QTestState *s, gpio_id id, int num)
{
    /* dummy operation in order to make sure irq is up to date */
    qtest_inb(s, 0);

    return s->irq_level[id][num];
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

static gpio_id get_next_intercept_gpio_id(QTestState *s)
{
    gpio_id next_gpio_id = ++(s->last_intercept_gpio_id);
    g_assert(next_gpio_id < MAX_GPIO_INTERCEPTS);
    return next_gpio_id;
}

gpio_id qtest_irq_intercept_out(QTestState *s, const char *qom_path)
{
    gpio_id next_gpio_id = get_next_intercept_gpio_id(s);
    qtest_sendf(s, "irq_intercept_out %s %d\n", qom_path, next_gpio_id);
    qtest_rsp(s, 0);
    return next_gpio_id;
}

gpio_id qtest_irq_intercept_in(QTestState *s, const char *qom_path)
{
    gpio_id next_gpio_id = get_next_intercept_gpio_id(s);
    qtest_sendf(s, "irq_intercept_in %s %d\n", qom_path, next_gpio_id);
    qtest_rsp(s, 0);
    return next_gpio_id;
}

void qtest_set_irq_in(QTestState *s, const char *string, int num, int level)
{
    qtest_sendf(s, "set_irq_in %s %d %s\n", string, num,
                   level ? "raise" : "lower");
    qtest_rsp(s, 0);
}

static SocketInfo *get_serial_port_socket(QTestState *s, int serial_socket_num)
{
    g_assert(serial_socket_num >= 0 &&
             serial_socket_num < s->num_serial_ports);
    return &s->serial_port_sockets[serial_socket_num];
}

void qtest_write_serial_port(QTestState *s,
                             int serial_port_num,
                             const char *fmt, ...)
{
    va_list ap;
    SocketInfo *socket_info = get_serial_port_socket(s, serial_port_num);

    va_start(ap, fmt);
    socket_sendf(socket_info, fmt, ap);
    va_end(ap);
}

uint8_t qtest_read_serial_port_byte(QTestState *s, int serial_port_num)
{
    ssize_t len;
    uint8_t buffer;
    SocketInfo *socket_info = get_serial_port_socket(s, serial_port_num);

    do {
        len = read(socket_info->fd, &buffer, sizeof(buffer));
        if (errno == EINTR) {
            continue;
        }

        if (len == -1 || len == 0) {
            fprintf(stderr, "No character to read from socket %d\n",
                               serial_port_num);
            g_assert(false);
        }
    } while(len == -1);

    g_assert_cmpint(len, ==, 1);

    return buffer;
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

void write_serial_port(int serial_port_num, const char *fmt, ...)
{
    va_list ap;
    SocketInfo *socket_info = get_serial_port_socket(global_qtest,
                                                     serial_port_num);

    va_start(ap, fmt);
    socket_sendf(socket_info, fmt, ap);
    va_end(ap);
}

QDict *qmp(const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_qmpv(global_qtest, fmt, ap);
    va_end(ap);
    return response;
}

void qmp_discard_response(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qtest_qmpv_discard_response(global_qtest, fmt, ap);
    va_end(ap);
}
