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

enum QSPSortBy {
    QSP_SORT_BY_TOTAL_WAIT_TIME,
    QSP_SORT_BY_AVG_WAIT_TIME,
};

void qsp_report(FILE *f, fprintf_function cpu_fprintf, size_t max,
                enum QSPSortBy sort_by, bool callsite_coalesce);

bool qsp_is_enabled(void);
void qsp_enable(void);
void qsp_disable(void);
void qsp_reset(void);

#endif /* QEMU_QSP_H */
