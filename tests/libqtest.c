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
#include "qemu/osdep.h"
#include "libqtest.h"

#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>

#include "qapi/qmp/json-parser.h"
#include "qapi/qmp/json-streamer.h"
#include "qapi/qmp/qjson.h"

#define MAX_IRQ 256
#define SOCKET_TIMEOUT 50

QTestState *global_qtest;

struct QTestState
{
    int fd;
    int qmp_fd;
    bool irq_level[MAX_IRQ];
    GString *rx;
    pid_t qemu_pid;  /* our child QEMU process */
};

static GHookList abrt_hooks;
static GList *qtest_instances;
static struct sigaction sigact_old;

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
    ret = listen(sock, 1);
    g_assert_no_errno(ret);

    return sock;
}

static int socket_accept(int sock)
{
    struct sockaddr_un addr;
    socklen_t addrlen;
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

    return ret;
}

static void kill_qemu(QTestState *s)
{
    if (s->qemu_pid != -1) {
        kill(s->qemu_pid, SIGTERM);
        waitpid(s->qemu_pid, NULL, 0);
    }
}

static void kill_qemu_hook_func(void *s)
{
    kill_qemu(s);
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

void qtest_add_abrt_handler(GHookFunc fn, const void *data)
{
    GHook *hook;

    /* Only install SIGABRT handler once */
    if (!abrt_hooks.is_setup) {
        g_hook_list_init(&abrt_hooks, sizeof(GHook));
        setup_sigabrt_handler();
    }

    hook = g_hook_alloc(&abrt_hooks);
    hook->func = fn;
    hook->data = (void *)data;

    g_hook_prepend(&abrt_hooks, hook);
}

QTestState *qtest_init(const char *extra_args)
{
    QTestState *s;
    int sock, qmpsock, i;
    gchar *socket_path;
    gchar *qmp_socket_path;
    gchar *command;
    const char *qemu_binary;

    qemu_binary = getenv("QTEST_QEMU_BINARY");
    g_assert(qemu_binary != NULL);

    s = g_malloc(sizeof(*s));

    socket_path = g_strdup_printf("/tmp/qtest-%d.sock", getpid());
    qmp_socket_path = g_strdup_printf("/tmp/qtest-%d.qmp", getpid());

    sock = init_socket(socket_path);
    qmpsock = init_socket(qmp_socket_path);

    qtest_add_abrt_handler(kill_qemu_hook_func, s);

    s->qemu_pid = fork();
    if (s->qemu_pid == 0) {
        setenv("QEMU_AUDIO_DRV", "none", true);
        command = g_strdup_printf("exec %s "
                                  "-qtest unix:%s,nowait "
                                  "-qtest-log %s "
                                  "-qmp unix:%s,nowait "
                                  "-machine accel=qtest "
                                  "-display none "
                                  "%s", qemu_binary, socket_path,
                                  getenv("QTEST_LOG") ? "/dev/fd/2" : "/dev/null",
                                  qmp_socket_path,
                                  extra_args ?: "");
        execlp("/bin/sh", "sh", "-c", command, NULL);
        exit(1);
    }

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

    /* Read the QMP greeting and then do the handshake */
    qtest_qmp_discard_response(s, "");
    qtest_qmp_discard_response(s, "{ 'execute': 'qmp_capabilities' }");

    if (getenv("QTEST_STOP")) {
        kill(s->qemu_pid, SIGSTOP);
    }

    return s;
}

void qtest_quit(QTestState *s)
{
    qtest_instances = g_list_remove(qtest_instances, s);
    g_hook_destroy_link(&abrt_hooks, g_hook_find_data(&abrt_hooks, TRUE, s));

    /* Uninstall SIGABRT handler on last instance */
    if (!qtest_instances) {
        cleanup_sigabrt_handler();
    }

    kill_qemu(s);
    close(s->fd);
    close(s->qmp_fd);
    g_string_free(s->rx, true);
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

        g_assert_no_errno(len);
        g_assert_cmpint(len, >, 0);

        offset += len;
    }
}

