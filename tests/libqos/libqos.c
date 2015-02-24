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
