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
#include "monitor/monitor.h"
#include "hw/intc/intc.h"

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

static void xics_common_pic_print_info(InterruptStatsProvider *obj,
                                       Monitor *mon)
{
    XICSState *xics = XICS_COMMON(obj);
    ICSState *ics;
    uint32_t i;

    for (i = 0; i < xics->nr_servers; i++) {
        ICPState *icp = &xics->ss[i];

        if (!icp->output) {
            continue;
        }
        monitor_printf(mon, "CPU %d XIRR=%08x (%p) PP=%02x MFRR=%02x\n",
                       i, icp->xirr, icp->xirr_owner,
                       icp->pending_priority, icp->mfrr);
    }

    QLIST_FOREACH(ics, &xics->ics, list) {
        monitor_printf(mon, "ICS %4x..%4x %p\n",
                       ics->offset, ics->offset + ics->nr_irqs - 1, ics);

        if (!ics->irqs) {
            continue;
        }

        for (i = 0; i < ics->nr_irqs; i++) {
            ICSIRQState *irq = ics->irqs + i;

            if (!(irq->flags & XICS_FLAGS_IRQ_MASK)) {
                continue;
            }
            monitor_printf(mon, "  %4x %s %02x %02x\n",
                           ics->offset + i,
                           (irq->flags & XICS_FLAGS_IRQ_LSI) ?
                           "LSI" : "MSI",
                           irq->priority, irq->status);
        }
    }
}

/*
 * XICS Common class - parent for emulated XICS and KVM-XICS
 */
static void xics_common_reset(DeviceState *d)
{
    XICSState *xics = XICS_COMMON(d);
    ICSState *ics;
    int i;

    for (i = 0; i < xics->nr_servers; i++) {
        device_reset(DEVICE(&xics->ss[i]));
    }

    QLIST_FOREACH(ics, &xics->ics, list) {
        device_reset(DEVICE(ics));
    }
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
    info->set_nr_irqs(xics, value, errp);
}

void xics_set_nr_servers(XICSState *xics, uint32_t nr_servers,
                         const char *typename, Error **errp)
{
    int i;

    xics->nr_servers = nr_servers;

    xics->ss = g_malloc0(xics->nr_servers * sizeof(ICPState));
    for (i = 0; i < xics->nr_servers; i++) {
        char name[32];
        ICPState *icp = &xics->ss[i];

        object_initialize(icp, sizeof(*icp), typename);
        snprintf(name, sizeof(name), "icp[%d]", i);
        object_property_add_child(OBJECT(xics), name, OBJECT(icp), errp);
        icp->xics = xics;
    }
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
    XICSStateClass *xsc = XICS_COMMON_GET_CLASS(xics);
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

    assert(xsc->set_nr_servers);
    xsc->set_nr_servers(xics, value, errp);
}

static void xics_common_initfn(Object *obj)
{
    XICSState *xics = XICS_COMMON(obj);

    QLIST_INIT(&xics->ics);
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
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(oc);

    dc->reset = xics_common_reset;
    ic->print_info = xics_common_pic_print_info;
}

static const TypeInfo xics_common_info = {
    .name          = TYPE_XICS_COMMON,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XICSState),
    .class_size    = sizeof(XICSStateClass),
    .instance_init = xics_common_initfn,
    .class_init    = xics_common_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_INTERRUPT_STATS_PROVIDER },
        { }
    },
};

/*
 * ICP: Presentation layer
 */

#define XISR_MASK  0x00ffffff
#define CPPR_MASK  0xff000000

#define XISR(ss)   (((ss)->xirr) & XISR_MASK)
#define CPPR(ss)   (((ss)->xirr) >> 24)

static void ics_reject(ICSState *ics, uint32_t nr)
{
    ICSStateClass *k = ICS_BASE_GET_CLASS(ics);

    if (k->reject) {
        k->reject(ics, nr);
    }
}