static void socket_sendf(int fd, const char *fmt, va_list ap)
{
    gchar *str = g_strdup_vprintf(fmt, ap);
    size_t size = strlen(str);

    socket_send(fd, str, size);
    g_free(str);
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

static void qmp_response(JSONMessageParser *parser, GQueue *tokens)
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

QDict *qmp_fd_receive(int fd)
{
    QMPResponseParser qmp;
    bool log = getenv("QTEST_LOG") != NULL;

    qmp.response = NULL;
    json_message_parser_init(&qmp.parser, qmp_response);
    while (!qmp.response) {
        ssize_t len;
        char c;

        len = read(fd, &c, 1);
        if (len == -1 && errno == EINTR) {
            continue;
        }

        if (len == -1 || len == 0) {
            fprintf(stderr, "Broken pipe\n");
            exit(1);
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
    return qmp_fd_receive(s->qmp_fd);
}

/**
 * Allow users to send a message without waiting for the reply,
 * in the case that they choose to discard all replies up until
 * a particular EVENT is received.
 */
void qmp_fd_sendv(int fd, const char *fmt, va_list ap)
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
        int log = getenv("QTEST_LOG") != NULL;
        QString *qstr = qobject_to_json(qobj);
        const char *str = qstring_get_str(qstr);
        size_t size = qstring_get_length(qstr);

        if (log) {
            fprintf(stderr, "%s", str);
        }
        /* Send QMP request */
        socket_send(fd, str, size);

        QDECREF(qstr);
        qobject_decref(qobj);
    }
}

void qtest_async_qmpv(QTestState *s, const char *fmt, va_list ap)
{
    qmp_fd_sendv(s->qmp_fd, fmt, ap);
}

QDict *qmp_fdv(int fd, const char *fmt, va_list ap)
{
    qmp_fd_sendv(fd, fmt, ap);

    return qmp_fd_receive(fd);
}

QDict *qtest_qmpv(QTestState *s, const char *fmt, va_list ap)
{
    qtest_async_qmpv(s, fmt, ap);

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
    qmp_fd_sendv(fd, fmt, ap);
    va_end(ap);
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

void qtest_async_qmp(QTestState *s, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qtest_async_qmpv(s, fmt, ap);
    va_end(ap);
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

void qtest_qmp_eventwait(QTestState *s, const char *event)
{
    QDict *response;

    for (;;) {
        response = qtest_qmp_receive(s);
        if ((qdict_haskey(response, "event")) &&
            (strcmp(qdict_get_str(response, "event"), event) == 0)) {
            QDECREF(response);
            break;
        }
        QDECREF(response);
    }
}

char *qtest_hmpv(QTestState *s, const char *fmt, va_list ap)
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
    QDECREF(resp);
    g_free(cmd);
    return ret;
}

char *qtest_hmp(QTestState *s, const char *fmt, ...)
{
    va_list ap;
    char *ret;

    va_start(ap, fmt);
    ret = qtest_hmpv(s, fmt, ap);
    va_end(ap);
    return ret;
}

const char *qtest_get_arch(void)
{
    const char *qemu = getenv("QTEST_QEMU_BINARY");
    g_assert(qemu != NULL);
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

void qtest_add_func(const char *str, void (*fn)(void))
{
    gchar *path = g_strdup_printf("/%s/%s", qtest_get_arch(), str);
    g_test_add_func(path, fn);
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
    socket_send(s->fd, bdata, strlen(bdata));
    socket_send(s->fd, "\n", 1);
    qtest_rsp(s, 0);
    g_free(bdata);
}

void qtest_bufread(QTestState *s, uint64_t addr, void *data, size_t size)
{
    gchar **args;
    size_t len;

    qtest_sendf(s, "b64read 0x%" PRIx64 " 0x%zx\n", addr, size);
    args = qtest_rsp(s, 2);

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
    char *enc = g_malloc(2 * size + 1);

    for (i = 0; i < size; i++) {
        sprintf(&enc[i * 2], "%02x", ptr[i]);
    }

    qtest_sendf(s, "write 0x%" PRIx64 " 0x%zx 0x%s\n", addr, size, enc);
    qtest_rsp(s, 0);
    g_free(enc);
}

void qtest_memset(QTestState *s, uint64_t addr, uint8_t pattern, size_t size)
{
    qtest_sendf(s, "memset 0x%" PRIx64 " 0x%zx 0x%02x\n", addr, size, pattern);
    qtest_rsp(s, 0);
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

void qmp_async(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qtest_async_qmpv(global_qtest, fmt, ap);
    va_end(ap);
}

void qmp_discard_response(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    qtest_qmpv_discard_response(global_qtest, fmt, ap);
    va_end(ap);
}
char *hmp(const char *fmt, ...)
{
    va_list ap;
    char *ret;

    va_start(ap, fmt);
    ret = qtest_hmpv(global_qtest, fmt, ap);
    va_end(ap);
    return ret;
}

bool qtest_big_endian(void)
{
    const char *arch = qtest_get_arch();
    int i;

    static const struct {
        const char *arch;
        bool big_endian;
    } endianness[] = {
        { "aarch64", false },
        { "alpha", false },
        { "arm", false },
        { "cris", false },
        { "i386", false },
        { "lm32", true },
        { "m68k", true },
        { "microblaze", true },
        { "microblazeel", false },
        { "mips", true },
        { "mips64", true },
        { "mips64el", false },
        { "mipsel", false },
        { "moxie", true },
        { "or32", true },
        { "ppc", true },
        { "ppc64", true },
        { "ppcemb", true },
        { "s390x", true },
        { "sh4", false },
        { "sh4eb", true },
        { "sparc", true },
        { "sparc64", true },
        { "unicore32", false },
        { "x86_64", false },
        { "xtensa", false },
        { "xtensaeb", true },
        {},
    };

    for (i = 0; endianness[i].arch; i++) {
        if (strcmp(endianness[i].arch, arch) == 0) {
            return endianness[i].big_endian;
        }
    }

    return false;
}
