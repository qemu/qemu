#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/sockets.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qom/qom-qobject.h"
#include "qmp-commands.h"

static bool quit;

typedef struct FeHandler {
    int read_count;
    int last_event;
    char read_buf[128];
} FeHandler;

static void main_loop(void)
{
    quit = false;
    do {
        main_loop_wait(false);
    } while (!quit);
}

static int fe_can_read(void *opaque)
{
    FeHandler *h = opaque;

    return sizeof(h->read_buf) - h->read_count;
}

static void fe_read(void *opaque, const uint8_t *buf, int size)
{
    FeHandler *h = opaque;

    g_assert_cmpint(size, <=, fe_can_read(opaque));

    memcpy(h->read_buf + h->read_count, buf, size);
    h->read_count += size;
    quit = true;
}

static void fe_event(void *opaque, int event)
{
    FeHandler *h = opaque;

    h->last_event = event;
    if (event != CHR_EVENT_BREAK) {
        quit = true;
    }
}

#ifdef CONFIG_HAS_GLIB_SUBPROCESS_TESTS
#ifdef _WIN32
static void char_console_test_subprocess(void)
{
    QemuOpts *opts;
    Chardev *chr;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "console-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "console", &error_abort);

    chr = qemu_chr_new_from_opts(opts, NULL);
    g_assert_nonnull(chr);

    qemu_chr_write_all(chr, (const uint8_t *)"CONSOLE", 7);

    qemu_opts_del(opts);
    object_unparent(OBJECT(chr));
}

static void char_console_test(void)
{
    g_test_trap_subprocess("/char/console/subprocess", 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stdout("CONSOLE");
}
#endif
static void char_stdio_test_subprocess(void)
{
    Chardev *chr;
    CharBackend be;
    int ret;

    chr = qemu_chr_new("label", "stdio");
    g_assert_nonnull(chr);

    qemu_chr_fe_init(&be, chr, &error_abort);
    qemu_chr_fe_set_open(&be, true);
    ret = qemu_chr_fe_write(&be, (void *)"buf", 4);
    g_assert_cmpint(ret, ==, 4);

    qemu_chr_fe_deinit(&be, true);
}

static void char_stdio_test(void)
{
    g_test_trap_subprocess("/char/stdio/subprocess", 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stdout("buf");
}
#endif

static void char_ringbuf_test(void)
{
    QemuOpts *opts;
    Chardev *chr;
    CharBackend be;
    char *data;
    int ret;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "ringbuf-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);

    qemu_opt_set(opts, "size", "5", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL);
    g_assert_null(chr);
    qemu_opts_del(opts);

    opts = qemu_opts_create(qemu_find_opts("chardev"), "ringbuf-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", "2", &error_abort);
    chr = qemu_chr_new_from_opts(opts, &error_abort);
    g_assert_nonnull(chr);
    qemu_opts_del(opts);

    qemu_chr_fe_init(&be, chr, &error_abort);
    ret = qemu_chr_fe_write(&be, (void *)"buff", 4);
    g_assert_cmpint(ret, ==, 4);

    data = qmp_ringbuf_read("ringbuf-label", 4, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "ff");
    g_free(data);

    data = qmp_ringbuf_read("ringbuf-label", 4, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "");
    g_free(data);

    qemu_chr_fe_deinit(&be, true);

    /* check alias */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "memory-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "memory", &error_abort);
    qemu_opt_set(opts, "size", "2", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL);
    g_assert_nonnull(chr);
    object_unparent(OBJECT(chr));
    qemu_opts_del(opts);
}

static void char_mux_test(void)
{
    QemuOpts *opts;
    Chardev *chr, *base;
    char *data;
    FeHandler h1 = { 0, }, h2 = { 0, };
    CharBackend chr_be1, chr_be2;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "mux-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", "128", &error_abort);
    qemu_opt_set(opts, "mux", "on", &error_abort);
    chr = qemu_chr_new_from_opts(opts, &error_abort);
    g_assert_nonnull(chr);
    qemu_opts_del(opts);

    qemu_chr_fe_init(&chr_be1, chr, &error_abort);
    qemu_chr_fe_set_handlers(&chr_be1,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &h1,
                             NULL, true);

    qemu_chr_fe_init(&chr_be2, chr, &error_abort);
    qemu_chr_fe_set_handlers(&chr_be2,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &h2,
                             NULL, true);
    qemu_chr_fe_take_focus(&chr_be2);

    base = qemu_chr_find("mux-label-base");
    g_assert_cmpint(qemu_chr_be_can_write(base), !=, 0);

    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h1.read_count, ==, 0);
    g_assert_cmpint(h2.read_count, ==, 6);
    g_assert_cmpstr(h2.read_buf, ==, "hello");
    h2.read_count = 0;

    /* switch focus */
    qemu_chr_be_write(base, (void *)"\1c", 2);

    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h2.read_count, ==, 0);
    g_assert_cmpint(h1.read_count, ==, 6);
    g_assert_cmpstr(h1.read_buf, ==, "hello");
    h1.read_count = 0;

    /* remove first handler */
    qemu_chr_fe_set_handlers(&chr_be1, NULL, NULL, NULL, NULL,
                             NULL, NULL, true);
    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h1.read_count, ==, 0);
    g_assert_cmpint(h2.read_count, ==, 0);

    qemu_chr_be_write(base, (void *)"\1c", 2);
    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h1.read_count, ==, 0);
    g_assert_cmpint(h2.read_count, ==, 6);
    g_assert_cmpstr(h2.read_buf, ==, "hello");
    h2.read_count = 0;

    /* print help */
    qemu_chr_be_write(base, (void *)"\1?", 2);
    data = qmp_ringbuf_read("mux-label-base", 128, false, 0, &error_abort);
    g_assert_cmpint(strlen(data), !=, 0);
    g_free(data);

    qemu_chr_fe_deinit(&chr_be1, false);
    qemu_chr_fe_deinit(&chr_be2, true);
}

