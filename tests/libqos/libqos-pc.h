#ifndef LIBQOS_PC_H
#define LIBQOS_PC_H

#include "libqos/libqos.h"

QOSState *qtest_pc_vboot(const char *cmdline_fmt, va_list ap);
QOSState *qtest_pc_boot(const char *cmdline_fmt, ...);
void qtest_pc_shutdown(QOSState *qs);

#endif
