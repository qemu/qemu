#include "qemu/osdep.h"
#include "libqos/libqos-spapr.h"
#include "libqos/malloc-spapr.h"

static QOSOps qos_ops = {
    .init_allocator = spapr_alloc_init_flags,
    .uninit_allocator = spapr_alloc_uninit
};

QOSState *qtest_spapr_vboot(const char *cmdline_fmt, va_list ap)
{
    return qtest_vboot(&qos_ops, cmdline_fmt, ap);
}

QOSState *qtest_spapr_boot(const char *cmdline_fmt, ...)
{
    QOSState *qs;
    va_list ap;

    va_start(ap, cmdline_fmt);
    qs = qtest_vboot(&qos_ops, cmdline_fmt, ap);
    va_end(ap);

    return qs;
}

void qtest_spapr_shutdown(QOSState *qs)
{
    return qtest_shutdown(qs);
}
