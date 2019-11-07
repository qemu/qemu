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

#include "hw/ppc/spapr_irq.h"
#include "hw/ppc/xive.h"

#define TYPE_SPAPR_XIVE "spapr-xive"
#define SPAPR_XIVE(obj) OBJECT_CHECK(SpaprXive, (obj), TYPE_SPAPR_XIVE)

typedef struct SpaprXive {
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

    /* KVM support */
    int           fd;
    void          *tm_mmap;
    MemoryRegion  tm_mmio_kvm;
    VMChangeStateEntry *change;
} SpaprXive;

/*
 * The sPAPR machine has a unique XIVE IC device. Assign a fixed value
 * to the controller block id value. It can nevertheless be changed
 * for testing purpose.
 */
#define SPAPR_XIVE_BLOCK_ID 0x0

void spapr_xive_pic_print_info(SpaprXive *xive, Monitor *mon);

void spapr_xive_hcall_init(SpaprMachineState *spapr);
void spapr_xive_mmio_set_enabled(SpaprXive *xive, bool enable);
void spapr_xive_map_mmio(SpaprXive *xive);

int spapr_xive_end_to_target(uint8_t end_blk, uint32_t end_idx,
                             uint32_t *out_server, uint8_t *out_prio);

/*
 * KVM XIVE device helpers
 */
int kvmppc_xive_connect(SpaprInterruptController *intc, Error **errp);
void kvmppc_xive_disconnect(SpaprInterruptController *intc);
void kvmppc_xive_reset(SpaprXive *xive, Error **errp);
void kvmppc_xive_set_source_config(SpaprXive *xive, uint32_t lisn, XiveEAS *eas,
                                   Error **errp);
void kvmppc_xive_sync_source(SpaprXive *xive, uint32_t lisn, Error **errp);
uint64_t kvmppc_xive_esb_rw(XiveSource *xsrc, int srcno, uint32_t offset,
                            uint64_t data, bool write);
void kvmppc_xive_set_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp);
void kvmppc_xive_get_queue_config(SpaprXive *xive, uint8_t end_blk,
                                 uint32_t end_idx, XiveEND *end,
                                 Error **errp);
void kvmppc_xive_synchronize_state(SpaprXive *xive, Error **errp);
int kvmppc_xive_pre_save(SpaprXive *xive);
int kvmppc_xive_post_load(SpaprXive *xive, int version_id);

#endif /* PPC_SPAPR_XIVE_H */
