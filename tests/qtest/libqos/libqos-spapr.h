#ifndef LIBQOS_SPAPR_H
#define LIBQOS_SPAPR_H

#include "libqos.h"

QOSState *qtest_spapr_vboot(const char *cmdline_fmt, va_list ap)
    G_GNUC_PRINTF(1, 0);
QOSState *qtest_spapr_boot(const char *cmdline_fmt, ...)
    G_GNUC_PRINTF(1, 2);
void qtest_spapr_shutdown(QOSState *qs);

#endif
