/*
 * QEMU PowerPC sPAPR IRQ interface
 *
 * Copyright (c) 2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/xics.h"

void spapr_irq_msi_init(sPAPRMachineState *spapr, uint32_t nr_msis)
{
    spapr->irq_map_nr = nr_msis;
    spapr->irq_map = bitmap_new(spapr->irq_map_nr);
}

int spapr_irq_msi_alloc(sPAPRMachineState *spapr, uint32_t num, bool align,
                        Error **errp)
{
    int irq;

    /*
     * The 'align_mask' parameter of bitmap_find_next_zero_area()
     * should be one less than a power of 2; 0 means no
     * alignment. Adapt the 'align' value of the former allocator
     * to fit the requirements of bitmap_find_next_zero_area()
     */
    align -= 1;

    irq = bitmap_find_next_zero_area(spapr->irq_map, spapr->irq_map_nr, 0, num,
                                     align);
    if (irq == spapr->irq_map_nr) {
        error_setg(errp, "can't find a free %d-IRQ block", num);
        return -1;
    }

    bitmap_set(spapr->irq_map, irq, num);

    return irq + SPAPR_IRQ_MSI;
}

void spapr_irq_msi_free(sPAPRMachineState *spapr, int irq, uint32_t num)
{
    bitmap_clear(spapr->irq_map, irq - SPAPR_IRQ_MSI, num);
}

void spapr_irq_msi_reset(sPAPRMachineState *spapr)
{
    bitmap_clear(spapr->irq_map, 0, spapr->irq_map_nr);
}