static void ics_resend(ICSState *ics)
{
    ICSStateClass *k = ICS_BASE_GET_CLASS(ics);

    if (k->resend) {
        k->resend(ics);
    }
}

static void ics_eoi(ICSState *ics, int nr)
{
    ICSStateClass *k = ICS_BASE_GET_CLASS(ics);

    if (k->eoi) {
        k->eoi(ics, nr);
    }
}

static void icp_check_ipi(ICPState *ss)
{
    if (XISR(ss) && (ss->pending_priority <= ss->mfrr)) {
        return;
    }

    trace_xics_icp_check_ipi(ss->cs->cpu_index, ss->mfrr);

    if (XISR(ss) && ss->xirr_owner) {
        ics_reject(ss->xirr_owner, XISR(ss));
    }

    ss->xirr = (ss->xirr & ~XISR_MASK) | XICS_IPI;
    ss->pending_priority = ss->mfrr;
    ss->xirr_owner = NULL;
    qemu_irq_raise(ss->output);
}

static void icp_resend(ICPState *ss)
{
    ICSState *ics;

    if (ss->mfrr < CPPR(ss)) {
        icp_check_ipi(ss);
    }
    QLIST_FOREACH(ics, &ss->xics->ics, list) {
        ics_resend(ics);
    }
}

void icp_set_cppr(ICPState *ss, uint8_t cppr)
{
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
            if (ss->xirr_owner) {
                ics_reject(ss->xirr_owner, old_xisr);
                ss->xirr_owner = NULL;
            }
        }
    } else {
        if (!XISR(ss)) {
            icp_resend(ss);
        }
    }
}

void icp_set_mfrr(ICPState *ss, uint8_t mfrr)
{
    ss->mfrr = mfrr;
    if (mfrr < CPPR(ss)) {
        icp_check_ipi(ss);
    }
}

