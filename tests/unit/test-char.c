#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "chardev/char-fe.h"
#include "system/system.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-char.h"
#include "qobject/qdict.h"
#include "qom/qom-qobject.h"
#include "io/channel-socket.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-sockets.h"
#include "socket-helpers.h"

static bool quit;

typedef struct FeHandler {
    int read_count;
    bool is_open;
    int openclose_count;
    bool openclose_mismatch;
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

static void fe_event(void *opaque, QEMUChrEvent event)
{
    FeHandler *h = opaque;
    bool new_open_state;

    h->last_event = event;
    switch (event) {
    case CHR_EVENT_BREAK:
        break;
    case CHR_EVENT_OPENED:
    case CHR_EVENT_CLOSED:
        h->openclose_count++;
        new_open_state = (event == CHR_EVENT_OPENED);
        if (h->is_open == new_open_state) {
            h->openclose_mismatch = true;
        }
        h->is_open = new_open_state;
        /* fallthrough */
    default:
        quit = true;
        break;
    }
}

#ifdef _WIN32
static void char_console_test_subprocess(void)
{
    QemuOpts *opts;
    Chardev *chr;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "console-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "console", &error_abort);

    chr = qemu_chr_new_from_opts(opts, NULL, NULL);
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
    CharFrontend c;
    int ret;

    chr = qemu_chr_new("label", "stdio", NULL);
    g_assert_nonnull(chr);

    qemu_chr_fe_init(&c, chr, &error_abort);
    qemu_chr_fe_set_open(&c, true);
    ret = qemu_chr_fe_write(&c, (void *)"buf", 4);
    g_assert_cmpint(ret, ==, 4);

    qemu_chr_fe_deinit(&c, true);
}

static void char_stdio_test(void)
{
    g_test_trap_subprocess("/char/stdio/subprocess", 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stdout("buf");
}

static void char_ringbuf_test(void)
{
    QemuOpts *opts;
    Chardev *chr;
    CharFrontend c;
    char *data;
    int ret;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "ringbuf-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);

    qemu_opt_set(opts, "size", "5", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, NULL);
    g_assert_null(chr);
    qemu_opts_del(opts);

    opts = qemu_opts_create(qemu_find_opts("chardev"), "ringbuf-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", "2", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr);
    qemu_opts_del(opts);

    qemu_chr_fe_init(&c, chr, &error_abort);
    ret = qemu_chr_fe_write(&c, (void *)"buff", 4);
    g_assert_cmpint(ret, ==, 4);

    data = qmp_ringbuf_read("ringbuf-label", 4, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "ff");
    g_free(data);

    data = qmp_ringbuf_read("ringbuf-label", 4, false, 0, &error_abort);
    g_assert_cmpstr(data, ==, "");
    g_free(data);

    qemu_chr_fe_deinit(&c, true);

    /* check alias */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "memory-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "memory", &error_abort);
    qemu_opt_set(opts, "size", "2", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, NULL);
    g_assert_nonnull(chr);
    object_unparent(OBJECT(chr));
    qemu_opts_del(opts);
}

static void char_mux_test(void)
{
    QemuOpts *opts;
    Chardev *chr, *base;
    char *data;
    FeHandler h1 = { 0, false, 0, false, }, h2 = { 0, false, 0, false, };
    CharFrontend chr_fe1, chr_fe2;
    Error *error = NULL;

    /* Create mux and chardev to be immediately removed */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "mux-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", "128", &error_abort);
    qemu_opt_set(opts, "mux", "on", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr);
    qemu_opts_del(opts);

    /* Remove just created mux and chardev */
    qmp_chardev_remove("mux-label", &error_abort);
    qmp_chardev_remove("mux-label-base", &error_abort);

    opts = qemu_opts_create(qemu_find_opts("chardev"), "mux-label",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", "128", &error_abort);
    qemu_opt_set(opts, "mux", "on", &error_abort);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr);
    qemu_opts_del(opts);

    qemu_chr_fe_init(&chr_fe1, chr, &error_abort);
    qemu_chr_fe_set_handlers(&chr_fe1,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &h1,
                             NULL, true);

    qemu_chr_fe_init(&chr_fe2, chr, &error_abort);
    qemu_chr_fe_set_handlers(&chr_fe2,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &h2,
                             NULL, true);
    qemu_chr_fe_take_focus(&chr_fe2);

    base = qemu_chr_find("mux-label-base");
    g_assert_cmpint(qemu_chr_be_can_write(base), !=, 0);

    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h1.read_count, ==, 0);
    g_assert_cmpint(h2.read_count, ==, 6);
    g_assert_cmpstr(h2.read_buf, ==, "hello");
    h2.read_count = 0;

    g_assert_cmpint(h1.last_event, !=, 42); /* should be MUX_OUT or OPENED */
    g_assert_cmpint(h2.last_event, !=, 42); /* should be MUX_IN or OPENED */
    /* sending event on the base broadcast to all fe, historical reasons? */
    qemu_chr_be_event(base, 42);
    g_assert_cmpint(h1.last_event, ==, 42);
    g_assert_cmpint(h2.last_event, ==, 42);
    qemu_chr_be_event(chr, -1);
    g_assert_cmpint(h1.last_event, ==, 42);
    g_assert_cmpint(h2.last_event, ==, -1);

    /* switch focus */
    qemu_chr_be_write(base, (void *)"\1b", 2);
    g_assert_cmpint(h1.last_event, ==, 42);
    g_assert_cmpint(h2.last_event, ==, CHR_EVENT_BREAK);

    qemu_chr_be_write(base, (void *)"\1c", 2);
    g_assert_cmpint(h1.last_event, ==, CHR_EVENT_MUX_IN);
    g_assert_cmpint(h2.last_event, ==, CHR_EVENT_MUX_OUT);
    qemu_chr_be_event(chr, -1);
    g_assert_cmpint(h1.last_event, ==, -1);
    g_assert_cmpint(h2.last_event, ==, CHR_EVENT_MUX_OUT);

    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h2.read_count, ==, 0);
    g_assert_cmpint(h1.read_count, ==, 6);
    g_assert_cmpstr(h1.read_buf, ==, "hello");
    h1.read_count = 0;

    qemu_chr_be_write(base, (void *)"\1b", 2);
    g_assert_cmpint(h1.last_event, ==, CHR_EVENT_BREAK);
    g_assert_cmpint(h2.last_event, ==, CHR_EVENT_MUX_OUT);

    /* open/close state and corresponding events */
    g_assert_true(qemu_chr_fe_backend_open(&chr_fe1));
    g_assert_true(qemu_chr_fe_backend_open(&chr_fe2));
    g_assert_true(h1.is_open);
    g_assert_false(h1.openclose_mismatch);
    g_assert_true(h2.is_open);
    g_assert_false(h2.openclose_mismatch);

    h1.openclose_count = h2.openclose_count = 0;

    qemu_chr_fe_set_handlers(&chr_fe1, NULL, NULL, NULL, NULL,
                             NULL, NULL, false);
    qemu_chr_fe_set_handlers(&chr_fe2, NULL, NULL, NULL, NULL,
                             NULL, NULL, false);
    g_assert_cmpint(h1.openclose_count, ==, 0);
    g_assert_cmpint(h2.openclose_count, ==, 0);

    h1.is_open = h2.is_open = false;
    qemu_chr_fe_set_handlers(&chr_fe1,
                             NULL,
                             NULL,
                             fe_event,
                             NULL,
                             &h1,
                             NULL, false);
    qemu_chr_fe_set_handlers(&chr_fe2,
                             NULL,
                             NULL,
                             fe_event,
                             NULL,
                             &h2,
                             NULL, false);
    g_assert_cmpint(h1.openclose_count, ==, 1);
    g_assert_false(h1.openclose_mismatch);
    g_assert_cmpint(h2.openclose_count, ==, 1);
    g_assert_false(h2.openclose_mismatch);

    qemu_chr_be_event(base, CHR_EVENT_CLOSED);
    qemu_chr_be_event(base, CHR_EVENT_OPENED);
    g_assert_cmpint(h1.openclose_count, ==, 3);
    g_assert_false(h1.openclose_mismatch);
    g_assert_cmpint(h2.openclose_count, ==, 3);
    g_assert_false(h2.openclose_mismatch);

    qemu_chr_fe_set_handlers(&chr_fe2,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &h2,
                             NULL, false);
    qemu_chr_fe_set_handlers(&chr_fe1,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &h1,
                             NULL, false);

    /* remove first handler */
    qemu_chr_fe_set_handlers(&chr_fe1, NULL, NULL, NULL, NULL,
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

    qemu_chr_fe_deinit(&chr_fe1, false);

    qmp_chardev_remove("mux-label", &error);
    g_assert_cmpstr(error_get_pretty(error), ==, "Chardev 'mux-label' is busy");
    error_free(error);

    qemu_chr_fe_deinit(&chr_fe2, false);
    qmp_chardev_remove("mux-label", &error_abort);
}

