#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qemu/config-file.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qmp-commands.h"

typedef struct FeHandler {
    int read_count;
    int last_event;
    char read_buf[128];
} FeHandler;

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
}

static void fe_event(void *opaque, int event)
{
    FeHandler *h = opaque;

    h->last_event = event;
}

#ifdef CONFIG_HAS_GLIB_SUBPROCESS_TESTS
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

    qemu_chr_fe_deinit(&be);
    qemu_chr_delete(chr);
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

    qemu_chr_fe_deinit(&be);
    qemu_chr_delete(chr);
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
                             &h1,
                             NULL, true);

    qemu_chr_fe_init(&chr_be2, chr, &error_abort);
    qemu_chr_fe_set_handlers(&chr_be2,
                             fe_can_read,
                             fe_read,
                             fe_event,
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
    qemu_chr_fe_set_handlers(&chr_be1, NULL, NULL, NULL, NULL, NULL, true);
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

    qemu_chr_fe_deinit(&chr_be1);
    qemu_chr_fe_deinit(&chr_be2);
    qemu_chr_delete(chr);
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
    qemu_chr_fe_deinit(&be);
    qemu_chr_fe_init(&be, chr, &error_abort);

    qemu_chr_fe_set_open(&be, true);

    qemu_chr_fe_set_handlers(&be,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL, NULL, true);

    ret = qemu_chr_fe_write(&be, (void *)"buf", 4);
    g_assert_cmpint(ret, ==, 4);

    qemu_chr_fe_deinit(&be);
    qemu_chr_delete(chr);
}

static void char_invalid_test(void)
{
    Chardev *chr;

    chr = qemu_chr_new("label-invalid", "invalid");
    g_assert_null(chr);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_chardev_opts);

    g_test_add_func("/char/null", char_null_test);
    g_test_add_func("/char/invalid", char_invalid_test);
    g_test_add_func("/char/ringbuf", char_ringbuf_test);
    g_test_add_func("/char/mux", char_mux_test);
#ifdef CONFIG_HAS_GLIB_SUBPROCESS_TESTS
    g_test_add_func("/char/stdio/subprocess", char_stdio_test_subprocess);
    g_test_add_func("/char/stdio", char_stdio_test);
#endif

    return g_test_run();
}