typedef struct SocketIdleData {
    GMainLoop *loop;
    Chardev *chr;
    bool conn_expected;
    CharBackend *be;
    CharBackend *client_be;
} SocketIdleData;

static gboolean char_socket_test_idle(gpointer user_data)
{
    SocketIdleData *data = user_data;

    if (object_property_get_bool(OBJECT(data->chr), "connected", NULL)
        == data->conn_expected) {
        quit = true;
        return FALSE;
    }

    return TRUE;
}

static void socket_read(void *opaque, const uint8_t *buf, int size)
{
    SocketIdleData *data = opaque;

    g_assert_cmpint(size, ==, 1);
    g_assert_cmpint(*buf, ==, 'Z');

    size = qemu_chr_fe_write(data->be, (const uint8_t *)"hello", 5);
    g_assert_cmpint(size, ==, 5);
}

static int socket_can_read(void *opaque)
{
    return 10;
}

static void socket_read_hello(void *opaque, const uint8_t *buf, int size)
{
    g_assert_cmpint(size, ==, 5);
    g_assert(strncmp((char *)buf, "hello", 5) == 0);

    quit = true;
}

static int socket_can_read_hello(void *opaque)
{
    return 10;
}

static void char_socket_test(void)
{
    Chardev *chr = qemu_chr_new("server", "tcp:127.0.0.1:0,server,nowait");
    Chardev *chr_client;
    QObject *addr;
    QDict *qdict;
    const char *port;
    SocketIdleData d = { .chr = chr };
    CharBackend be;
    CharBackend client_be;
    char *tmp;

    d.be = &be;
    d.client_be = &be;

    g_assert_nonnull(chr);
    g_assert(!object_property_get_bool(OBJECT(chr), "connected", &error_abort));

    addr = object_property_get_qobject(OBJECT(chr), "addr", &error_abort);
    qdict = qobject_to_qdict(addr);
    port = qdict_get_str(qdict, "port");
    tmp = g_strdup_printf("tcp:127.0.0.1:%s", port);
    QDECREF(qdict);

    qemu_chr_fe_init(&be, chr, &error_abort);
    qemu_chr_fe_set_handlers(&be, socket_can_read, socket_read,
                             NULL, NULL, &d, NULL, true);

    chr_client = qemu_chr_new("client", tmp);
    qemu_chr_fe_init(&client_be, chr_client, &error_abort);
    qemu_chr_fe_set_handlers(&client_be, socket_can_read_hello,
                             socket_read_hello,
                             NULL, NULL, &d, NULL, true);
    g_free(tmp);

    d.conn_expected = true;
    guint id = g_idle_add(char_socket_test_idle, &d);
    g_source_set_name_by_id(id, "test-idle");
    g_assert_cmpint(id, >, 0);
    main_loop();

    g_assert(object_property_get_bool(OBJECT(chr), "connected", &error_abort));
    g_assert(object_property_get_bool(OBJECT(chr_client),
                                      "connected", &error_abort));

    qemu_chr_write_all(chr_client, (const uint8_t *)"Z", 1);
    main_loop();

    object_unparent(OBJECT(chr_client));

    d.conn_expected = false;
    g_idle_add(char_socket_test_idle, &d);
    main_loop();

    object_unparent(OBJECT(chr));
}