static void char_hub_test(void)
{
    QemuOpts *opts;
    Chardev *hub, *chr1, *chr2, *base;
    char *data;
    FeHandler h = { 0, false, 0, false, };
    Error *error = NULL;
    CharFrontend chr_fe;
    int ret, i;

#define RB_SIZE 128

    /*
     * Create invalid hub
     * 1. Create hub without a 'chardevs.N' defined (expect error)
     */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "hub0",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "hub", &error_abort);
    hub = qemu_chr_new_from_opts(opts, NULL, &error);
    g_assert_cmpstr(error_get_pretty(error), ==,
                    "hub: 'chardevs' list is not defined");
    error_free(error);
    error = NULL;
    qemu_opts_del(opts);

    /*
     * Create invalid hub
     * 1. Create chardev with embedded mux: 'mux=on'
     * 2. Create hub which refers mux
     * 3. Create hub which refers chardev already attached
     *    to the mux (already in use, expect error)
     */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "chr0",
                            1, &error_abort);
    qemu_opt_set(opts, "mux", "on", &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", stringify(RB_SIZE), &error_abort);
    base = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(base);
    qemu_opts_del(opts);

    opts = qemu_opts_create(qemu_find_opts("chardev"), "hub0",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "hub", &error_abort);
    qemu_opt_set(opts, "chardevs.0", "chr0", &error_abort);
    hub = qemu_chr_new_from_opts(opts, NULL, &error);
    g_assert_cmpstr(error_get_pretty(error), ==,
                    "hub: multiplexers and hub devices can't be "
                    "stacked, check chardev 'chr0', chardev should "
                    "not be a hub device or have 'mux=on' enabled");
    error_free(error);
    error = NULL;
    qemu_opts_del(opts);

    opts = qemu_opts_create(qemu_find_opts("chardev"), "hub0",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "hub", &error_abort);
    qemu_opt_set(opts, "chardevs.0", "chr0-base", &error_abort);
    hub = qemu_chr_new_from_opts(opts, NULL, &error);
    g_assert_cmpstr(error_get_pretty(error), ==,
                    "chardev 'chr0-base' is already in use");
    error_free(error);
    error = NULL;
    qemu_opts_del(opts);

    /* Finalize chr0 */
    qmp_chardev_remove("chr0", &error_abort);

    /*
     * Create invalid hub with more than maximum allowed backends
     * 1. Create more than maximum allowed 'chardevs.%d' options for
     *    hub (expect error)
     */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "hub0",
                            1, &error_abort);
    for (i = 0; i < 10; i++) {
        char key[32], val[32];

        snprintf(key, sizeof(key), "chardevs.%d", i);
        snprintf(val, sizeof(val), "chr%d", i);
        qemu_opt_set(opts, key, val, &error);
        if (error) {
            char buf[64];

            snprintf(buf, sizeof(buf), "Invalid parameter 'chardevs.%d'", i);
            g_assert_cmpstr(error_get_pretty(error), ==, buf);
            error_free(error);
            break;
        }
    }
    g_assert_nonnull(error);
    error = NULL;
    qemu_opts_del(opts);

    /*
     * Create hub with 2 backend chardevs and 1 frontend and perform
     * data aggregation
     * 1. Create 2 ringbuf backend chardevs
     * 2. Create 1 frontend
     * 3. Create hub which refers 2 backend chardevs
     * 4. Attach hub to a frontend
     * 5. Attach hub to a frontend second time (expect error)
     * 6. Perform data aggregation
     * 7. Remove chr1 ("chr1 is busy", expect error)
     * 8. Remove hub0 ("hub0 is busy", expect error);
     * 9. Finilize frontend, hub and backend chardevs in correct order
     */

    /* Create first chardev */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "chr1",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", stringify(RB_SIZE), &error_abort);
    chr1 = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr1);
    qemu_opts_del(opts);

    /* Create second chardev */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "chr2",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
    qemu_opt_set(opts, "size", stringify(RB_SIZE), &error_abort);
    chr2 = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr2);
    qemu_opts_del(opts);

    /* Create hub0 and refer 2 backend chardevs */
    opts = qemu_opts_create(qemu_find_opts("chardev"), "hub0",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "hub", &error_abort);
    qemu_opt_set(opts, "chardevs.0", "chr1", &error_abort);
    qemu_opt_set(opts, "chardevs.1", "chr2", &error_abort);
    hub = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(hub);
    qemu_opts_del(opts);

    /* Attach hub to a frontend */
    qemu_chr_fe_init(&chr_fe, hub, &error_abort);
    qemu_chr_fe_set_handlers(&chr_fe,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &h,
                             NULL, true);

    /* Fails second time */
    qemu_chr_fe_init(&chr_fe, hub, &error);
    g_assert_cmpstr(error_get_pretty(error), ==, "chardev 'hub0' is already in use");
    error_free(error);
    error = NULL;

    /* Write to backend, chr1 */
    base = qemu_chr_find("chr1");
    g_assert_cmpint(qemu_chr_be_can_write(base), !=, 0);

    qemu_chr_be_write(base, (void *)"hello", 6);
    g_assert_cmpint(h.read_count, ==, 6);
    g_assert_cmpstr(h.read_buf, ==, "hello");
    h.read_count = 0;

    /* Write to backend, chr2 */
    base = qemu_chr_find("chr2");
    g_assert_cmpint(qemu_chr_be_can_write(base), !=, 0);

    qemu_chr_be_write(base, (void *)"olleh", 6);
    g_assert_cmpint(h.read_count, ==, 6);
    g_assert_cmpstr(h.read_buf, ==, "olleh");
    h.read_count = 0;

    /* Write to frontend, chr_be */
    ret = qemu_chr_fe_write(&chr_fe, (void *)"heyhey", 6);
    g_assert_cmpint(ret, ==, 6);

    data = qmp_ringbuf_read("chr1", RB_SIZE, false, 0, &error_abort);
    g_assert_cmpint(strlen(data), ==, 6);
    g_assert_cmpstr(data, ==, "heyhey");
    g_free(data);

    data = qmp_ringbuf_read("chr2", RB_SIZE, false, 0, &error_abort);
    g_assert_cmpint(strlen(data), ==, 6);
    g_assert_cmpstr(data, ==, "heyhey");
    g_free(data);

    /* Can't be removed, depends on hub0 */
    qmp_chardev_remove("chr1", &error);
    g_assert_cmpstr(error_get_pretty(error), ==, "Chardev 'chr1' is busy");
    error_free(error);
    error = NULL;

    /* Can't be removed, depends on frontend chr_be */
    qmp_chardev_remove("hub0", &error);
    g_assert_cmpstr(error_get_pretty(error), ==, "Chardev 'hub0' is busy");
    error_free(error);
    error = NULL;

    /* Finalize frontend */
    qemu_chr_fe_deinit(&chr_fe, false);

    /* Finalize hub0 */
    qmp_chardev_remove("hub0", &error_abort);

    /* Finalize backend chardevs */
    qmp_chardev_remove("chr1", &error_abort);
    qmp_chardev_remove("chr2", &error_abort);

