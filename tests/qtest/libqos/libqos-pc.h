#ifndef LIBQOS_PC_H
#define LIBQOS_PC_H

#include "libqos.h"

QOSState *qtest_pc_vboot(const char *cmdline_fmt, va_list ap)
    G_GNUC_PRINTF(1, 0);
QOSState *qtest_pc_boot(const char *cmdline_fmt, ...)
    G_GNUC_PRINTF(1, 2);
void qtest_pc_shutdown(QOSState *qs);

#endif
