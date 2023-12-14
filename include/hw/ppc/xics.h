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

#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

#define XICS_IPI        0x2
#define XICS_BUID       0x1
#define XICS_IRQ_BASE   (XICS_BUID << 12)

/*
 * We currently only support one BUID which is our interrupt base
 * (the kernel implementation supports more but we don't exploit
 *  that yet)
 */
typedef struct PnvICPState PnvICPState;
typedef struct ICSStateClass ICSStateClass;
typedef struct ICSState ICSState;
typedef struct ICSIRQState ICSIRQState;
typedef struct XICSFabric XICSFabric;

#define TYPE_ICP "icp"
OBJECT_DECLARE_TYPE(ICPState, ICPStateClass,
                    ICP)

#define TYPE_PNV_ICP "pnv-icp"
DECLARE_INSTANCE_CHECKER(PnvICPState, PNV_ICP,
                         TYPE_PNV_ICP)


struct ICPStateClass {
    DeviceClass parent_class;

    DeviceRealize parent_realize;
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

    XICSFabric *xics;
};

#define ICP_PROP_XICS "xics"
#define ICP_PROP_CPU "cpu"

struct PnvICPState {
    ICPState parent_obj;

    MemoryRegion mmio;
    uint32_t links[3];
};

#define TYPE_ICS "ics"
DECLARE_OBJ_CHECKERS(ICSState, ICSStateClass,
                     ICS, TYPE_ICS)


struct ICSStateClass {
    DeviceClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;

    void (*reject)(ICSState *s, uint32_t irq);
    void (*resend)(ICSState *s);
};

struct ICSState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/
    uint32_t nr_irqs;
    uint32_t offset;
    ICSIRQState *irqs;
    XICSFabric *xics;
};

#define ICS_PROP_XICS "xics"

static inline bool ics_valid_irq(ICSState *ics, uint32_t nr)
{
    return (nr >= ics->offset) && (nr < (ics->offset + ics->nr_irqs));
}

struct ICSIRQState {
    uint32_t server;
    uint8_t priority;
    uint8_t saved_priority;
#define XICS_STATUS_ASSERTED           0x1
#define XICS_STATUS_SENT               0x2
#define XICS_STATUS_REJECTED           0x4
#define XICS_STATUS_MASKED_PENDING     0x8
#define XICS_STATUS_PRESENTED          0x10
#define XICS_STATUS_QUEUED             0x20
    uint8_t status;
/* (flags & XICS_FLAGS_IRQ_MASK) == 0 means the interrupt is not allocated */
#define XICS_FLAGS_IRQ_LSI             0x1
#define XICS_FLAGS_IRQ_MSI             0x2
#define XICS_FLAGS_IRQ_MASK            0x3
    uint8_t flags;
};

#define TYPE_XICS_FABRIC "xics-fabric"
#define XICS_FABRIC(obj)                                     \
    INTERFACE_CHECK(XICSFabric, (obj), TYPE_XICS_FABRIC)
typedef struct XICSFabricClass XICSFabricClass;
DECLARE_CLASS_CHECKERS(XICSFabricClass, XICS_FABRIC,
                       TYPE_XICS_FABRIC)

struct XICSFabricClass {
    InterfaceClass parent;
    ICSState *(*ics_get)(XICSFabric *xi, int irq);
    void (*ics_resend)(XICSFabric *xi);
    ICPState *(*icp_get)(XICSFabric *xi, int server);
};

ICPState *xics_icp_get(XICSFabric *xi, int server);

/* Internal XICS interfaces */
void icp_set_cppr(ICPState *icp, uint8_t cppr);
void icp_set_mfrr(ICPState *icp, uint8_t mfrr);
uint32_t icp_accept(ICPState *ss);
uint32_t icp_ipoll(ICPState *ss, uint32_t *mfrr);
void icp_eoi(ICPState *icp, uint32_t xirr);
void icp_irq(ICSState *ics, int server, int nr, uint8_t priority);
void icp_reset(ICPState *icp);

void ics_write_xive(ICSState *ics, int nr, int server,
                    uint8_t priority, uint8_t saved_priority);
void ics_set_irq(void *opaque, int srcno, int val);

static inline bool ics_irq_free(ICSState *ics, uint32_t srcno)
{
    return !(ics->irqs[srcno].flags & XICS_FLAGS_IRQ_MASK);
}

void ics_set_irq_type(ICSState *ics, int srcno, bool lsi);
void icp_pic_print_info(ICPState *icp, Monitor *mon);
void ics_pic_print_info(ICSState *ics, Monitor *mon);

void ics_resend(ICSState *ics);
void icp_resend(ICPState *ss);

Object *icp_create(Object *cpu, const char *type, XICSFabric *xi,
                   Error **errp);
void icp_destroy(ICPState *icp);

/* KVM */
void icp_get_kvm_state(ICPState *icp);
int icp_set_kvm_state(ICPState *icp, Error **errp);
void icp_synchronize_state(ICPState *icp);
void icp_kvm_realize(DeviceState *dev, Error **errp);

void ics_get_kvm_state(ICSState *ics);
int ics_set_kvm_state_one(ICSState *ics, int srcno, Error **errp);
int ics_set_kvm_state(ICSState *ics, Error **errp);
void ics_synchronize_state(ICSState *ics);
void ics_kvm_set_irq(ICSState *ics, int srcno, int val);

#endif /* XICS_H */