#ifndef _WIN32
    /*
     * Create 3 backend chardevs to simulate EAGAIN and watcher.
     * Mainly copied from char_pipe_test().
     * 1. Create 2 ringbuf backend chardevs
     * 2. Create 1 pipe backend chardev
     * 3. Create 1 frontend
     * 4. Create hub which refers 2 backend chardevs
     * 5. Attach hub to a frontend
     * 6. Perform data aggregation and check watcher
     * 7. Finilize frontend, hub and backend chardevs in correct order
     */
    {
        gchar *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
        gchar *in, *out, *pipe = g_build_filename(tmp_path, "pipe", NULL);
        Chardev *chr3;
        int fd, len;
        char buf[128];

        in = g_strdup_printf("%s.in", pipe);
        if (mkfifo(in, 0600) < 0) {
            abort();
        }
        out = g_strdup_printf("%s.out", pipe);
        if (mkfifo(out, 0600) < 0) {
            abort();
        }

        /* Create first chardev */
        opts = qemu_opts_create(qemu_find_opts("chardev"), "chr1",
                                1, &error_abort);
        qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
        qemu_opt_set(opts, "size", stringify(RB_SIZE), &error_abort);
        chr1 = qemu_chr_new_from_opts(opts, NULL, &error_abort);
        g_assert_nonnull(chr1);
        qemu_opts_del(opts);

        /* Create second chardev */
        opts = qemu_opts_create(qemu_find_opts("chardev"), "chr2",
                                1, &error_abort);
        qemu_opt_set(opts, "backend", "ringbuf", &error_abort);
        qemu_opt_set(opts, "size", stringify(RB_SIZE), &error_abort);
        chr2 = qemu_chr_new_from_opts(opts, NULL, &error_abort);
        g_assert_nonnull(chr2);
        qemu_opts_del(opts);

        /* Create third chardev */
        opts = qemu_opts_create(qemu_find_opts("chardev"), "chr3",
                                1, &error_abort);
        qemu_opt_set(opts, "backend", "pipe", &error_abort);
        qemu_opt_set(opts, "path", pipe, &error_abort);
        chr3 = qemu_chr_new_from_opts(opts, NULL, &error_abort);
        g_assert_nonnull(chr3);

        /* Create hub0 and refer 3 backend chardevs */
        opts = qemu_opts_create(qemu_find_opts("chardev"), "hub0",
                                1, &error_abort);
        qemu_opt_set(opts, "backend", "hub", &error_abort);
        qemu_opt_set(opts, "chardevs.0", "chr1", &error_abort);
        qemu_opt_set(opts, "chardevs.1", "chr2", &error_abort);
        qemu_opt_set(opts, "chardevs.2", "chr3", &error_abort);
        hub = qemu_chr_new_from_opts(opts, NULL, &error_abort);
        g_assert_nonnull(hub);
        qemu_opts_del(opts);

        /* Attach hub to a frontend */
        qemu_chr_fe_init(&chr_fe, hub, &error_abort);
        qemu_chr_fe_set_handlers(&chr_fe,
                                 fe_can_read,
                                 fe_read,
                                 fe_event,
                                 NULL,
                                 &h,
                                 NULL, true);

        /* Write to frontend, chr_be */
        ret = qemu_chr_fe_write(&chr_fe, (void *)"thisis", 6);
        g_assert_cmpint(ret, ==, 6);

        data = qmp_ringbuf_read("chr1", RB_SIZE, false, 0, &error_abort);
        g_assert_cmpint(strlen(data), ==, 6);
        g_assert_cmpstr(data, ==, "thisis");
        g_free(data);

        data = qmp_ringbuf_read("chr2", RB_SIZE, false, 0, &error_abort);
        g_assert_cmpint(strlen(data), ==, 6);
        g_assert_cmpstr(data, ==, "thisis");
        g_free(data);

        fd = open(out, O_RDWR);
        ret = read(fd, buf, sizeof(buf));
        g_assert_cmpint(ret, ==, 6);
        buf[ret] = 0;
        g_assert_cmpstr(buf, ==, "thisis");
        close(fd);

        /* Add watch. 0 indicates no watches if nothing to wait for */
        ret = qemu_chr_fe_add_watch(&chr_fe, G_IO_OUT | G_IO_HUP,
                                    NULL, NULL);
        g_assert_cmpint(ret, ==, 0);

        /*
         * Write to frontend, chr_be, until EAGAIN. Make sure length is
         * power of two to fit nicely the whole pipe buffer.
         */
        len = 0;
        while ((ret = qemu_chr_fe_write(&chr_fe, (void *)"thisisit", 8))
               != -1) {
            len += ret;
        }
        g_assert_cmpint(errno, ==, EAGAIN);

        /* Further all writes should cause EAGAIN */
        ret = qemu_chr_fe_write(&chr_fe, (void *)"b", 1);
        g_assert_cmpint(ret, ==, -1);
        g_assert_cmpint(errno, ==, EAGAIN);

        /*
         * Add watch. Non 0 indicates we have a blocked chardev, which
         * can wakes us up when write is possible.
         */
        ret = qemu_chr_fe_add_watch(&chr_fe, G_IO_OUT | G_IO_HUP,
                                    NULL, NULL);
        g_assert_cmpint(ret, !=, 0);
        g_source_remove(ret);

        /* Drain pipe and ring buffers */
        fd = open(out, O_RDWR);
        while ((ret = read(fd, buf, MIN(sizeof(buf), len))) != -1 && len > 0) {
            len -= ret;
        }
        close(fd);

        data = qmp_ringbuf_read("chr1", RB_SIZE, false, 0, &error_abort);
        g_assert_cmpint(strlen(data), ==, 128);
        g_free(data);

        data = qmp_ringbuf_read("chr2", RB_SIZE, false, 0, &error_abort);
        g_assert_cmpint(strlen(data), ==, 128);
        g_free(data);

        /*
         * Now we are good to go, first repeat "lost" sequence, which
         * was already consumed and drained by the ring buffers, but
         * pipe have not recieved that yet.
         */
        ret = qemu_chr_fe_write(&chr_fe, (void *)"thisisit", 8);
        g_assert_cmpint(ret, ==, 8);

        ret = qemu_chr_fe_write(&chr_fe, (void *)"streamisrestored", 16);
        g_assert_cmpint(ret, ==, 16);

        data = qmp_ringbuf_read("chr1", RB_SIZE, false, 0, &error_abort);
        g_assert_cmpint(strlen(data), ==, 16);
        /* Only last 16 bytes, see big comment above */
        g_assert_cmpstr(data, ==, "streamisrestored");
        g_free(data);

        data = qmp_ringbuf_read("chr2", RB_SIZE, false, 0, &error_abort);
        g_assert_cmpint(strlen(data), ==, 16);
        /* Only last 16 bytes, see big comment above */
        g_assert_cmpstr(data, ==, "streamisrestored");
        g_free(data);

        fd = open(out, O_RDWR);
        ret = read(fd, buf, sizeof(buf));
        g_assert_cmpint(ret, ==, 24);
        buf[ret] = 0;
        /* Both 8 and 16 bytes */
        g_assert_cmpstr(buf, ==, "thisisitstreamisrestored");
        close(fd);

        g_free(in);
        g_free(out);
        g_free(tmp_path);
        g_free(pipe);

        /* Finalize frontend */
        qemu_chr_fe_deinit(&chr_fe, false);

        /* Finalize hub0 */
        qmp_chardev_remove("hub0", &error_abort);

        /* Finalize backend chardevs */
        qmp_chardev_remove("chr1", &error_abort);
        qmp_chardev_remove("chr2", &error_abort);
        qmp_chardev_remove("chr3", &error_abort);
    }
