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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "trace.h"
#include "qemu/timer.h"
#include "hw/ppc/xics.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"

int xics_get_cpu_index_by_dt_id(int cpu_dt_id)
{
    PowerPCCPU *cpu = ppc_get_vcpu_by_dt_id(cpu_dt_id);

    if (cpu) {
        return cpu->parent_obj.cpu_index;
    }

    return -1;
}

void xics_cpu_destroy(XICSState *xics, PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    ICPState *ss = &xics->ss[cs->cpu_index];

    assert(cs->cpu_index < xics->nr_servers);
    assert(cs == ss->cs);

    ss->output = NULL;
    ss->cs = NULL;
}

void xics_cpu_setup(XICSState *xics, PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    ICPState *ss = &xics->ss[cs->cpu_index];
    XICSStateClass *info = XICS_COMMON_GET_CLASS(xics);

    assert(cs->cpu_index < xics->nr_servers);

    ss->cs = cs;

    if (info->cpu_setup) {
        info->cpu_setup(xics, cpu);
    }

    switch (PPC_INPUT(env)) {
    case PPC_FLAGS_INPUT_POWER7:
        ss->output = env->irq_inputs[POWER7_INPUT_INT];
        break;

    case PPC_FLAGS_INPUT_970:
        ss->output = env->irq_inputs[PPC970_INPUT_INT];
        break;

    default:
        error_report("XICS interrupt controller does not support this CPU "
                     "bus model");
        abort();
    }
}

/*
 * XICS Common class - parent for emulated XICS and KVM-XICS
 */
static void xics_common_reset(DeviceState *d)
{
    XICSState *xics = XICS_COMMON(d);
    int i;

    for (i = 0; i < xics->nr_servers; i++) {
        device_reset(DEVICE(&xics->ss[i]));
    }

    device_reset(DEVICE(xics->ics));
}

static void xics_prop_get_nr_irqs(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    XICSState *xics = XICS_COMMON(obj);
    int64_t value = xics->nr_irqs;

    visit_type_int(v, name, &value, errp);
}

static void xics_prop_set_nr_irqs(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    XICSState *xics = XICS_COMMON(obj);
    XICSStateClass *info = XICS_COMMON_GET_CLASS(xics);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (xics->nr_irqs) {
        error_setg(errp, "Number of interrupts is already set to %u",
                   xics->nr_irqs);
        return;
    }

    assert(info->set_nr_irqs);
    assert(xics->ics);
    info->set_nr_irqs(xics, value, errp);
}

static void xics_prop_get_nr_servers(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    XICSState *xics = XICS_COMMON(obj);
    int64_t value = xics->nr_servers;

    visit_type_int(v, name, &value, errp);
}

static void xics_prop_set_nr_servers(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    XICSState *xics = XICS_COMMON(obj);
    XICSStateClass *info = XICS_COMMON_GET_CLASS(xics);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (xics->nr_servers) {
        error_setg(errp, "Number of servers is already set to %u",
                   xics->nr_servers);
        return;
    }

    assert(info->set_nr_servers);
    info->set_nr_servers(xics, value, errp);
}

static void xics_common_initfn(Object *obj)
{
    object_property_add(obj, "nr_irqs", "int",
                        xics_prop_get_nr_irqs, xics_prop_set_nr_irqs,
                        NULL, NULL, NULL);
    object_property_add(obj, "nr_servers", "int",
                        xics_prop_get_nr_servers, xics_prop_set_nr_servers,
                        NULL, NULL, NULL);
}

static void xics_common_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = xics_common_reset;
}

static const TypeInfo xics_common_info = {
    .name          = TYPE_XICS_COMMON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XICSState),
    .class_size    = sizeof(XICSStateClass),
    .instance_init = xics_common_initfn,
    .class_init    = xics_common_class_init,
};

/*
 * ICP: Presentation layer
 */

#define XISR_MASK  0x00ffffff
#define CPPR_MASK  0xff000000

#define XISR(ss)   (((ss)->xirr) & XISR_MASK)
#define CPPR(ss)   (((ss)->xirr) >> 24)

static void ics_reject(ICSState *ics, int nr);
static void ics_resend(ICSState *ics);
static void ics_eoi(ICSState *ics, int nr);

static void icp_check_ipi(XICSState *xics, int server)
{
    ICPState *ss = xics->ss + server;

    if (XISR(ss) && (ss->pending_priority <= ss->mfrr)) {
        return;
    }

    trace_xics_icp_check_ipi(server, ss->mfrr);

    if (XISR(ss)) {
        ics_reject(xics->ics, XISR(ss));
    }

    ss->xirr = (ss->xirr & ~XISR_MASK) | XICS_IPI;
    ss->pending_priority = ss->mfrr;
    qemu_irq_raise(ss->output);
}

