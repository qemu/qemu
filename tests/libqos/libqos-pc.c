#include "qemu/osdep.h"
#include "libqos/libqos-pc.h"
#include "libqos/malloc-pc.h"
#include "libqos/pci-pc.h"

static QOSOps qos_ops = {
    .init_allocator = pc_alloc_init_flags,
    .uninit_allocator = pc_alloc_uninit,
    .qpci_init = qpci_init_pc,
    .qpci_free = qpci_free_pc,
    .shutdown = qtest_pc_shutdown,
};

QOSState *qtest_pc_vboot(const char *cmdline_fmt, va_list ap)
{
    return qtest_vboot(&qos_ops, cmdline_fmt, ap);
}

QOSState *qtest_pc_boot(const char *cmdline_fmt, ...)
{
    QOSState *qs;
    va_list ap;

    va_start(ap, cmdline_fmt);
    qs = qtest_vboot(&qos_ops, cmdline_fmt, ap);
    va_end(ap);

    qtest_irq_intercept_in(global_qtest, "ioapic");

    return qs;
}

void qtest_pc_shutdown(QOSState *qs)
{
    return qtest_common_shutdown(qs);
}