#endif
}

static void websock_server_read(void *opaque, const uint8_t *buf, int size)
{
    g_assert_cmpint(size, ==, 5);
    g_assert(memcmp(buf, "world", size) == 0);
    quit = true;
}


static int websock_server_can_read(void *opaque)
{
    return 10;
}


static bool websock_check_http_headers(char *buf, int size)
{
    int i;
    const char *ans[] = { "HTTP/1.1 101 Switching Protocols\r\n",
                          "Server: QEMU VNC\r\n",
                          "Upgrade: websocket\r\n",
                          "Connection: Upgrade\r\n",
                          "Sec-WebSocket-Accept:",
                          "Sec-WebSocket-Protocol: binary\r\n" };

    for (i = 0; i < 6; i++) {
        if (g_strstr_len(buf, size, ans[i]) == NULL) {
            return false;
        }
    }

    return true;
}


static void websock_client_read(void *opaque, const uint8_t *buf, int size)
{
    const uint8_t ping[] = { 0x89, 0x85,                  /* Ping header */
                             0x07, 0x77, 0x9e, 0xf9,      /* Masking key */
                             0x6f, 0x12, 0xf2, 0x95, 0x68 /* "hello" */ };

    const uint8_t binary[] = { 0x82, 0x85,                  /* Binary header */
                               0x74, 0x90, 0xb9, 0xdf,      /* Masking key */
                               0x03, 0xff, 0xcb, 0xb3, 0x10 /* "world" */ };
    Chardev *chr_client = opaque;

    if (websock_check_http_headers((char *) buf, size)) {
        qemu_chr_fe_write(chr_client->fe, ping, sizeof(ping));
    } else if (buf[0] == 0x8a && buf[1] == 0x05) {
        g_assert(strncmp((char *) buf + 2, "hello", 5) == 0);
        qemu_chr_fe_write(chr_client->fe, binary, sizeof(binary));
    } else {
        g_assert(buf[0] == 0x88 && buf[1] == 0x16);
        g_assert(strncmp((char *) buf + 4, "peer requested close", 10) == 0);
        quit = true;
    }
}


static int websock_client_can_read(void *opaque)
{
    return 4096;
}


static void char_websock_test(void)
{
    QObject *addr;
    QDict *qdict;
    const char *port;
    char *tmp;
    char *handshake_port;
    CharFrontend fe;
    CharFrontend client_fe;
    Chardev *chr_client;
    Chardev *chr = qemu_chr_new("server",
                                "websocket:127.0.0.1:0,server=on,wait=off", NULL);
    const char handshake[] = "GET / HTTP/1.1\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Host: localhost:%s\r\n"
                             "Origin: http://localhost:%s\r\n"
                             "Sec-WebSocket-Key: o9JHNiS3/0/0zYE1wa3yIw==\r\n"
                             "Sec-WebSocket-Version: 13\r\n"
                             "Sec-WebSocket-Protocol: binary\r\n\r\n";
    const uint8_t close[] = { 0x88, 0x82,             /* Close header */
                              0xef, 0xaa, 0xc5, 0x97, /* Masking key */
                              0xec, 0x42              /* Status code */ };

    addr = object_property_get_qobject(OBJECT(chr), "addr", &error_abort);
    qdict = qobject_to(QDict, addr);
    port = qdict_get_str(qdict, "port");
    tmp = g_strdup_printf("tcp:127.0.0.1:%s", port);
    handshake_port = g_strdup_printf(handshake, port, port);
    qobject_unref(qdict);

    qemu_chr_fe_init(&fe, chr, &error_abort);
    qemu_chr_fe_set_handlers(&fe, websock_server_can_read, websock_server_read,
                             NULL, NULL, chr, NULL, true);

    chr_client = qemu_chr_new("client", tmp, NULL);
    qemu_chr_fe_init(&client_fe, chr_client, &error_abort);
    qemu_chr_fe_set_handlers(&client_fe, websock_client_can_read,
                             websock_client_read,
                             NULL, NULL, chr_client, NULL, true);
    g_free(tmp);

    qemu_chr_write_all(chr_client,
                       (uint8_t *) handshake_port,
                       strlen(handshake_port));
    g_free(handshake_port);
    main_loop();

    g_assert(object_property_get_bool(OBJECT(chr), "connected", &error_abort));
    g_assert(object_property_get_bool(OBJECT(chr_client),
                                      "connected", &error_abort));

    qemu_chr_write_all(chr_client, close, sizeof(close));
    main_loop();

    object_unparent(OBJECT(chr_client));
    object_unparent(OBJECT(chr));
}


#ifndef _WIN32
static void char_pipe_test(void)
{
    gchar *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
    gchar *tmp, *in, *out, *pipe = g_build_filename(tmp_path, "pipe", NULL);
    Chardev *chr;
    CharFrontend c;
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
    chr = qemu_chr_new("pipe", tmp, NULL);
    g_assert_nonnull(chr);
    g_free(tmp);

    qemu_chr_fe_init(&c, chr, &error_abort);

    ret = qemu_chr_fe_write(&c, (void *)"pipe-out", 9);
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

    qemu_chr_fe_set_handlers(&c,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             &fe,
                             NULL, true);

    main_loop();

    g_assert_cmpint(fe.read_count, ==, 8);
    g_assert_cmpstr(fe.read_buf, ==, "pipe-in");

    qemu_chr_fe_deinit(&c, true);

    g_assert(g_unlink(in) == 0);
    g_assert(g_unlink(out) == 0);
    g_assert(g_rmdir(tmp_path) == 0);
    g_free(in);
    g_free(out);
    g_free(tmp_path);
    g_free(pipe);
}
#endif

typedef struct SocketIdleData {
    GMainLoop *loop;
    Chardev *chr;
    bool conn_expected;
    CharFrontend *fe;
    CharFrontend *client_fe;
} SocketIdleData;


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

