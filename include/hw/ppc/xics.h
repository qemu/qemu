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
#if !defined(__XICS_H__)
#define __XICS_H__

#include "hw/sysbus.h"

#define TYPE_XICS "xics"
#define XICS(obj) OBJECT_CHECK(XICSState, (obj), TYPE_XICS)

#define XICS_IPI        0x2
#define XICS_BUID       0x1
#define XICS_IRQ_BASE   (XICS_BUID << 12)

/*
 * We currently only support one BUID which is our interrupt base
 * (the kernel implementation supports more but we don't exploit
 *  that yet)
 */
typedef struct XICSState XICSState;
typedef struct ICPState ICPState;
typedef struct ICSState ICSState;
typedef struct ICSIRQState ICSIRQState;

struct XICSState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    uint32_t nr_servers;
    uint32_t nr_irqs;
    ICPState *ss;
    ICSState *ics;
};

#define TYPE_ICP "icp"
#define ICP(obj) OBJECT_CHECK(ICPState, (obj), TYPE_ICP)

struct ICPState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/
    uint32_t xirr;
    uint8_t pending_priority;
    uint8_t mfrr;
    qemu_irq output;
};

#define TYPE_ICS "ics"
#define ICS(obj) OBJECT_CHECK(ICSState, (obj), TYPE_ICS)

struct ICSState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/
    uint32_t nr_irqs;
    uint32_t offset;
    qemu_irq *qirqs;
    bool *islsi;
    ICSIRQState *irqs;
    XICSState *icp;
};

struct ICSIRQState {
    uint32_t server;
    uint8_t priority;
    uint8_t saved_priority;
#define XICS_STATUS_ASSERTED           0x1
#define XICS_STATUS_SENT               0x2
#define XICS_STATUS_REJECTED           0x4
#define XICS_STATUS_MASKED_PENDING     0x8
    uint8_t status;
};

qemu_irq xics_get_qirq(XICSState *icp, int irq);
void xics_set_irq_type(XICSState *icp, int irq, bool lsi);

void xics_cpu_setup(XICSState *icp, PowerPCCPU *cpu);

#endif /* __XICS_H__ */