#ifndef _WIN32
static void char_pipe_test(void)
{
    gchar *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
    gchar *tmp, *in, *out, *pipe = g_build_filename(tmp_path, "pipe", NULL);
    Chardev *chr;
    CharBackend be;
    int ret, fd;
    char buf[10];
    FeHandler fe = { 0, };

    in = g_strdup_printf("%s.in", pipe);
    if (mkfifo(in, 0600) < 0) {
        abort();
    }
    out = g_strdup_printf("%s.out", pipe);
    if (mkfifo(out, 0600) < 0) {
        abort();
    }

    tmp = g_strdup_printf("pipe:%s", pipe);
    chr = qemu_chr_new("pipe", tmp);
    g_assert_nonnull(chr);
    g_free(tmp);

    qemu_chr_fe_init(&be, chr, &error_abort);

    ret = qemu_chr_fe_write(&be, (void *)"pipe-out", 9);
    g_assert_cmpint(ret, ==, 9);

    fd = open(out, O_RDWR);
    ret = read(fd, buf, sizeof(buf));
    g_assert_cmpint(ret, ==, 9);
    g_assert_cmpstr(buf, ==, "pipe-out");
    close(fd);

    fd = open(in, O_WRONLY);
    ret = write(fd, "pipe-in", 8);
    g_assert_cmpint(ret, ==, 8);
    close(fd);

    qemu_chr_fe_set_handlers(&be,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &fe,
                             NULL, true);

    main_loop();

    g_assert_cmpint(fe.read_count, ==, 8);
    g_assert_cmpstr(fe.read_buf, ==, "pipe-in");

    qemu_chr_fe_deinit(&be, true);

    g_assert(g_unlink(in) == 0);
    g_assert(g_unlink(out) == 0);
    g_assert(g_rmdir(tmp_path) == 0);
    g_free(in);
    g_free(out);
    g_free(tmp_path);
    g_free(pipe);
}
#endif

static int make_udp_socket(int *port)
{
    struct sockaddr_in addr = { 0, };
    socklen_t alen = sizeof(addr);
    int ret, sock = qemu_socket(PF_INET, SOCK_DGRAM, 0);

    g_assert_cmpint(sock, >, 0);
    addr.sin_family = AF_INET ;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    g_assert_cmpint(ret, ==, 0);
    ret = getsockname(sock, (struct sockaddr *)&addr, &alen);
    g_assert_cmpint(ret, ==, 0);

    *port = ntohs(addr.sin_port);
    return sock;
}

static void char_udp_test_internal(Chardev *reuse_chr, int sock)
{
    struct sockaddr_in other;
    SocketIdleData d = { 0, };
    Chardev *chr;
    CharBackend *be;
    socklen_t alen = sizeof(other);
    int ret;
    char buf[10];
    char *tmp = NULL;

    if (reuse_chr) {
        chr = reuse_chr;
        be = chr->be;
    } else {
        int port;
        sock = make_udp_socket(&port);
        tmp = g_strdup_printf("udp:127.0.0.1:%d", port);
        chr = qemu_chr_new("client", tmp);
        g_assert_nonnull(chr);

        be = g_alloca(sizeof(CharBackend));
        qemu_chr_fe_init(be, chr, &error_abort);
    }

    d.chr = chr;
    qemu_chr_fe_set_handlers(be, socket_can_read_hello, socket_read_hello,
                             NULL, NULL, &d, NULL, true);
    ret = qemu_chr_write_all(chr, (uint8_t *)"hello", 5);
    g_assert_cmpint(ret, ==, 5);

    ret = recvfrom(sock, buf, sizeof(buf), 0,
                   (struct sockaddr *)&other, &alen);
    g_assert_cmpint(ret, ==, 5);
    ret = sendto(sock, buf, 5, 0, (struct sockaddr *)&other, alen);
    g_assert_cmpint(ret, ==, 5);

    main_loop();

    if (!reuse_chr) {
        close(sock);
        qemu_chr_fe_deinit(be, true);
    }
    g_free(tmp);
}