static int make_udp_socket(int *port)
{
    struct sockaddr_in addr = { 0, };
    socklen_t alen = sizeof(addr);
    int ret, sock = qemu_socket(PF_INET, SOCK_DGRAM, 0);

    g_assert_cmpint(sock, >=, 0);
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
    CharFrontend stack_fe, *fe = &stack_fe;
    socklen_t alen = sizeof(other);
    int ret;
    char buf[10];
    char *tmp = NULL;

    if (reuse_chr) {
        chr = reuse_chr;
        fe = chr->fe;
    } else {
        int port;
        sock = make_udp_socket(&port);
        tmp = g_strdup_printf("udp:127.0.0.1:%d", port);
        chr = qemu_chr_new("client", tmp, NULL);
        g_assert_nonnull(chr);

        qemu_chr_fe_init(fe, chr, &error_abort);
    }

    d.chr = chr;
    qemu_chr_fe_set_handlers(fe, socket_can_read_hello, socket_read_hello,
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
        qemu_chr_fe_deinit(fe, true);
    }
    g_free(tmp);
}

static void char_udp_test(void)
{
    char_udp_test_internal(NULL, 0);
}


typedef struct {
    int event;
    bool got_pong;
    CharFrontend *fe;
} CharSocketTestData;


#define SOCKET_PING "Hello"
#define SOCKET_PONG "World"

typedef void (*char_socket_cb)(void *opaque, QEMUChrEvent event);

static void
char_socket_event(void *opaque, QEMUChrEvent event)
{
    CharSocketTestData *data = opaque;
    data->event = event;
}

static void
char_socket_event_with_error(void *opaque, QEMUChrEvent event)
{
    static bool first_error;
    CharSocketTestData *data = opaque;
    CharFrontend *fe = data->fe;
    data->event = event;
    switch (event) {
    case CHR_EVENT_OPENED:
        if (!first_error) {
            first_error = true;
            qemu_chr_fe_disconnect(fe);
        }
        return;
    case CHR_EVENT_CLOSED:
        return;
    default:
        return;
    }
}


static void
char_socket_read(void *opaque, const uint8_t *buf, int size)
{
    CharSocketTestData *data = opaque;
    g_assert_cmpint(size, ==, sizeof(SOCKET_PONG));
    g_assert(memcmp(buf, SOCKET_PONG, size) == 0);
    data->got_pong = true;
}


static int
char_socket_can_read(void *opaque)
{
    return sizeof(SOCKET_PONG);
}


static char *
char_socket_addr_to_opt_str(SocketAddress *addr, bool fd_pass,
                            const char *reconnect, bool is_listen)
{
    if (fd_pass) {
        QIOChannelSocket *ioc = qio_channel_socket_new();
        int fd;
        char *optstr;
        g_assert(!reconnect);
        if (is_listen) {
            qio_channel_socket_listen_sync(ioc, addr, 1, &error_abort);
        } else {
            qio_channel_socket_connect_sync(ioc, addr, &error_abort);
        }
        fd = ioc->fd;
        ioc->fd = -1;
        optstr = g_strdup_printf("socket,id=cdev0,fd=%d%s",
                                 fd, is_listen ? ",server=on,wait=off" : "");
        object_unref(OBJECT(ioc));
        return optstr;
    } else {
        switch (addr->type) {
        case SOCKET_ADDRESS_TYPE_INET:
            return g_strdup_printf("socket,id=cdev0,host=%s,port=%s%s%s",
                                   addr->u.inet.host,
                                   addr->u.inet.port,
                                   reconnect ? reconnect : "",
                                   is_listen ? ",server=on,wait=off" : "");

        case SOCKET_ADDRESS_TYPE_UNIX:
            return g_strdup_printf("socket,id=cdev0,path=%s%s%s",
                                   addr->u.q_unix.path,
                                   reconnect ? reconnect : "",
                                   is_listen ? ",server=on,wait=off" : "");

        default:
            g_assert_not_reached();
        }
    }
}


static int
char_socket_ping_pong(QIOChannel *ioc, Error **errp)
{
    char greeting[sizeof(SOCKET_PING)];
    const char *response = SOCKET_PONG;

    int ret;
    ret = qio_channel_read_all(ioc, greeting, sizeof(greeting), errp);
    if (ret != 0) {
        object_unref(OBJECT(ioc));
        return -1;
    }

    g_assert(memcmp(greeting, SOCKET_PING, sizeof(greeting)) == 0);

    qio_channel_write_all(ioc, response, sizeof(SOCKET_PONG), errp);
    object_unref(OBJECT(ioc));
    return 0;
}


static gpointer
char_socket_server_client_thread(gpointer data)
{
    SocketAddress *addr = data;
    QIOChannelSocket *ioc = qio_channel_socket_new();

    qio_channel_socket_connect_sync(ioc, addr, &error_abort);

    char_socket_ping_pong(QIO_CHANNEL(ioc), &error_abort);

    return NULL;
}


typedef struct {
    SocketAddress *addr;
    bool wait_connected;
    bool fd_pass;
} CharSocketServerTestConfig;


static void char_socket_server_test(gconstpointer opaque)
{
    const CharSocketServerTestConfig *config = opaque;
    Chardev *chr;
    CharFrontend c = {0};
    CharSocketTestData data = {0};
    QObject *qaddr;
    SocketAddress *addr;
    Visitor *v;
    QemuThread thread;
    int ret;
    bool reconnected = false;
    char *optstr;
    QemuOpts *opts;

    g_setenv("QTEST_SILENT_ERRORS", "1", 1);
    /*
     * We rely on config->addr containing "wait=off", otherwise
     * qemu_chr_new() will block until a client connects. We
     * can't spawn our client thread though, because until
     * qemu_chr_new() returns we don't know what TCP port was
     * allocated by the OS
     */
    optstr = char_socket_addr_to_opt_str(config->addr,
                                         config->fd_pass,
                                         NULL,
                                         true);
    opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"),
                                   optstr, true);
    g_assert_nonnull(opts);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    qemu_opts_del(opts);
    g_assert_nonnull(chr);
    g_assert(!object_property_get_bool(OBJECT(chr), "connected", &error_abort));

    qaddr = object_property_get_qobject(OBJECT(chr), "addr", &error_abort);
    g_assert_nonnull(qaddr);

    v = qobject_input_visitor_new(qaddr);
    visit_type_SocketAddress(v, "addr", &addr, &error_abort);
    visit_free(v);
    qobject_unref(qaddr);

    qemu_chr_fe_init(&c, chr, &error_abort);

 reconnect:
    data.event = -1;
    data.fe = &c;
    qemu_chr_fe_set_handlers(&c, NULL, NULL,
                             char_socket_event, NULL,
                             &data, NULL, true);
    g_assert(data.event == -1);

    /*
     * Kick off a thread to act as the "remote" client
     * which just plays ping-pong with us
     */
    qemu_thread_create(&thread, "client",
                       char_socket_server_client_thread,
                       addr, QEMU_THREAD_JOINABLE);
    g_assert(data.event == -1);

    if (config->wait_connected) {
        /* Synchronously accept a connection */
        qemu_chr_wait_connected(chr, &error_abort);
    } else {
        /*
         * Asynchronously accept a connection when the evnt
         * loop reports the listener socket as readable
         */
        while (data.event == -1) {
            main_loop_wait(false);
        }
    }
    g_assert(object_property_get_bool(OBJECT(chr), "connected", &error_abort));
    g_assert(data.event == CHR_EVENT_OPENED);
    data.event = -1;

    /* Send a greeting to the client */
    ret = qemu_chr_fe_write_all(&c, (const uint8_t *)SOCKET_PING,
                                sizeof(SOCKET_PING));
    g_assert_cmpint(ret, ==, sizeof(SOCKET_PING));
    g_assert(data.event == -1);

    /* Setup a callback to receive the reply to our greeting */
    qemu_chr_fe_set_handlers(&c, char_socket_can_read,
                             char_socket_read,
                             char_socket_event, NULL,
                             &data, NULL, true);
    g_assert(data.event == CHR_EVENT_OPENED);
    data.event = -1;

    /* Wait for the client to go away */
    while (data.event == -1) {
        main_loop_wait(false);
    }
    g_assert(!object_property_get_bool(OBJECT(chr), "connected", &error_abort));
    g_assert(data.event == CHR_EVENT_CLOSED);
    g_assert(data.got_pong);

    qemu_thread_join(&thread);

    if (!reconnected) {
        reconnected = true;
        goto reconnect;
    }

    qapi_free_SocketAddress(addr);
    object_unparent(OBJECT(chr));
    g_free(optstr);
    g_unsetenv("QTEST_SILENT_ERRORS");
}


