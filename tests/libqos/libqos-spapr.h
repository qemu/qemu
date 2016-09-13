#ifndef LIBQOS_SPAPR_H
#define LIBQOS_SPAPR_H

#include "libqos/libqos.h"

QOSState *qtest_spapr_vboot(const char *cmdline_fmt, va_list ap);
QOSState *qtest_spapr_boot(const char *cmdline_fmt, ...);
void qtest_spapr_shutdown(QOSState *qs);

#endif
