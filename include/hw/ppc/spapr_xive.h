/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_SPAPR_XIVE_H
#define PPC_SPAPR_XIVE_H

#include "hw/ppc/xive.h"

#define TYPE_SPAPR_XIVE "spapr-xive"
#define SPAPR_XIVE(obj) OBJECT_CHECK(sPAPRXive, (obj), TYPE_SPAPR_XIVE)

typedef struct sPAPRXive {
    XiveRouter    parent;

    /* Internal interrupt source for IPIs and virtual devices */
    XiveSource    source;
    hwaddr        vc_base;

    /* END ESB MMIOs */
    XiveENDSource end_source;
    hwaddr        end_base;

    /* DT */
    gchar *nodename;

    /* Routing table */
    XiveEAS       *eat;
    uint32_t      nr_irqs;
    XiveEND       *endt;
    uint32_t      nr_ends;

    /* TIMA mapping address */
    hwaddr        tm_base;
    MemoryRegion  tm_mmio;
} sPAPRXive;

bool spapr_xive_irq_claim(sPAPRXive *xive, uint32_t lisn, bool lsi);
bool spapr_xive_irq_free(sPAPRXive *xive, uint32_t lisn);
void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon);

void spapr_xive_hcall_init(sPAPRMachineState *spapr);
void spapr_dt_xive(sPAPRMachineState *spapr, uint32_t nr_servers, void *fdt,
                   uint32_t phandle);
void spapr_xive_set_tctx_os_cam(XiveTCTX *tctx);
void spapr_xive_mmio_set_enabled(sPAPRXive *xive, bool enable);

#endif /* PPC_SPAPR_XIVE_H */
