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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/ppc/spapr_numa.h"
#include "hw/ppc/fdt.h"

/*
 * Helper that writes ibm,associativity-reference-points and
 * max-associativity-domains in the RTAS pointed by @rtas
 * in the DT @fdt.
 */
void spapr_numa_write_rtas_dt(SpaprMachineState *spapr, void *fdt, int rtas)
{
    SpaprMachineClass *smc = SPAPR_MACHINE_GET_CLASS(spapr);
    uint32_t refpoints[] = {
        cpu_to_be32(0x4),
        cpu_to_be32(0x4),
        cpu_to_be32(0x2),
    };
    uint32_t nr_refpoints = ARRAY_SIZE(refpoints);
    uint32_t maxdomain = cpu_to_be32(spapr->gpu_numa_id > 1 ? 1 : 0);
    uint32_t maxdomains[] = {
        cpu_to_be32(4),
        maxdomain,
        maxdomain,
        maxdomain,
        cpu_to_be32(spapr->gpu_numa_id),
    };

    if (smc->pre_5_1_assoc_refpoints) {
        nr_refpoints = 2;
    }

    _FDT(fdt_setprop(fdt, rtas, "ibm,associativity-reference-points",
                     refpoints, nr_refpoints * sizeof(refpoints[0])));

    _FDT(fdt_setprop(fdt, rtas, "ibm,max-associativity-domains",
                     maxdomains, sizeof(maxdomains)));
}
