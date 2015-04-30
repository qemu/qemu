#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "libqtest.h"
#include "libqos/libqos.h"
#include "libqos/pci.h"

/*** Test Setup & Teardown ***/

/**
 * Launch QEMU with the given command line,
 * and then set up interrupts and our guest malloc interface.
 */
QOSState *qtest_vboot(QOSOps *ops, const char *cmdline_fmt, va_list ap)
{
    char *cmdline;

    struct QOSState *qs = g_malloc(sizeof(QOSState));

    cmdline = g_strdup_vprintf(cmdline_fmt, ap);
    qs->qts = qtest_start(cmdline);
    qs->ops = ops;
    qtest_irq_intercept_in(global_qtest, "ioapic");
    if (ops && ops->init_allocator) {
        qs->alloc = ops->init_allocator(ALLOC_NO_FLAGS);
    }

    g_free(cmdline);
    return qs;
}

/**
 * Launch QEMU with the given command line,
 * and then set up interrupts and our guest malloc interface.
 */
QOSState *qtest_boot(QOSOps *ops, const char *cmdline_fmt, ...)
{
    QOSState *qs;
    va_list ap;

    va_start(ap, cmdline_fmt);
    qs = qtest_vboot(ops, cmdline_fmt, ap);
    va_end(ap);

    return qs;
}

/**
 * Tear down the QEMU instance.
 */
void qtest_shutdown(QOSState *qs)
{
    if (qs->alloc && qs->ops && qs->ops->uninit_allocator) {
        qs->ops->uninit_allocator(qs->alloc);
        qs->alloc = NULL;
    }
    qtest_quit(qs->qts);
    g_free(qs);
}

void mkimg(const char *file, const char *fmt, unsigned size_mb)
{
    gchar *cli;
    bool ret;
    int rc;
    GError *err = NULL;
    char *qemu_img_path;
    gchar *out, *out2;
    char *abs_path;

    qemu_img_path = getenv("QTEST_QEMU_IMG");
    abs_path = realpath(qemu_img_path, NULL);
    assert(qemu_img_path);

    cli = g_strdup_printf("%s create -f %s %s %uM", abs_path,
                          fmt, file, size_mb);
    ret = g_spawn_command_line_sync(cli, &out, &out2, &rc, &err);
    if (err) {
        fprintf(stderr, "%s\n", err->message);
        g_error_free(err);
    }
    g_assert(ret && !err);

    /* In glib 2.34, we have g_spawn_check_exit_status. in 2.12, we don't.
     * glib 2.43.91 implementation assumes that any non-zero is an error for
     * windows, but uses extra precautions for Linux. However,
     * 0 is only possible if the program exited normally, so that should be
     * sufficient for our purposes on all platforms, here. */
    if (rc) {
        fprintf(stderr, "qemu-img returned status code %d\n", rc);
    }
    g_assert(!rc);

    g_free(out);
    g_free(out2);
    g_free(cli);
    free(abs_path);
}

void mkqcow2(const char *file, unsigned size_mb)
{
    return mkimg(file, "qcow2", size_mb);
}

void prepare_blkdebug_script(const char *debug_fn, const char *event)
{
    FILE *debug_file = fopen(debug_fn, "w");
    int ret;

    fprintf(debug_file, "[inject-error]\n");
    fprintf(debug_file, "event = \"%s\"\n", event);
    fprintf(debug_file, "errno = \"5\"\n");
    fprintf(debug_file, "state = \"1\"\n");
    fprintf(debug_file, "immediately = \"off\"\n");
    fprintf(debug_file, "once = \"on\"\n");

    fprintf(debug_file, "[set-state]\n");
    fprintf(debug_file, "event = \"%s\"\n", event);
    fprintf(debug_file, "new_state = \"2\"\n");
    fflush(debug_file);
    g_assert(!ferror(debug_file));

    ret = fclose(debug_file);
    g_assert(ret == 0);
}