static void icp_resend(XICSState *xics, int server)
{
    ICPState *ss = xics->ss + server;

    if (ss->mfrr < CPPR(ss)) {
        icp_check_ipi(xics, server);
    }
    ics_resend(xics->ics);
}

void icp_set_cppr(XICSState *xics, int server, uint8_t cppr)
{
    ICPState *ss = xics->ss + server;
    uint8_t old_cppr;
    uint32_t old_xisr;

    old_cppr = CPPR(ss);
    ss->xirr = (ss->xirr & ~CPPR_MASK) | (cppr << 24);

    if (cppr < old_cppr) {
        if (XISR(ss) && (cppr <= ss->pending_priority)) {
            old_xisr = XISR(ss);
            ss->xirr &= ~XISR_MASK; /* Clear XISR */
            ss->pending_priority = 0xff;
            qemu_irq_lower(ss->output);
            ics_reject(xics->ics, old_xisr);
        }
    } else {
        if (!XISR(ss)) {
            icp_resend(xics, server);
        }
    }
}

void icp_set_mfrr(XICSState *xics, int server, uint8_t mfrr)
{
    ICPState *ss = xics->ss + server;

    ss->mfrr = mfrr;
    if (mfrr < CPPR(ss)) {
        icp_check_ipi(xics, server);
    }
}

uint32_t icp_accept(ICPState *ss)
{
    uint32_t xirr = ss->xirr;

    qemu_irq_lower(ss->output);
    ss->xirr = ss->pending_priority << 24;
    ss->pending_priority = 0xff;

    trace_xics_icp_accept(xirr, ss->xirr);

    return xirr;
}

uint32_t icp_ipoll(ICPState *ss, uint32_t *mfrr)
{
    if (mfrr) {
        *mfrr = ss->mfrr;
    }
    return ss->xirr;
}

void icp_eoi(XICSState *xics, int server, uint32_t xirr)
{
    ICPState *ss = xics->ss + server;

    /* Send EOI -> ICS */
    ss->xirr = (ss->xirr & ~CPPR_MASK) | (xirr & CPPR_MASK);
    trace_xics_icp_eoi(server, xirr, ss->xirr);
    ics_eoi(xics->ics, xirr & XISR_MASK);
    if (!XISR(ss)) {
        icp_resend(xics, server);
    }
}

static void icp_irq(XICSState *xics, int server, int nr, uint8_t priority)
{
    ICPState *ss = xics->ss + server;

    trace_xics_icp_irq(server, nr, priority);

    if ((priority >= CPPR(ss))
        || (XISR(ss) && (ss->pending_priority <= priority))) {
        ics_reject(xics->ics, nr);
    } else {
        if (XISR(ss)) {
            ics_reject(xics->ics, XISR(ss));
        }
        ss->xirr = (ss->xirr & ~XISR_MASK) | (nr & XISR_MASK);
        ss->pending_priority = priority;
        trace_xics_icp_raise(ss->xirr, ss->pending_priority);
        qemu_irq_raise(ss->output);
    }
}

static void icp_dispatch_pre_save(void *opaque)
{
    ICPState *ss = opaque;
    ICPStateClass *info = ICP_GET_CLASS(ss);

    if (info->pre_save) {
        info->pre_save(ss);
    }
}

static int icp_dispatch_post_load(void *opaque, int version_id)
{
    ICPState *ss = opaque;
    ICPStateClass *info = ICP_GET_CLASS(ss);

    if (info->post_load) {
        return info->post_load(ss, version_id);
    }

    return 0;
}

static const VMStateDescription vmstate_icp_server = {
    .name = "icp/server",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = icp_dispatch_pre_save,
    .post_load = icp_dispatch_post_load,
    .fields = (VMStateField[]) {
        /* Sanity check */
        VMSTATE_UINT32(xirr, ICPState),
        VMSTATE_UINT8(pending_priority, ICPState),
        VMSTATE_UINT8(mfrr, ICPState),
        VMSTATE_END_OF_LIST()
    },
};

static void icp_reset(DeviceState *dev)
{
    ICPState *icp = ICP(dev);

    icp->xirr = 0;
    icp->pending_priority = 0xff;
    icp->mfrr = 0xff;

    /* Make all outputs are deasserted */
    qemu_set_irq(icp->output, 0);
}

