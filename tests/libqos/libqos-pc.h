#ifndef __libqos_pc_h
#define __libqos_pc_h

#include "libqos/libqos.h"

QOSState *qtest_pc_boot(const char *cmdline_fmt, ...);
void qtest_pc_shutdown(QOSState *qs);

#endif
