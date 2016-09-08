/*
 * QEMU PowerPC helper routines for the device tree.
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_FDT_H
#define PPC_FDT_H

#include "qemu/error-report.h"

typedef struct CPUPPCState CPUPPCState;

#define _FDT(exp)                                                  \
    do {                                                           \
        int ret = (exp);                                           \
        if (ret < 0) {                                             \
            error_report("error creating device tree: %s: %s",   \
                    #exp, fdt_strerror(ret));                      \
            exit(1);                                               \
        }                                                          \
    } while (0)

size_t ppc_create_page_sizes_prop(CPUPPCState *env, uint32_t *prop,
                                  size_t maxsize);

#endif /* PPC_FDT_H */