static void icp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = icp_reset;
    dc->vmsd = &vmstate_icp_server;
}

static const TypeInfo icp_info = {
    .name = TYPE_ICP,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ICPState),
    .class_init = icp_class_init,
    .class_size = sizeof(ICPStateClass),
};

/*
 * ICS: Source layer
 */
static void resend_msi(ICSState *ics, int srcno)
{
    ICSIRQState *irq = ics->irqs + srcno;

    /* FIXME: filter by server#? */
    if (irq->status & XICS_STATUS_REJECTED) {
        irq->status &= ~XICS_STATUS_REJECTED;
        if (irq->priority != 0xff) {
            icp_irq(ics->xics, irq->server, srcno + ics->offset,
                    irq->priority);
        }
    }
}

static void resend_lsi(ICSState *ics, int srcno)
{
    ICSIRQState *irq = ics->irqs + srcno;

    if ((irq->priority != 0xff)
        && (irq->status & XICS_STATUS_ASSERTED)
        && !(irq->status & XICS_STATUS_SENT)) {
        irq->status |= XICS_STATUS_SENT;
        icp_irq(ics->xics, irq->server, srcno + ics->offset, irq->priority);
    }
}

static void set_irq_msi(ICSState *ics, int srcno, int val)
{
    ICSIRQState *irq = ics->irqs + srcno;

    trace_xics_set_irq_msi(srcno, srcno + ics->offset);

    if (val) {
        if (irq->priority == 0xff) {
            irq->status |= XICS_STATUS_MASKED_PENDING;
            trace_xics_masked_pending();
        } else  {
            icp_irq(ics->xics, irq->server, srcno + ics->offset, irq->priority);
        }
    }
}

static void set_irq_lsi(ICSState *ics, int srcno, int val)
{
    ICSIRQState *irq = ics->irqs + srcno;

    trace_xics_set_irq_lsi(srcno, srcno + ics->offset);
    if (val) {
        irq->status |= XICS_STATUS_ASSERTED;
    } else {
        irq->status &= ~XICS_STATUS_ASSERTED;
    }
    resend_lsi(ics, srcno);
}

static void ics_set_irq(void *opaque, int srcno, int val)
{
    ICSState *ics = (ICSState *)opaque;

    if (ics->irqs[srcno].flags & XICS_FLAGS_IRQ_LSI) {
        set_irq_lsi(ics, srcno, val);
    } else {
        set_irq_msi(ics, srcno, val);
    }
}

static void write_xive_msi(ICSState *ics, int srcno)
{
    ICSIRQState *irq = ics->irqs + srcno;

    if (!(irq->status & XICS_STATUS_MASKED_PENDING)
        || (irq->priority == 0xff)) {
        return;
    }

    irq->status &= ~XICS_STATUS_MASKED_PENDING;
    icp_irq(ics->xics, irq->server, srcno + ics->offset, irq->priority);
}

static void write_xive_lsi(ICSState *ics, int srcno)
{
    resend_lsi(ics, srcno);
}

void ics_write_xive(ICSState *ics, int nr, int server,
                    uint8_t priority, uint8_t saved_priority)
{
    int srcno = nr - ics->offset;
    ICSIRQState *irq = ics->irqs + srcno;

    irq->server = server;
    irq->priority = priority;
    irq->saved_priority = saved_priority;

    trace_xics_ics_write_xive(nr, srcno, server, priority);

    if (ics->irqs[srcno].flags & XICS_FLAGS_IRQ_LSI) {
        write_xive_lsi(ics, srcno);
    } else {
        write_xive_msi(ics, srcno);
    }
}

static void ics_reject(ICSState *ics, int nr)
{
    ICSIRQState *irq = ics->irqs + nr - ics->offset;

    trace_xics_ics_reject(nr, nr - ics->offset);
    irq->status |= XICS_STATUS_REJECTED; /* Irrelevant but harmless for LSI */
    irq->status &= ~XICS_STATUS_SENT; /* Irrelevant but harmless for MSI */
}

static void ics_resend(ICSState *ics)
{
    int i;

    for (i = 0; i < ics->nr_irqs; i++) {
        /* FIXME: filter by server#? */
        if (ics->irqs[i].flags & XICS_FLAGS_IRQ_LSI) {
            resend_lsi(ics, i);
        } else {
            resend_msi(ics, i);
        }
    }
}

