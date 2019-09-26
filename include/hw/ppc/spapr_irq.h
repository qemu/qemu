/*
 * QEMU PowerPC sPAPR IRQ backend definitions
 *
 * Copyright (c) 2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef HW_SPAPR_IRQ_H
#define HW_SPAPR_IRQ_H

#include "target/ppc/cpu-qom.h"

/*
 * IRQ range offsets per device type
 */
#define SPAPR_IRQ_IPI        0x0

#define SPAPR_XIRQ_BASE      XICS_IRQ_BASE /* 0x1000 */
#define SPAPR_IRQ_EPOW       (SPAPR_XIRQ_BASE + 0x0000)
#define SPAPR_IRQ_HOTPLUG    (SPAPR_XIRQ_BASE + 0x0001)
#define SPAPR_IRQ_VIO        (SPAPR_XIRQ_BASE + 0x0100)  /* 256 VIO devices */
#define SPAPR_IRQ_PCI_LSI    (SPAPR_XIRQ_BASE + 0x0200)  /* 32+ PHBs devices */

/* Offset of the dynamic range covered by the bitmap allocator */
#define SPAPR_IRQ_MSI        (SPAPR_XIRQ_BASE + 0x0300)

#define SPAPR_NR_XIRQS       0x1000
#define SPAPR_NR_MSIS        (SPAPR_XIRQ_BASE + SPAPR_NR_XIRQS - SPAPR_IRQ_MSI)

typedef struct SpaprMachineState SpaprMachineState;

typedef struct SpaprInterruptController SpaprInterruptController;

#define TYPE_SPAPR_INTC "spapr-interrupt-controller"
#define SPAPR_INTC(obj)                                     \
    INTERFACE_CHECK(SpaprInterruptController, (obj), TYPE_SPAPR_INTC)
#define SPAPR_INTC_CLASS(klass)                                     \
    OBJECT_CLASS_CHECK(SpaprInterruptControllerClass, (klass), TYPE_SPAPR_INTC)
#define SPAPR_INTC_GET_CLASS(obj)                                   \
    OBJECT_GET_CLASS(SpaprInterruptControllerClass, (obj), TYPE_SPAPR_INTC)

typedef struct SpaprInterruptControllerClass {
    InterfaceClass parent;

    int (*activate)(SpaprInterruptController *intc, Error **errp);
    void (*deactivate)(SpaprInterruptController *intc);

    /*
     * These methods will typically be called on all intcs, active and
     * inactive
     */
    int (*cpu_intc_create)(SpaprInterruptController *intc,
                            PowerPCCPU *cpu, Error **errp);
    int (*claim_irq)(SpaprInterruptController *intc, int irq, bool lsi,
                     Error **errp);
    void (*free_irq)(SpaprInterruptController *intc, int irq);

    /* These methods should only be called on the active intc */
    void (*set_irq)(SpaprInterruptController *intc, int irq, int val);
    void (*print_info)(SpaprInterruptController *intc, Monitor *mon);
} SpaprInterruptControllerClass;

void spapr_irq_update_active_intc(SpaprMachineState *spapr);

int spapr_irq_cpu_intc_create(SpaprMachineState *spapr,
                              PowerPCCPU *cpu, Error **errp);
void spapr_irq_print_info(SpaprMachineState *spapr, Monitor *mon);

void spapr_irq_msi_init(SpaprMachineState *spapr, uint32_t nr_msis);
int spapr_irq_msi_alloc(SpaprMachineState *spapr, uint32_t num, bool align,
                        Error **errp);
void spapr_irq_msi_free(SpaprMachineState *spapr, int irq, uint32_t num);

typedef struct SpaprIrq {
    uint32_t    nr_xirqs;
    uint32_t    nr_msis;
    bool        xics;
    bool        xive;

    void (*dt_populate)(SpaprMachineState *spapr, uint32_t nr_servers,
                        void *fdt, uint32_t phandle);
    int (*post_load)(SpaprMachineState *spapr, int version_id);
    void (*reset)(SpaprMachineState *spapr, Error **errp);
    void (*init_kvm)(SpaprMachineState *spapr, Error **errp);
} SpaprIrq;

extern SpaprIrq spapr_irq_xics;
extern SpaprIrq spapr_irq_xics_legacy;
extern SpaprIrq spapr_irq_xive;
extern SpaprIrq spapr_irq_dual;

void spapr_irq_init(SpaprMachineState *spapr, Error **errp);
int spapr_irq_claim(SpaprMachineState *spapr, int irq, bool lsi, Error **errp);
void spapr_irq_free(SpaprMachineState *spapr, int irq, int num);
qemu_irq spapr_qirq(SpaprMachineState *spapr, int irq);
int spapr_irq_post_load(SpaprMachineState *spapr, int version_id);
void spapr_irq_reset(SpaprMachineState *spapr, Error **errp);
int spapr_irq_get_phandle(SpaprMachineState *spapr, void *fdt, Error **errp);

/*
 * XICS legacy routines
 */
int spapr_irq_find(SpaprMachineState *spapr, int num, bool align, Error **errp);
#define spapr_irq_findone(spapr, errp) spapr_irq_find(spapr, 1, false, errp)

#endif