static gpointer
char_socket_client_server_thread(gpointer data)
{
    QIOChannelSocket *ioc = data;
    QIOChannelSocket *cioc;

retry:
    cioc = qio_channel_socket_accept(ioc, &error_abort);
    g_assert_nonnull(cioc);

    if (char_socket_ping_pong(QIO_CHANNEL(cioc), NULL) != 0) {
        goto retry;
    }

    return NULL;
}


typedef struct {
    SocketAddress *addr;
    const char *reconnect;
    bool wait_connected;
    bool fd_pass;
    char_socket_cb event_cb;
} CharSocketClientTestConfig;

static void char_socket_client_dupid_test(gconstpointer opaque)
{
    const CharSocketClientTestConfig *config = opaque;
    QIOChannelSocket *ioc;
    char *optstr;
    Chardev *chr1, *chr2;
    SocketAddress *addr;
    QemuOpts *opts;
    Error *local_err = NULL;

    /*
     * Setup a listener socket and determine get its address
     * so we know the TCP port for the client later
     */
    ioc = qio_channel_socket_new();
    g_assert_nonnull(ioc);
    qio_channel_socket_listen_sync(ioc, config->addr, 1, &error_abort);
    addr = qio_channel_socket_get_local_address(ioc, &error_abort);
    g_assert_nonnull(addr);

    /*
     * Populate the chardev address based on what the server
     * is actually listening on
     */
    optstr = char_socket_addr_to_opt_str(addr,
                                         config->fd_pass,
                                         config->reconnect,
                                         false);

    opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"),
                                   optstr, true);
    g_assert_nonnull(opts);
    chr1 = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    g_assert_nonnull(chr1);
    qemu_chr_wait_connected(chr1, &error_abort);

    chr2 = qemu_chr_new_from_opts(opts, NULL, &local_err);
    g_assert_null(chr2);
    error_free_or_abort(&local_err);

    object_unref(OBJECT(ioc));
    qemu_opts_del(opts);
    object_unparent(OBJECT(chr1));
    qapi_free_SocketAddress(addr);
    g_free(optstr);
}

static void char_socket_client_test(gconstpointer opaque)
{
    const CharSocketClientTestConfig *config = opaque;
    const char_socket_cb event_cb = config->event_cb;
    QIOChannelSocket *ioc;
    char *optstr;
    Chardev *chr;
    CharFrontend c = {0};
    CharSocketTestData data = {0};
    SocketAddress *addr;
    QemuThread thread;
    int ret;
    bool reconnected = false;
    QemuOpts *opts;

    /*
     * Setup a listener socket and determine get its address
     * so we know the TCP port for the client later
     */
    ioc = qio_channel_socket_new();
    g_assert_nonnull(ioc);
    qio_channel_socket_listen_sync(ioc, config->addr, 1, &error_abort);
    addr = qio_channel_socket_get_local_address(ioc, &error_abort);
    g_assert_nonnull(addr);

    /*
     * Kick off a thread to act as the "remote" client
     * which just plays ping-pong with us
     */
    qemu_thread_create(&thread, "client",
                       char_socket_client_server_thread,
                       ioc, QEMU_THREAD_JOINABLE);

    /*
     * Populate the chardev address based on what the server
     * is actually listening on
     */
    optstr = char_socket_addr_to_opt_str(addr,
                                         config->fd_pass,
                                         config->reconnect,
                                         false);

    opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"),
                                   optstr, true);
    g_assert_nonnull(opts);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    qemu_opts_del(opts);
    g_assert_nonnull(chr);

    if (config->reconnect) {
        /*
         * If reconnect is set, the connection will be
         * established in a background thread and we won't
         * see the "connected" status updated until we
         * run the main event loop, or call qemu_chr_wait_connected
         */
        g_assert(!object_property_get_bool(OBJECT(chr), "connected",
                                           &error_abort));
    } else {
        g_assert(object_property_get_bool(OBJECT(chr), "connected",
                                          &error_abort));
    }

    qemu_chr_fe_init(&c, chr, &error_abort);

 reconnect:
    data.event = -1;
    data.fe = &c;
    qemu_chr_fe_set_handlers(&c, NULL, NULL,
                             event_cb, NULL,
                             &data, NULL, true);
    if (config->reconnect) {
        g_assert(data.event == -1);
    } else {
        g_assert(data.event == CHR_EVENT_OPENED);
    }

    if (config->wait_connected) {
        /*
         * Synchronously wait for the connection to complete
         * This should be a no-op if reconnect is not set.
         */
        qemu_chr_wait_connected(chr, &error_abort);
    } else {
        /*
         * Asynchronously wait for the connection to be reported
         * as complete when the background thread reports its
         * status.
         * The loop will short-circuit if reconnect was set
         */
        while (data.event == -1) {
            main_loop_wait(false);
        }
    }
    g_assert(data.event == CHR_EVENT_OPENED);
    data.event = -1;
    g_assert(object_property_get_bool(OBJECT(chr), "connected", &error_abort));

    /* Send a greeting to the server */
    ret = qemu_chr_fe_write_all(&c, (const uint8_t *)SOCKET_PING,
                                sizeof(SOCKET_PING));
    g_assert_cmpint(ret, ==, sizeof(SOCKET_PING));
    g_assert(data.event == -1);

    /* Setup a callback to receive the reply to our greeting */
    qemu_chr_fe_set_handlers(&c, char_socket_can_read,
                             char_socket_read,
                             event_cb, NULL,
                             &data, NULL, true);
    g_assert(data.event == CHR_EVENT_OPENED);
    data.event = -1;

    /* Wait for the server to go away */
    while (data.event == -1) {
        main_loop_wait(false);
    }
    g_assert(data.event == CHR_EVENT_CLOSED);
    g_assert(!object_property_get_bool(OBJECT(chr), "connected", &error_abort));
    g_assert(data.got_pong);
    qemu_thread_join(&thread);

    if (config->reconnect && !reconnected) {
        reconnected = true;
        qemu_thread_create(&thread, "client",
                           char_socket_client_server_thread,
                           ioc, QEMU_THREAD_JOINABLE);
        goto reconnect;
    }

    object_unref(OBJECT(ioc));
    object_unparent(OBJECT(chr));
    qapi_free_SocketAddress(addr);
    g_free(optstr);
}

