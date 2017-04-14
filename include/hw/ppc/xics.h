/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtualized Interrupt System, aka ICS/ICP aka xics
 *
 * Copyright (c) 2010,2011 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef XICS_H
#define XICS_H

#include "hw/sysbus.h"

#define XICS_IPI        0x2
#define XICS_BUID       0x1
#define XICS_IRQ_BASE   (XICS_BUID << 12)

/*
 * We currently only support one BUID which is our interrupt base
 * (the kernel implementation supports more but we don't exploit
 *  that yet)
 */
typedef struct ICPStateClass ICPStateClass;
typedef struct ICPState ICPState;
typedef struct ICSStateClass ICSStateClass;
typedef struct ICSState ICSState;
typedef struct ICSIRQState ICSIRQState;
typedef struct XICSFabric XICSFabric;

#define TYPE_ICP "icp"
#define ICP(obj) OBJECT_CHECK(ICPState, (obj), TYPE_ICP)

#define TYPE_KVM_ICP "icp-kvm"
#define KVM_ICP(obj) OBJECT_CHECK(ICPState, (obj), TYPE_KVM_ICP)

#define ICP_CLASS(klass) \
     OBJECT_CLASS_CHECK(ICPStateClass, (klass), TYPE_ICP)
#define ICP_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ICPStateClass, (obj), TYPE_ICP)

struct ICPStateClass {
    DeviceClass parent_class;

    void (*pre_save)(ICPState *s);
    int (*post_load)(ICPState *s, int version_id);
    void (*cpu_setup)(ICPState *icp, PowerPCCPU *cpu);
};

struct ICPState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/
    CPUState *cs;
    ICSState *xirr_owner;
    uint32_t xirr;
    uint8_t pending_priority;
    uint8_t mfrr;
    qemu_irq output;
    bool cap_irq_xics_enabled;

    XICSFabric *xics;
};

#define TYPE_ICS_BASE "ics-base"
#define ICS_BASE(obj) OBJECT_CHECK(ICSState, (obj), TYPE_ICS_BASE)

/* Retain ics for sPAPR for migration from existing sPAPR guests */
#define TYPE_ICS_SIMPLE "ics"
#define ICS_SIMPLE(obj) OBJECT_CHECK(ICSState, (obj), TYPE_ICS_SIMPLE)

#define TYPE_ICS_KVM "icskvm"
#define ICS_KVM(obj) OBJECT_CHECK(ICSState, (obj), TYPE_ICS_KVM)

#define ICS_BASE_CLASS(klass) \
     OBJECT_CLASS_CHECK(ICSStateClass, (klass), TYPE_ICS_BASE)
#define ICS_BASE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ICSStateClass, (obj), TYPE_ICS_BASE)

struct ICSStateClass {
    DeviceClass parent_class;

    void (*realize)(DeviceState *dev, Error **errp);
    void (*pre_save)(ICSState *s);
    int (*post_load)(ICSState *s, int version_id);
    void (*reject)(ICSState *s, uint32_t irq);
    void (*resend)(ICSState *s);
    void (*eoi)(ICSState *s, uint32_t irq);
};

struct ICSState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/
    uint32_t nr_irqs;
    uint32_t offset;
    qemu_irq *qirqs;
    ICSIRQState *irqs;
    XICSFabric *xics;
};

static inline bool ics_valid_irq(ICSState *ics, uint32_t nr)
{
    return (ics->offset != 0) && (nr >= ics->offset)
        && (nr < (ics->offset + ics->nr_irqs));
}

struct ICSIRQState {
    uint32_t server;
    uint8_t priority;
    uint8_t saved_priority;
#define XICS_STATUS_ASSERTED           0x1
#define XICS_STATUS_SENT               0x2
#define XICS_STATUS_REJECTED           0x4
#define XICS_STATUS_MASKED_PENDING     0x8
    uint8_t status;
/* (flags & XICS_FLAGS_IRQ_MASK) == 0 means the interrupt is not allocated */
#define XICS_FLAGS_IRQ_LSI             0x1
#define XICS_FLAGS_IRQ_MSI             0x2
#define XICS_FLAGS_IRQ_MASK            0x3
    uint8_t flags;
};

struct XICSFabric {
    Object parent;
};

#define TYPE_XICS_FABRIC "xics-fabric"
#define XICS_FABRIC(obj)                                     \
    OBJECT_CHECK(XICSFabric, (obj), TYPE_XICS_FABRIC)
#define XICS_FABRIC_CLASS(klass)                                     \
    OBJECT_CLASS_CHECK(XICSFabricClass, (klass), TYPE_XICS_FABRIC)
#define XICS_FABRIC_GET_CLASS(obj)                                   \
    OBJECT_GET_CLASS(XICSFabricClass, (obj), TYPE_XICS_FABRIC)

typedef struct XICSFabricClass {
    InterfaceClass parent;
    ICSState *(*ics_get)(XICSFabric *xi, int irq);
    void (*ics_resend)(XICSFabric *xi);
    ICPState *(*icp_get)(XICSFabric *xi, int server);
} XICSFabricClass;

#define XICS_IRQS_SPAPR               1024

int spapr_ics_alloc(ICSState *ics, int irq_hint, bool lsi, Error **errp);
int spapr_ics_alloc_block(ICSState *ics, int num, bool lsi, bool align,
                           Error **errp);
void spapr_ics_free(ICSState *ics, int irq, int num);
void spapr_dt_xics(int nr_servers, void *fdt, uint32_t phandle);

qemu_irq xics_get_qirq(XICSFabric *xi, int irq);
ICPState *xics_icp_get(XICSFabric *xi, int server);
void xics_cpu_setup(XICSFabric *xi, PowerPCCPU *cpu);
void xics_cpu_destroy(XICSFabric *xi, PowerPCCPU *cpu);

/* Internal XICS interfaces */
int xics_get_cpu_index_by_dt_id(int cpu_dt_id);

void icp_set_cppr(ICPState *icp, uint8_t cppr);
void icp_set_mfrr(ICPState *icp, uint8_t mfrr);
uint32_t icp_accept(ICPState *ss);
uint32_t icp_ipoll(ICPState *ss, uint32_t *mfrr);
void icp_eoi(ICPState *icp, uint32_t xirr);

void ics_simple_write_xive(ICSState *ics, int nr, int server,
                           uint8_t priority, uint8_t saved_priority);

void ics_set_irq_type(ICSState *ics, int srcno, bool lsi);
void icp_pic_print_info(ICPState *icp, Monitor *mon);
void ics_pic_print_info(ICSState *ics, Monitor *mon);

void ics_resend(ICSState *ics);
void icp_resend(ICPState *ss);

typedef struct sPAPRMachineState sPAPRMachineState;

int xics_kvm_init(sPAPRMachineState *spapr, Error **errp);
int xics_spapr_init(sPAPRMachineState *spapr, Error **errp);

#endif /* XICS_H */