static void char_udp_test(void)
{
    char_udp_test_internal(NULL, 0);
}

#ifdef HAVE_CHARDEV_SERIAL
static void char_serial_test(void)
{
    QemuOpts *opts;
    Chardev *chr;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "serial-id",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "serial", &error_abort);
    qemu_opt_set(opts, "path", "/dev/null", &error_abort);

    chr = qemu_chr_new_from_opts(opts, NULL);
    g_assert_nonnull(chr);
    /* TODO: add more tests with a pty */
    object_unparent(OBJECT(chr));

    /* test tty alias */
    qemu_opt_set(opts, "backend", "tty", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL);
    g_assert_nonnull(chr);
    object_unparent(OBJECT(chr));

    qemu_opts_del(opts);
}
#endif

#ifndef _WIN32
static void char_file_fifo_test(void)
{
    Chardev *chr;
    CharBackend be;
    char *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
    char *fifo = g_build_filename(tmp_path, "fifo", NULL);
    char *out = g_build_filename(tmp_path, "out", NULL);
    ChardevFile file = { .in = fifo,
                         .has_in = true,
                         .out = out };
    ChardevBackend backend = { .type = CHARDEV_BACKEND_KIND_FILE,
                               .u.file.data = &file };
    FeHandler fe = { 0, };
    int fd, ret;

    if (mkfifo(fifo, 0600) < 0) {
        abort();
    }

    fd = open(fifo, O_RDWR);
    ret = write(fd, "fifo-in", 8);
    g_assert_cmpint(ret, ==, 8);

    chr = qemu_chardev_new("label-file", TYPE_CHARDEV_FILE, &backend,
                           &error_abort);

    qemu_chr_fe_init(&be, chr, &error_abort);
    qemu_chr_fe_set_handlers(&be,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &fe, NULL, true);

    g_assert_cmpint(fe.last_event, !=, CHR_EVENT_BREAK);
    qmp_chardev_send_break("label-foo", NULL);
    g_assert_cmpint(fe.last_event, !=, CHR_EVENT_BREAK);
    qmp_chardev_send_break("label-file", NULL);
    g_assert_cmpint(fe.last_event, ==, CHR_EVENT_BREAK);

    main_loop();

    close(fd);

    g_assert_cmpint(fe.read_count, ==, 8);
    g_assert_cmpstr(fe.read_buf, ==, "fifo-in");

    qemu_chr_fe_deinit(&be, true);

    g_unlink(fifo);
    g_free(fifo);
    g_unlink(out);
    g_free(out);
    g_rmdir(tmp_path);
    g_free(tmp_path);
}
#endif

static void char_file_test_internal(Chardev *ext_chr, const char *filepath)
{
    char *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
    char *out;
    Chardev *chr;
    char *contents = NULL;
    ChardevFile file = {};
    ChardevBackend backend = { .type = CHARDEV_BACKEND_KIND_FILE,
                               .u.file.data = &file };
    gsize length;
    int ret;

    if (ext_chr) {
        chr = ext_chr;
        out = g_strdup(filepath);
        file.out = out;
    } else {
        out = g_build_filename(tmp_path, "out", NULL);
        file.out = out;
        chr = qemu_chardev_new(NULL, TYPE_CHARDEV_FILE, &backend,
                               &error_abort);
    }
    ret = qemu_chr_write_all(chr, (uint8_t *)"hello!", 6);
    g_assert_cmpint(ret, ==, 6);

    ret = g_file_get_contents(out, &contents, &length, NULL);
    g_assert(ret == TRUE);
    g_assert_cmpint(length, ==, 6);
    g_assert(strncmp(contents, "hello!", 6) == 0);

    if (!ext_chr) {
        object_unref(OBJECT(chr));
        g_unlink(out);
    }
    g_free(contents);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(out);
}

static void char_file_test(void)
{
    char_file_test_internal(NULL, NULL);
}

