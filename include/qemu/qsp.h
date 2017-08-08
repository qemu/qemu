/*
 * qsp.c - QEMU Synchronization Profiler
 *
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * Note: this header file can *only* be included from thread.h.
 */
#ifndef QEMU_QSP_H
#define QEMU_QSP_H

#include "qemu/fprintf-fn.h"

void qsp_report(FILE *f, fprintf_function cpu_fprintf, size_t max);

bool qsp_is_enabled(void);
void qsp_enable(void);
void qsp_disable(void);

#endif /* QEMU_QSP_H */
