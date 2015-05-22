#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void set_context(QOSState *s)
{
    global_qtest = s->qts;
}

static QDict *qmp_execute(const char *command)
{
    char *fmt;
    QDict *rsp;

    fmt = g_strdup_printf("{ 'execute': '%s' }", command);
    rsp = qmp(fmt);
    g_free(fmt);

    return rsp;
}

void migrate(QOSState *from, QOSState *to, const char *uri)
{
    const char *st;
    char *s;
    QDict *rsp, *sub;
    bool running;

    set_context(from);

    /* Is the machine currently running? */
    rsp = qmp_execute("query-status");
    g_assert(qdict_haskey(rsp, "return"));
    sub = qdict_get_qdict(rsp, "return");
    g_assert(qdict_haskey(sub, "running"));
    running = qdict_get_bool(sub, "running");
    QDECREF(rsp);

    /* Issue the migrate command. */
    s = g_strdup_printf("{ 'execute': 'migrate',"
                        "'arguments': { 'uri': '%s' } }",
                        uri);
    rsp = qmp(s);
    g_free(s);
    g_assert(qdict_haskey(rsp, "return"));
    QDECREF(rsp);

    /* Wait for STOP event, but only if we were running: */
    if (running) {
        qmp_eventwait("STOP");
    }

    /* If we were running, we can wait for an event. */
    if (running) {
        migrate_allocator(from->alloc, to->alloc);
        set_context(to);
        qmp_eventwait("RESUME");
        return;
    }

    /* Otherwise, we need to wait: poll until migration is completed. */
    while (1) {
        rsp = qmp_execute("query-migrate");
        g_assert(qdict_haskey(rsp, "return"));
        sub = qdict_get_qdict(rsp, "return");
        g_assert(qdict_haskey(sub, "status"));
        st = qdict_get_str(sub, "status");

        /* "setup", "active", "completed", "failed", "cancelled" */
        if (strcmp(st, "completed") == 0) {
            QDECREF(rsp);
            break;
        }

        if ((strcmp(st, "setup") == 0) || (strcmp(st, "active") == 0)) {
            QDECREF(rsp);
            g_usleep(5000);
            continue;
        }

        fprintf(stderr, "Migration did not complete, status: %s\n", st);
        g_assert_not_reached();
    }

    migrate_allocator(from->alloc, to->alloc);
    set_context(to);
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