static void
count_closed_event(void *opaque, QEMUChrEvent event)
{
    int *count = opaque;
    if (event == CHR_EVENT_CLOSED) {
        (*count)++;
    }
}

static void
char_socket_discard_read(void *opaque, const uint8_t *buf, int size)
{
}

static void char_socket_server_two_clients_test(gconstpointer opaque)
{
    SocketAddress *incoming_addr = (gpointer) opaque;
    Chardev *chr;
    CharFrontend c = {0};
    QObject *qaddr;
    SocketAddress *addr;
    Visitor *v;
    char *optstr;
    QemuOpts *opts;
    QIOChannelSocket *ioc1, *ioc2;
    int closed = 0;

    g_setenv("QTEST_SILENT_ERRORS", "1", 1);
    /*
     * We rely on addr containing "wait=off", otherwise
     * qemu_chr_new() will block until a client connects. We
     * can't spawn our client thread though, because until
     * qemu_chr_new() returns we don't know what TCP port was
     * allocated by the OS
     */
    optstr = char_socket_addr_to_opt_str(incoming_addr,
                                         false,
                                         NULL,
                                         true);
    opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"),
                                   optstr, true);
    g_assert_nonnull(opts);
    chr = qemu_chr_new_from_opts(opts, NULL, &error_abort);
    qemu_opts_del(opts);
    g_assert_nonnull(chr);
    g_assert(!object_property_get_bool(OBJECT(chr), "connected", &error_abort));

    qaddr = object_property_get_qobject(OBJECT(chr), "addr", &error_abort);
    g_assert_nonnull(qaddr);

    v = qobject_input_visitor_new(qaddr);
    visit_type_SocketAddress(v, "addr", &addr, &error_abort);
    visit_free(v);
    qobject_unref(qaddr);

    qemu_chr_fe_init(&c, chr, &error_abort);

    qemu_chr_fe_set_handlers(&c, char_socket_can_read, char_socket_discard_read,
                             count_closed_event, NULL,
                             &closed, NULL, true);

    ioc1 = qio_channel_socket_new();
    qio_channel_socket_connect_sync(ioc1, addr, &error_abort);
    qemu_chr_wait_connected(chr, &error_abort);

    /* switch the chardev to another context */
    GMainContext *ctx = g_main_context_new();
    qemu_chr_fe_set_handlers(&c, char_socket_can_read, char_socket_discard_read,
                             count_closed_event, NULL,
                             &closed, ctx, true);

    /* Start a second connection while the first is still connected.
     * It will be placed in the listen() backlog, and connect() will
     * succeed immediately.
     */
    ioc2 = qio_channel_socket_new();
    qio_channel_socket_connect_sync(ioc2, addr, &error_abort);

    object_unref(OBJECT(ioc1));
    /* The two connections should now be processed serially.  */
    while (g_main_context_iteration(ctx, TRUE)) {
        if (closed == 1 && ioc2) {
            object_unref(OBJECT(ioc2));
            ioc2 = NULL;
        }
        if (closed == 2) {
            break;
        }
    }

    qapi_free_SocketAddress(addr);
    object_unparent(OBJECT(chr));
    g_main_context_unref(ctx);
    g_free(optstr);
    g_unsetenv("QTEST_SILENT_ERRORS");
}


#if defined(HAVE_CHARDEV_SERIAL) && !defined(WIN32)
static void char_serial_test(void)
{
    QemuOpts *opts;
    Chardev *chr;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "serial-id",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "serial", &error_abort);
    qemu_opt_set(opts, "path", "/dev/null", &error_abort);

    chr = qemu_chr_new_from_opts(opts, NULL, NULL);
    g_assert_nonnull(chr);
    /* TODO: add more tests with a pty */
    object_unparent(OBJECT(chr));

    qemu_opts_del(opts);
}
#endif

#if defined(HAVE_CHARDEV_PARALLEL) && !defined(WIN32)
static void char_parallel_test(void)
{
    QemuOpts *opts;
    Chardev *chr;

    opts = qemu_opts_create(qemu_find_opts("chardev"), "parallel-id",
                            1, &error_abort);
    qemu_opt_set(opts, "backend", "parallel", &error_abort);
    qemu_opt_set(opts, "path", "/dev/null", &error_abort);

    chr = qemu_chr_new_from_opts(opts, NULL, NULL);
#ifdef __linux__
    /* fails to PPCLAIM, see qemu_chr_open_pp_fd() */
    g_assert_null(chr);
#else
    g_assert_nonnull(chr);
    object_unparent(OBJECT(chr));
#endif

    qemu_opts_del(opts);
}
#endif

#ifndef _WIN32
static void char_file_fifo_test(void)
{
    Chardev *chr;
    CharFrontend c;
    char *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
    char *fifo = g_build_filename(tmp_path, "fifo", NULL);
    char *out = g_build_filename(tmp_path, "out", NULL);
    ChardevFile file = { .in = fifo,
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
                           NULL, &error_abort);

    qemu_chr_fe_init(&c, chr, &error_abort);
    qemu_chr_fe_set_handlers(&c,
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

    qemu_chr_fe_deinit(&c, true);

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
                               NULL, &error_abort);
    }
    ret = qemu_chr_write_all(chr, (uint8_t *)"hello!", 6);
    g_assert_cmpint(ret, ==, 6);

    ret = g_file_get_contents(out, &contents, &length, NULL);
    g_assert(ret == TRUE);
    g_assert_cmpint(length, ==, 6);
    g_assert(strncmp(contents, "hello!", 6) == 0);

    if (!ext_chr) {
        object_unparent(OBJECT(chr));
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
    CharFrontend c;
    int ret;

    chr = qemu_chr_find("label-null");
    g_assert_null(chr);

    chr = qemu_chr_new("label-null", "null", NULL);
    chr = qemu_chr_find("label-null");
    g_assert_nonnull(chr);

    g_assert(qemu_chr_has_feature(chr,
                 QEMU_CHAR_FEATURE_FD_PASS) == false);
    g_assert(qemu_chr_has_feature(chr,
                 QEMU_CHAR_FEATURE_RECONNECTABLE) == false);

    /* check max avail */
    qemu_chr_fe_init(&c, chr, &error_abort);
    qemu_chr_fe_init(&c, chr, &err);
    error_free_or_abort(&err);

    /* deinit & reinit */
    qemu_chr_fe_deinit(&c, false);
    qemu_chr_fe_init(&c, chr, &error_abort);

    qemu_chr_fe_set_open(&c, true);

    qemu_chr_fe_set_handlers(&c,
                             fe_can_read,
                             fe_read,
                             fe_event,
                             NULL,
                             NULL, NULL, true);

    ret = qemu_chr_fe_write(&c, (void *)"buf", 4);
    g_assert_cmpint(ret, ==, 4);

    qemu_chr_fe_deinit(&c, true);
}

