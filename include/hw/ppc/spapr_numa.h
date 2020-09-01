/*
 * QEMU PowerPC pSeries Logical Partition NUMA associativity handling
 *
 * Copyright IBM Corp. 2020
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_SPAPR_NUMA_H
#define HW_SPAPR_NUMA_H

#include "hw/ppc/spapr.h"

void spapr_numa_write_rtas_dt(SpaprMachineState *spapr, void *fdt, int rtas);

#endif /* HW_SPAPR_NUMA_H */