static void ics_eoi(ICSState *ics, int nr)
{
    int srcno = nr - ics->offset;
    ICSIRQState *irq = ics->irqs + srcno;

    trace_xics_ics_eoi(nr);

    if (ics->irqs[srcno].flags & XICS_FLAGS_IRQ_LSI) {
        irq->status &= ~XICS_STATUS_SENT;
    }
}

static void ics_reset(DeviceState *dev)
{
    ICSState *ics = ICS(dev);
    int i;
    uint8_t flags[ics->nr_irqs];

    for (i = 0; i < ics->nr_irqs; i++) {
        flags[i] = ics->irqs[i].flags;
    }

    memset(ics->irqs, 0, sizeof(ICSIRQState) * ics->nr_irqs);

    for (i = 0; i < ics->nr_irqs; i++) {
        ics->irqs[i].priority = 0xff;
        ics->irqs[i].saved_priority = 0xff;
        ics->irqs[i].flags = flags[i];
    }
}

static int ics_post_load(ICSState *ics, int version_id)
{
    int i;

    for (i = 0; i < ics->xics->nr_servers; i++) {
        icp_resend(ics->xics, i);
    }

    return 0;
}

static void ics_dispatch_pre_save(void *opaque)
{
    ICSState *ics = opaque;
    ICSStateClass *info = ICS_GET_CLASS(ics);

    if (info->pre_save) {
        info->pre_save(ics);
    }
}

static int ics_dispatch_post_load(void *opaque, int version_id)
{
    ICSState *ics = opaque;
    ICSStateClass *info = ICS_GET_CLASS(ics);

    if (info->post_load) {
        return info->post_load(ics, version_id);
    }

    return 0;
}

static const VMStateDescription vmstate_ics_irq = {
    .name = "ics/irq",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(server, ICSIRQState),
        VMSTATE_UINT8(priority, ICSIRQState),
        VMSTATE_UINT8(saved_priority, ICSIRQState),
        VMSTATE_UINT8(status, ICSIRQState),
        VMSTATE_UINT8(flags, ICSIRQState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_ics = {
    .name = "ics",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = ics_dispatch_pre_save,
    .post_load = ics_dispatch_post_load,
    .fields = (VMStateField[]) {
        /* Sanity check */
        VMSTATE_UINT32_EQUAL(nr_irqs, ICSState),

        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(irqs, ICSState, nr_irqs,
                                             vmstate_ics_irq, ICSIRQState),
        VMSTATE_END_OF_LIST()
    },
};

static void ics_initfn(Object *obj)
{
    ICSState *ics = ICS(obj);

    ics->offset = XICS_IRQ_BASE;
}

static void ics_realize(DeviceState *dev, Error **errp)
{
    ICSState *ics = ICS(dev);

    if (!ics->nr_irqs) {
        error_setg(errp, "Number of interrupts needs to be greater 0");
        return;
    }
    ics->irqs = g_malloc0(ics->nr_irqs * sizeof(ICSIRQState));
    ics->qirqs = qemu_allocate_irqs(ics_set_irq, ics, ics->nr_irqs);
}

static void ics_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ICSStateClass *isc = ICS_CLASS(klass);

    dc->realize = ics_realize;
    dc->vmsd = &vmstate_ics;
    dc->reset = ics_reset;
    isc->post_load = ics_post_load;
}

static const TypeInfo ics_info = {
    .name = TYPE_ICS,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ICSState),
    .class_init = ics_class_init,
    .class_size = sizeof(ICSStateClass),
    .instance_init = ics_initfn,
};

/*
 * Exported functions
 */
int xics_find_source(XICSState *xics, int irq)
{
    int sources = 1;
    int src;

    /* FIXME: implement multiple sources */
    for (src = 0; src < sources; ++src) {
        ICSState *ics = &xics->ics[src];
        if (ics_valid_irq(ics, irq)) {
            return src;
        }
    }

    return -1;
}

qemu_irq xics_get_qirq(XICSState *xics, int irq)
{
    int src = xics_find_source(xics, irq);

    if (src >= 0) {
        ICSState *ics = &xics->ics[src];
        return ics->qirqs[irq - ics->offset];
    }

    return NULL;
}

void ics_set_irq_type(ICSState *ics, int srcno, bool lsi)
{
    assert(!(ics->irqs[srcno].flags & XICS_FLAGS_IRQ_MASK));

    ics->irqs[srcno].flags |=
        lsi ? XICS_FLAGS_IRQ_LSI : XICS_FLAGS_IRQ_MSI;
}

static void xics_register_types(void)
{
    type_register_static(&xics_common_info);
    type_register_static(&ics_info);
    type_register_static(&icp_info);
}

type_init(xics_register_types)