static void char_null_test(void)
{
    Error *err = NULL;
    Chardev *chr;
    CharBackend be;
    int ret;

    chr = qemu_chr_find("label-null");
    g_assert_null(chr);

    chr = qemu_chr_new("label-null", "null");
    chr = qemu_chr_find("label-null");
    g_assert_nonnull(chr);

    g_assert(qemu_chr_has_feature(chr,
                 QEMU_CHAR_FEATURE_FD_PASS) == false);
    g_assert(qemu_chr_has_feature(chr,
                 QEMU_CHAR_FEATURE_RECONNECTABLE) == false);

    /* check max avail */
    qemu_chr_fe_init(&be, chr, &error_abort);
    qemu_chr_fe_init(&be, chr, &err);
    error_free_or_abort(&err);

    /* deinit & reinit */
    qemu_chr_fe_deinit(&be, false);
    qemu_chr_fe_init(&be, chr, &error_abort);

    qemu_chr_fe_set_open(&be, true);

    qemu_chr_fe_set_handlers(&be,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             NULL, NULL, true);

    ret = qemu_chr_fe_write(&be, (void *)"buf", 4);
    g_assert_cmpint(ret, ==, 4);

    qemu_chr_fe_deinit(&be, true);
}

static void char_invalid_test(void)
{
    Chardev *chr;

    chr = qemu_chr_new("label-invalid", "invalid");
    g_assert_null(chr);
}

static int chardev_change(void *opaque)
{
    return 0;
}

static int chardev_change_denied(void *opaque)
{
    return -1;
}

static void char_hotswap_test(void)
{
    char *chr_args;
    Chardev *chr;
    CharBackend be;

    gchar *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
    char *filename = g_build_filename(tmp_path, "file", NULL);
    ChardevFile file = { .out = filename };
    ChardevBackend backend = { .type = CHARDEV_BACKEND_KIND_FILE,
                               .u.file.data = &file };
    ChardevReturn *ret;

    int port;
    int sock = make_udp_socket(&port);
    g_assert_cmpint(sock, >, 0);

    chr_args = g_strdup_printf("udp:127.0.0.1:%d", port);

    chr = qemu_chr_new("chardev", chr_args);
    qemu_chr_fe_init(&be, chr, &error_abort);

    /* check that chardev operates correctly */
    char_udp_test_internal(chr, sock);

    /* set the handler that denies the hotswap */
    qemu_chr_fe_set_handlers(&be, NULL, NULL,
                             NULL, chardev_change_denied, NULL, NULL, true);

    /* now, change is denied and has to keep the old backend operating */
    ret = qmp_chardev_change("chardev", &backend, NULL);
    g_assert(!ret);
    g_assert(be.chr == chr);

    char_udp_test_internal(chr, sock);

    /* now allow the change */
    qemu_chr_fe_set_handlers(&be, NULL, NULL,
                             NULL, chardev_change, NULL, NULL, true);

    /* has to succeed now */
    ret = qmp_chardev_change("chardev", &backend, &error_abort);
    g_assert(be.chr != chr);

    close(sock);
    chr = be.chr;

    /* run the file chardev test */
    char_file_test_internal(chr, filename);

    object_unparent(OBJECT(chr));

    qapi_free_ChardevReturn(ret);
    g_unlink(filename);
    g_free(filename);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(chr_args);
}

int main(int argc, char **argv)
{
    qemu_init_main_loop(&error_abort);
    socket_init();

    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_chardev_opts);

    g_test_add_func("/char/null", char_null_test);
    g_test_add_func("/char/invalid", char_invalid_test);
    g_test_add_func("/char/ringbuf", char_ringbuf_test);
    g_test_add_func("/char/mux", char_mux_test);
#ifdef CONFIG_HAS_GLIB_SUBPROCESS_TESTS
#ifdef _WIN32
    g_test_add_func("/char/console/subprocess", char_console_test_subprocess);
    g_test_add_func("/char/console", char_console_test);
#endif
    g_test_add_func("/char/stdio/subprocess", char_stdio_test_subprocess);
    g_test_add_func("/char/stdio", char_stdio_test);
#endif
#ifndef _WIN32
    g_test_add_func("/char/pipe", char_pipe_test);
#endif
    g_test_add_func("/char/file", char_file_test);
#ifndef _WIN32
    g_test_add_func("/char/file-fifo", char_file_fifo_test);
#endif
    g_test_add_func("/char/socket", char_socket_test);
    g_test_add_func("/char/udp", char_udp_test);
#ifdef HAVE_CHARDEV_SERIAL
    g_test_add_func("/char/serial", char_serial_test);
#endif
    g_test_add_func("/char/hotswap", char_hotswap_test);

    return g_test_run();
}
