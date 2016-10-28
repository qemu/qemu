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

#define TYPE_XICS_COMMON "xics-common"
#define XICS_COMMON(obj) OBJECT_CHECK(XICSState, (obj), TYPE_XICS_COMMON)

/*
 * Retain xics as the type name to be compatible for migration. Rest all the
 * functions, class and variables are renamed as xics_spapr.
 */
#define TYPE_XICS_SPAPR "xics"
#define XICS_SPAPR(obj) OBJECT_CHECK(XICSState, (obj), TYPE_XICS_SPAPR)

#define TYPE_XICS_SPAPR_KVM "xics-spapr-kvm"
#define XICS_SPAPR_KVM(obj) \
     OBJECT_CHECK(KVMXICSState, (obj), TYPE_XICS_SPAPR_KVM)

#define XICS_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(XICSStateClass, (klass), TYPE_XICS_COMMON)
#define XICS_SPAPR_CLASS(klass) \
     OBJECT_CLASS_CHECK(XICSStateClass, (klass), TYPE_XICS_SPAPR)
#define XICS_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XICSStateClass, (obj), TYPE_XICS_COMMON)
#define XICS_SPAPR_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XICSStateClass, (obj), TYPE_XICS_SPAPR)

#define XICS_IPI        0x2
#define XICS_BUID       0x1
#define XICS_IRQ_BASE   (XICS_BUID << 12)

/*
 * We currently only support one BUID which is our interrupt base
 * (the kernel implementation supports more but we don't exploit
 *  that yet)
 */
typedef struct XICSStateClass XICSStateClass;
typedef struct XICSState XICSState;
typedef struct ICPStateClass ICPStateClass;
typedef struct ICPState ICPState;
typedef struct ICSStateClass ICSStateClass;
typedef struct ICSState ICSState;
typedef struct ICSIRQState ICSIRQState;

struct XICSStateClass {
    DeviceClass parent_class;

    void (*cpu_setup)(XICSState *icp, PowerPCCPU *cpu);
    void (*set_nr_irqs)(XICSState *icp, uint32_t nr_irqs, Error **errp);
    void (*set_nr_servers)(XICSState *icp, uint32_t nr_servers, Error **errp);
};

struct XICSState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    uint32_t nr_servers;
    uint32_t nr_irqs;
    ICPState *ss;
    QLIST_HEAD(, ICSState) ics;
};

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

    XICSState *xics;
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
    XICSState *xics;
    QLIST_ENTRY(ICSState) list;
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

#define XICS_IRQS_SPAPR               1024

qemu_irq xics_get_qirq(XICSState *icp, int irq);
int xics_spapr_alloc(XICSState *icp, int irq_hint, bool lsi, Error **errp);
int xics_spapr_alloc_block(XICSState *icp, int num, bool lsi, bool align,
                           Error **errp);
void xics_spapr_free(XICSState *icp, int irq, int num);
void spapr_dt_xics(XICSState *xics, void *fdt, uint32_t phandle);

void xics_cpu_setup(XICSState *icp, PowerPCCPU *cpu);
void xics_cpu_destroy(XICSState *icp, PowerPCCPU *cpu);
void xics_set_nr_servers(XICSState *xics, uint32_t nr_servers,
                         const char *typename, Error **errp);

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

ICSState *xics_find_source(XICSState *icp, int irq);

#endif /* XICS_H */