uint32_t icp_accept(ICPState *ss)
{
    uint32_t xirr = ss->xirr;

    qemu_irq_lower(ss->output);
    ss->xirr = ss->pending_priority << 24;
    ss->pending_priority = 0xff;
    ss->xirr_owner = NULL;

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

void icp_eoi(ICPState *ss, uint32_t xirr)
{
    ICSState *ics;
    uint32_t irq;

    /* Send EOI -> ICS */
    ss->xirr = (ss->xirr & ~CPPR_MASK) | (xirr & CPPR_MASK);
    trace_xics_icp_eoi(ss->cs->cpu_index, xirr, ss->xirr);
    irq = xirr & XISR_MASK;
    QLIST_FOREACH(ics, &ss->xics->ics, list) {
        if (ics_valid_irq(ics, irq)) {
            ics_eoi(ics, irq);
        }
    }
    if (!XISR(ss)) {
        icp_resend(ss);
    }
}

static void icp_irq(ICSState *ics, int server, int nr, uint8_t priority)
{
    XICSState *xics = ics->xics;
    ICPState *ss = xics->ss + server;

    trace_xics_icp_irq(server, nr, priority);

    if ((priority >= CPPR(ss))
        || (XISR(ss) && (ss->pending_priority <= priority))) {
        ics_reject(ics, nr);
    } else {
        if (XISR(ss) && ss->xirr_owner) {
            ics_reject(ss->xirr_owner, XISR(ss));
            ss->xirr_owner = NULL;
        }
        ss->xirr = (ss->xirr & ~XISR_MASK) | (nr & XISR_MASK);
        ss->xirr_owner = ics;
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
static void ics_simple_resend_msi(ICSState *ics, int srcno)
{
    ICSIRQState *irq = ics->irqs + srcno;

    /* FIXME: filter by server#? */
    if (irq->status & XICS_STATUS_REJECTED) {
        irq->status &= ~XICS_STATUS_REJECTED;
        if (irq->priority != 0xff) {
            icp_irq(ics, irq->server, srcno + ics->offset, irq->priority);
        }
    }
}

static void ics_simple_resend_lsi(ICSState *ics, int srcno)
{
    ICSIRQState *irq = ics->irqs + srcno;

    if ((irq->priority != 0xff)
        && (irq->status & XICS_STATUS_ASSERTED)
        && !(irq->status & XICS_STATUS_SENT)) {
        irq->status |= XICS_STATUS_SENT;
        icp_irq(ics, irq->server, srcno + ics->offset, irq->priority);
    }
}

static void ics_simple_set_irq_msi(ICSState *ics, int srcno, int val)
{
    ICSIRQState *irq = ics->irqs + srcno;

    trace_xics_ics_simple_set_irq_msi(srcno, srcno + ics->offset);

    if (val) {
        if (irq->priority == 0xff) {
            irq->status |= XICS_STATUS_MASKED_PENDING;
            trace_xics_masked_pending();
        } else  {
            icp_irq(ics, irq->server, srcno + ics->offset, irq->priority);
        }
    }
}

static void ics_simple_set_irq_lsi(ICSState *ics, int srcno, int val)
{
    ICSIRQState *irq = ics->irqs + srcno;

    trace_xics_ics_simple_set_irq_lsi(srcno, srcno + ics->offset);
    if (val) {
        irq->status |= XICS_STATUS_ASSERTED;
    } else {
        irq->status &= ~XICS_STATUS_ASSERTED;
    }
    ics_simple_resend_lsi(ics, srcno);
}

static void ics_simple_set_irq(void *opaque, int srcno, int val)
{
    ICSState *ics = (ICSState *)opaque;

    if (ics->irqs[srcno].flags & XICS_FLAGS_IRQ_LSI) {
        ics_simple_set_irq_lsi(ics, srcno, val);
    } else {
        ics_simple_set_irq_msi(ics, srcno, val);
    }
}

static void ics_simple_write_xive_msi(ICSState *ics, int srcno)
{
    ICSIRQState *irq = ics->irqs + srcno;

    if (!(irq->status & XICS_STATUS_MASKED_PENDING)
        || (irq->priority == 0xff)) {
        return;
    }

    irq->status &= ~XICS_STATUS_MASKED_PENDING;
    icp_irq(ics, irq->server, srcno + ics->offset, irq->priority);
}

static void ics_simple_write_xive_lsi(ICSState *ics, int srcno)
{
    ics_simple_resend_lsi(ics, srcno);
}

void ics_simple_write_xive(ICSState *ics, int srcno, int server,
                           uint8_t priority, uint8_t saved_priority)
{
    ICSIRQState *irq = ics->irqs + srcno;

    irq->server = server;
    irq->priority = priority;
    irq->saved_priority = saved_priority;

    trace_xics_ics_simple_write_xive(ics->offset + srcno, srcno, server,
                                     priority);

    if (ics->irqs[srcno].flags & XICS_FLAGS_IRQ_LSI) {
        ics_simple_write_xive_lsi(ics, srcno);
    } else {
        ics_simple_write_xive_msi(ics, srcno);
    }
}

static void ics_simple_reject(ICSState *ics, uint32_t nr)
{
    ICSIRQState *irq = ics->irqs + nr - ics->offset;

    trace_xics_ics_simple_reject(nr, nr - ics->offset);
    if (irq->flags & XICS_FLAGS_IRQ_MSI) {
        irq->status |= XICS_STATUS_REJECTED;
    } else if (irq->flags & XICS_FLAGS_IRQ_LSI) {
        irq->status &= ~XICS_STATUS_SENT;
    }
}

static void ics_simple_resend(ICSState *ics)
{
    int i;

    for (i = 0; i < ics->nr_irqs; i++) {
        /* FIXME: filter by server#? */
        if (ics->irqs[i].flags & XICS_FLAGS_IRQ_LSI) {
            ics_simple_resend_lsi(ics, i);
        } else {
            ics_simple_resend_msi(ics, i);
        }
    }
}

static void ics_simple_eoi(ICSState *ics, uint32_t nr)
{
    int srcno = nr - ics->offset;
    ICSIRQState *irq = ics->irqs + srcno;

    trace_xics_ics_simple_eoi(nr);

    if (ics->irqs[srcno].flags & XICS_FLAGS_IRQ_LSI) {
        irq->status &= ~XICS_STATUS_SENT;
    }
}

static void ics_simple_reset(DeviceState *dev)
{
    ICSState *ics = ICS_SIMPLE(dev);
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

static int ics_simple_post_load(ICSState *ics, int version_id)
{
    int i;

    for (i = 0; i < ics->xics->nr_servers; i++) {
        icp_resend(&ics->xics->ss[i]);
    }

    return 0;
}

static void ics_simple_dispatch_pre_save(void *opaque)
{
    ICSState *ics = opaque;
    ICSStateClass *info = ICS_BASE_GET_CLASS(ics);

    if (info->pre_save) {
        info->pre_save(ics);
    }
}

static int ics_simple_dispatch_post_load(void *opaque, int version_id)
{
    ICSState *ics = opaque;
    ICSStateClass *info = ICS_BASE_GET_CLASS(ics);

    if (info->post_load) {
        return info->post_load(ics, version_id);
    }

    return 0;
}

static const VMStateDescription vmstate_ics_simple_irq = {
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

static const VMStateDescription vmstate_ics_simple = {
    .name = "ics",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = ics_simple_dispatch_pre_save,
    .post_load = ics_simple_dispatch_post_load,
    .fields = (VMStateField[]) {
        /* Sanity check */
        VMSTATE_UINT32_EQUAL(nr_irqs, ICSState),

        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(irqs, ICSState, nr_irqs,
                                             vmstate_ics_simple_irq,
                                             ICSIRQState),
        VMSTATE_END_OF_LIST()
    },
};

static void ics_simple_initfn(Object *obj)
{
    ICSState *ics = ICS_SIMPLE(obj);

    ics->offset = XICS_IRQ_BASE;
}

static void ics_simple_realize(DeviceState *dev, Error **errp)
{
    ICSState *ics = ICS_SIMPLE(dev);

    if (!ics->nr_irqs) {
        error_setg(errp, "Number of interrupts needs to be greater 0");
        return;
    }
    ics->irqs = g_malloc0(ics->nr_irqs * sizeof(ICSIRQState));
    ics->qirqs = qemu_allocate_irqs(ics_simple_set_irq, ics, ics->nr_irqs);
}

static void ics_simple_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ICSStateClass *isc = ICS_BASE_CLASS(klass);

    dc->realize = ics_simple_realize;
    dc->vmsd = &vmstate_ics_simple;
    dc->reset = ics_simple_reset;
    isc->post_load = ics_simple_post_load;
    isc->reject = ics_simple_reject;
    isc->resend = ics_simple_resend;
    isc->eoi = ics_simple_eoi;
}

static const TypeInfo ics_simple_info = {
    .name = TYPE_ICS_SIMPLE,
    .parent = TYPE_ICS_BASE,
    .instance_size = sizeof(ICSState),
    .class_init = ics_simple_class_init,
    .class_size = sizeof(ICSStateClass),
    .instance_init = ics_simple_initfn,
};

static const TypeInfo ics_base_info = {
    .name = TYPE_ICS_BASE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .instance_size = sizeof(ICSState),
    .class_size = sizeof(ICSStateClass),
};

/*
 * Exported functions
 */
ICSState *xics_find_source(XICSState *xics, int irq)
{
    ICSState *ics;

    QLIST_FOREACH(ics, &xics->ics, list) {
        if (ics_valid_irq(ics, irq)) {
            return ics;
        }
    }
    return NULL;
}

qemu_irq xics_get_qirq(XICSState *xics, int irq)
{
    ICSState *ics = xics_find_source(xics, irq);

    if (ics) {
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
    type_register_static(&ics_simple_info);
    type_register_static(&ics_base_info);
    type_register_static(&icp_info);
}

type_init(xics_register_types)
