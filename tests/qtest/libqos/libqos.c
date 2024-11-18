#include "qemu/osdep.h"
#include "../libqtest.h"
#include "libqos.h"
#include "pci.h"
#include "qobject/qdict.h"

/*** Test Setup & Teardown ***/

/**
 * Launch QEMU with the given command line,
 * and then set up interrupts and our guest malloc interface.
 * Never returns NULL:
 * Terminates the application in case an error is encountered.
 */
QOSState *qtest_vboot(QOSOps *ops, const char *cmdline_fmt, va_list ap)
{
    char *cmdline;

    QOSState *qs = g_new0(QOSState, 1);

    cmdline = g_strdup_vprintf(cmdline_fmt, ap);
    qs->qts = qtest_init(cmdline);
    qs->ops = ops;
    if (ops) {
        ops->alloc_init(&qs->alloc, qs->qts, ALLOC_NO_FLAGS);
        qs->pcibus = ops->qpci_new(qs->qts, &qs->alloc);
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
void qtest_common_shutdown(QOSState *qs)
{
    if (qs->ops) {
        if (qs->pcibus && qs->ops->qpci_free) {
            qs->ops->qpci_free(qs->pcibus);
            qs->pcibus = NULL;
        }
    }
    alloc_destroy(&qs->alloc);
    qtest_quit(qs->qts);
    g_free(qs);
}

void qtest_shutdown(QOSState *qs)
{
    if (qs->ops && qs->ops->shutdown) {
        qs->ops->shutdown(qs);
    } else {
        qtest_common_shutdown(qs);
    }
}

static QDict *qmp_execute(QTestState *qts, const char *command)
{
    return qtest_qmp(qts, "{ 'execute': %s }", command);
}

void migrate(QOSState *from, QOSState *to, const char *uri)
{
    const char *st;
    QDict *rsp, *sub;
    bool running;

    /* Is the machine currently running? */
    rsp = qmp_execute(from->qts, "query-status");
    g_assert(qdict_haskey(rsp, "return"));
    sub = qdict_get_qdict(rsp, "return");
    g_assert(qdict_haskey(sub, "running"));
    running = qdict_get_bool(sub, "running");
    qobject_unref(rsp);

    /* Issue the migrate command. */
    rsp = qtest_qmp(from->qts,
                    "{ 'execute': 'migrate', 'arguments': { 'uri': %s }}",
                    uri);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);

    /* Wait for STOP event, but only if we were running: */
    if (running) {
        qtest_qmp_eventwait(from->qts, "STOP");
    }

    /* If we were running, we can wait for an event. */
    if (running) {
        migrate_allocator(&from->alloc, &to->alloc);
        qtest_qmp_eventwait(to->qts, "RESUME");
        return;
    }

    /* Otherwise, we need to wait: poll until migration is completed. */
    while (1) {
        rsp = qmp_execute(from->qts, "query-migrate");
        g_assert(qdict_haskey(rsp, "return"));
        sub = qdict_get_qdict(rsp, "return");
        g_assert(qdict_haskey(sub, "status"));
        st = qdict_get_str(sub, "status");

        /* "setup", "active", "device", "completed", "failed", "cancelled" */
        if (strcmp(st, "completed") == 0) {
            qobject_unref(rsp);
            break;
        }

        if ((strcmp(st, "setup") == 0) || (strcmp(st, "active") == 0)
            || (strcmp(st, "device") == 0)
            || (strcmp(st, "wait-unplug") == 0)) {
            qobject_unref(rsp);
            g_usleep(5000);
            continue;
        }

        fprintf(stderr, "Migration did not complete, status: %s\n", st);
        g_assert_not_reached();
    }

    migrate_allocator(&from->alloc, &to->alloc);
}

void mkqcow2(const char *file, unsigned size_mb)
{
    g_assert_true(mkimg(file, "qcow2", size_mb));
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

void generate_pattern(void *buffer, size_t len, size_t cycle_len)
{
    int i, j;
    unsigned char *tx = (unsigned char *)buffer;
    unsigned char p;
    size_t *sx;

    /* Write an indicative pattern that varies and is unique per-cycle */
    p = rand() % 256;
    for (i = 0; i < len; i++) {
        tx[i] = p++ % 256;
        if (i % cycle_len == 0) {
            p = rand() % 256;
        }
    }

    /* force uniqueness by writing an id per-cycle */
    for (i = 0; i < len / cycle_len; i++) {
        j = i * cycle_len;
        if (j + sizeof(*sx) <= len) {
            sx = (size_t *)&tx[j];
            *sx = i;
        }
    }
}