static void char_invalid_test(void)
{
    Chardev *chr;
    g_setenv("QTEST_SILENT_ERRORS", "1", 1);
    chr = qemu_chr_new("label-invalid", "invalid", NULL);
    g_assert_null(chr);
    g_unsetenv("QTEST_SILENT_ERRORS");
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
    CharFrontend c;

    gchar *tmp_path = g_dir_make_tmp("qemu-test-char.XXXXXX", NULL);
    char *filename = g_build_filename(tmp_path, "file", NULL);
    ChardevFile file = { .out = filename };
    ChardevBackend backend = { .type = CHARDEV_BACKEND_KIND_FILE,
                               .u.file.data = &file };
    ChardevReturn *ret;

    int port;
    int sock = make_udp_socket(&port);
    g_assert_cmpint(sock, >=, 0);

    chr_args = g_strdup_printf("udp:127.0.0.1:%d", port);

    chr = qemu_chr_new("chardev", chr_args, NULL);
    qemu_chr_fe_init(&c, chr, &error_abort);

    /* check that chardev operates correctly */
    char_udp_test_internal(chr, sock);

    /* set the handler that denies the hotswap */
    qemu_chr_fe_set_handlers(&c, NULL, NULL,
                             NULL, chardev_change_denied, NULL, NULL, true);

    /* now, change is denied and has to keep the old backend operating */
    ret = qmp_chardev_change("chardev", &backend, NULL);
    g_assert(!ret);
    g_assert(c.chr == chr);

    char_udp_test_internal(chr, sock);

    /* now allow the change */
    qemu_chr_fe_set_handlers(&c, NULL, NULL,
                             NULL, chardev_change, NULL, NULL, true);

    /* has to succeed now */
    ret = qmp_chardev_change("chardev", &backend, &error_abort);
    g_assert(c.chr != chr);

    close(sock);
    chr = c.chr;

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

static SocketAddress tcpaddr = {
    .type = SOCKET_ADDRESS_TYPE_INET,
    .u.inet.host = (char *)"127.0.0.1",
    .u.inet.port = (char *)"0",
};
#ifndef WIN32
static SocketAddress unixaddr = {
    .type = SOCKET_ADDRESS_TYPE_UNIX,
    .u.q_unix.path = (char *)"test-char.sock",
};
#endif

int main(int argc, char **argv)
{
    bool has_ipv4, has_ipv6;

    qemu_init_main_loop(&error_abort);
    socket_init();

    g_test_init(&argc, &argv, NULL);

    if (socket_check_protocol_support(&has_ipv4, &has_ipv6) < 0) {
        g_printerr("socket_check_protocol_support() failed\n");
        goto end;
    }

    module_call_init(MODULE_INIT_QOM);
    qemu_add_opts(&qemu_chardev_opts);

    g_test_add_func("/char/null", char_null_test);
    g_test_add_func("/char/invalid", char_invalid_test);
    g_test_add_func("/char/ringbuf", char_ringbuf_test);
    g_test_add_func("/char/mux", char_mux_test);
    g_test_add_func("/char/hub", char_hub_test);
#ifdef _WIN32
    g_test_add_func("/char/console/subprocess", char_console_test_subprocess);
    g_test_add_func("/char/console", char_console_test);
#endif
    g_test_add_func("/char/stdio/subprocess", char_stdio_test_subprocess);
    g_test_add_func("/char/stdio", char_stdio_test);
#ifndef _WIN32
    g_test_add_func("/char/pipe", char_pipe_test);
#endif
    g_test_add_func("/char/file", char_file_test);
#ifndef _WIN32
    g_test_add_func("/char/file-fifo", char_file_fifo_test);
#endif

#define SOCKET_SERVER_TEST(name, addr)                                  \
    static CharSocketServerTestConfig server1 ## name =                 \
        { addr, false, false };                                         \
    static CharSocketServerTestConfig server2 ## name =                 \
        { addr, true, false };                                          \
    static CharSocketServerTestConfig server3 ## name =                 \
        { addr, false, true };                                          \
    static CharSocketServerTestConfig server4 ## name =                 \
        { addr, true, true };                                           \
    g_test_add_data_func("/char/socket/server/mainloop/" # name,        \
                         &server1 ##name, char_socket_server_test);     \
    g_test_add_data_func("/char/socket/server/wait-conn/" # name,       \
                         &server2 ##name, char_socket_server_test);     \
    g_test_add_data_func("/char/socket/server/mainloop-fdpass/" # name, \
                         &server3 ##name, char_socket_server_test);     \
    g_test_add_data_func("/char/socket/server/wait-conn-fdpass/" # name, \
                         &server4 ##name, char_socket_server_test);     \
    g_test_add_data_func("/char/socket/server/two-clients/" # name,     \
                         addr, char_socket_server_two_clients_test)

#define SOCKET_CLIENT_TEST(name, addr)                                  \
    static CharSocketClientTestConfig client1 ## name =                 \
        { addr, NULL, false, false, char_socket_event };                \
    static CharSocketClientTestConfig client2 ## name =                 \
        { addr, NULL, true, false, char_socket_event };                 \
    static CharSocketClientTestConfig client3 ## name =                 \
        { addr, ",reconnect-ms=1000", false, false, char_socket_event }; \
    static CharSocketClientTestConfig client4 ## name =                 \
        { addr, ",reconnect-ms=1000", true, false, char_socket_event }; \
    static CharSocketClientTestConfig client5 ## name =                 \
        { addr, NULL, false, true, char_socket_event };                 \
    static CharSocketClientTestConfig client6 ## name =                 \
        { addr, NULL, true, true, char_socket_event };                  \
    static CharSocketClientTestConfig client7 ## name =                 \
        { addr, ",reconnect-ms=1000", true, false,                      \
            char_socket_event_with_error };                             \
    static CharSocketClientTestConfig client8 ## name =                 \
        { addr, ",reconnect-ms=1000", false, false, char_socket_event };\
    g_test_add_data_func("/char/socket/client/mainloop/" # name,        \
                         &client1 ##name, char_socket_client_test);     \
    g_test_add_data_func("/char/socket/client/wait-conn/" # name,       \
                         &client2 ##name, char_socket_client_test);     \
    g_test_add_data_func("/char/socket/client/mainloop-reconnect/" # name, \
                         &client3 ##name, char_socket_client_test);     \
    g_test_add_data_func("/char/socket/client/wait-conn-reconnect/" # name, \
                         &client4 ##name, char_socket_client_test);     \
    g_test_add_data_func("/char/socket/client/mainloop-fdpass/" # name, \
                         &client5 ##name, char_socket_client_test);     \
    g_test_add_data_func("/char/socket/client/wait-conn-fdpass/" # name, \
                         &client6 ##name, char_socket_client_test);     \
    g_test_add_data_func("/char/socket/client/reconnect-error/" # name, \
                         &client7 ##name, char_socket_client_test);     \
    g_test_add_data_func("/char/socket/client/dupid-reconnect/" # name, \
                         &client8 ##name, char_socket_client_dupid_test)

    if (has_ipv4) {
        SOCKET_SERVER_TEST(tcp, &tcpaddr);
        SOCKET_CLIENT_TEST(tcp, &tcpaddr);
    }
#ifndef WIN32
    SOCKET_SERVER_TEST(unix, &unixaddr);
    SOCKET_CLIENT_TEST(unix, &unixaddr);
#endif

    g_test_add_func("/char/udp", char_udp_test);
#if defined(HAVE_CHARDEV_SERIAL) && !defined(WIN32)
    g_test_add_func("/char/serial", char_serial_test);
#endif
#if defined(HAVE_CHARDEV_PARALLEL) && !defined(WIN32)
    g_test_add_func("/char/parallel", char_parallel_test);
#endif
    g_test_add_func("/char/hotswap", char_hotswap_test);
    g_test_add_func("/char/websocket", char_websock_test);

end:
    return g_test_run();
}
