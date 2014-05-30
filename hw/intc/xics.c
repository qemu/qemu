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

#include "hw/hw.h"
#include "trace.h"
#include "qemu/timer.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/xics.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"

static int get_cpu_index_by_dt_id(int cpu_dt_id)
{
    PowerPCCPU *cpu = ppc_get_vcpu_by_dt_id(cpu_dt_id);

    if (cpu) {
        return cpu->parent_obj.cpu_index;
    }

    return -1;
}

void xics_cpu_setup(XICSState *icp, PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    ICPState *ss = &icp->ss[cs->cpu_index];
    XICSStateClass *info = XICS_COMMON_GET_CLASS(icp);

    assert(cs->cpu_index < icp->nr_servers);

    if (info->cpu_setup) {
        info->cpu_setup(icp, cpu);
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
    XICSState *icp = XICS_COMMON(d);
    int i;

    for (i = 0; i < icp->nr_servers; i++) {
        device_reset(DEVICE(&icp->ss[i]));
    }

    device_reset(DEVICE(icp->ics));
}

static void xics_prop_get_nr_irqs(Object *obj, Visitor *v,
                                  void *opaque, const char *name, Error **errp)
{
    XICSState *icp = XICS_COMMON(obj);
    int64_t value = icp->nr_irqs;

    visit_type_int(v, &value, name, errp);
}

static void xics_prop_set_nr_irqs(Object *obj, Visitor *v,
                                  void *opaque, const char *name, Error **errp)
{
    XICSState *icp = XICS_COMMON(obj);
    XICSStateClass *info = XICS_COMMON_GET_CLASS(icp);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (icp->nr_irqs) {
        error_setg(errp, "Number of interrupts is already set to %u",
                   icp->nr_irqs);
        return;
    }

    assert(info->set_nr_irqs);
    assert(icp->ics);
    info->set_nr_irqs(icp, value, errp);
}

static void xics_prop_get_nr_servers(Object *obj, Visitor *v,
                                     void *opaque, const char *name,
                                     Error **errp)
{
    XICSState *icp = XICS_COMMON(obj);
    int64_t value = icp->nr_servers;

    visit_type_int(v, &value, name, errp);
}

static void xics_prop_set_nr_servers(Object *obj, Visitor *v,
                                     void *opaque, const char *name,
                                     Error **errp)
{
    XICSState *icp = XICS_COMMON(obj);
    XICSStateClass *info = XICS_COMMON_GET_CLASS(icp);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (icp->nr_servers) {
        error_setg(errp, "Number of servers is already set to %u",
                   icp->nr_servers);
        return;
    }

    assert(info->set_nr_servers);
    info->set_nr_servers(icp, value, errp);
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

static void icp_check_ipi(XICSState *icp, int server)
{
    ICPState *ss = icp->ss + server;

    if (XISR(ss) && (ss->pending_priority <= ss->mfrr)) {
        return;
    }

    trace_xics_icp_check_ipi(server, ss->mfrr);

    if (XISR(ss)) {
        ics_reject(icp->ics, XISR(ss));
    }

    ss->xirr = (ss->xirr & ~XISR_MASK) | XICS_IPI;
    ss->pending_priority = ss->mfrr;
    qemu_irq_raise(ss->output);
}

static void icp_resend(XICSState *icp, int server)
{
    ICPState *ss = icp->ss + server;

    if (ss->mfrr < CPPR(ss)) {
        icp_check_ipi(icp, server);
    }
    ics_resend(icp->ics);
}

static void icp_set_cppr(XICSState *icp, int server, uint8_t cppr)
{
    ICPState *ss = icp->ss + server;
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
            ics_reject(icp->ics, old_xisr);
        }
    } else {
        if (!XISR(ss)) {
            icp_resend(icp, server);
        }
    }
}

static void icp_set_mfrr(XICSState *icp, int server, uint8_t mfrr)
{
    ICPState *ss = icp->ss + server;

    ss->mfrr = mfrr;
    if (mfrr < CPPR(ss)) {
        icp_check_ipi(icp, server);
    }
}

static uint32_t icp_accept(ICPState *ss)
{
    uint32_t xirr = ss->xirr;

    qemu_irq_lower(ss->output);
    ss->xirr = ss->pending_priority << 24;
    ss->pending_priority = 0xff;

    trace_xics_icp_accept(xirr, ss->xirr);

    return xirr;
}

static void icp_eoi(XICSState *icp, int server, uint32_t xirr)
{
    ICPState *ss = icp->ss + server;

    /* Send EOI -> ICS */
    ss->xirr = (ss->xirr & ~CPPR_MASK) | (xirr & CPPR_MASK);
    trace_xics_icp_eoi(server, xirr, ss->xirr);
    ics_eoi(icp->ics, xirr & XISR_MASK);
    if (!XISR(ss)) {
        icp_resend(icp, server);
    }
}

static void icp_irq(XICSState *icp, int server, int nr, uint8_t priority)
{
    ICPState *ss = icp->ss + server;

    trace_xics_icp_irq(server, nr, priority);

    if ((priority >= CPPR(ss))
        || (XISR(ss) && (ss->pending_priority <= priority))) {
        ics_reject(icp->ics, nr);
    } else {
        if (XISR(ss)) {
            ics_reject(icp->ics, XISR(ss));
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
static int ics_valid_irq(ICSState *ics, uint32_t nr)
{
    return (nr >= ics->offset)
        && (nr < (ics->offset + ics->nr_irqs));
}

static void resend_msi(ICSState *ics, int srcno)
{
    ICSIRQState *irq = ics->irqs + srcno;

    /* FIXME: filter by server#? */
    if (irq->status & XICS_STATUS_REJECTED) {
        irq->status &= ~XICS_STATUS_REJECTED;
        if (irq->priority != 0xff) {
            icp_irq(ics->icp, irq->server, srcno + ics->offset,
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
        icp_irq(ics->icp, irq->server, srcno + ics->offset, irq->priority);
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
            icp_irq(ics->icp, irq->server, srcno + ics->offset, irq->priority);
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
    icp_irq(ics->icp, irq->server, srcno + ics->offset, irq->priority);
}

static void write_xive_lsi(ICSState *ics, int srcno)
{
    resend_lsi(ics, srcno);
}

static void ics_write_xive(ICSState *ics, int nr, int server,
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

    memset(ics->irqs, 0, sizeof(ICSIRQState) * ics->nr_irqs);
    for (i = 0; i < ics->nr_irqs; i++) {
        ics->irqs[i].priority = 0xff;
        ics->irqs[i].saved_priority = 0xff;
    }
}

static int ics_post_load(ICSState *ics, int version_id)
{
    int i;

    for (i = 0; i < ics->icp->nr_servers; i++) {
        icp_resend(ics->icp, i);
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

qemu_irq xics_get_qirq(XICSState *icp, int irq)
{
    if (!ics_valid_irq(icp->ics, irq)) {
        return NULL;
    }

    return icp->ics->qirqs[irq - icp->ics->offset];
}

static void ics_set_irq_type(ICSState *ics, int srcno, bool lsi)
{
    assert(!(ics->irqs[srcno].flags & XICS_FLAGS_IRQ_MASK));

    ics->irqs[srcno].flags |=
        lsi ? XICS_FLAGS_IRQ_LSI : XICS_FLAGS_IRQ_MSI;
}

void xics_set_irq_type(XICSState *icp, int irq, bool lsi)
{
    ICSState *ics = icp->ics;

    assert(ics_valid_irq(ics, irq));

    ics_set_irq_type(ics, irq - ics->offset, lsi);
}

/*
 * Guest interfaces
 */

static target_ulong h_cppr(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                           target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);
    target_ulong cppr = args[0];

    icp_set_cppr(spapr->icp, cs->cpu_index, cppr);
    return H_SUCCESS;
}

static target_ulong h_ipi(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                          target_ulong opcode, target_ulong *args)
{
    target_ulong server = get_cpu_index_by_dt_id(args[0]);
    target_ulong mfrr = args[1];

    if (server >= spapr->icp->nr_servers) {
        return H_PARAMETER;
    }

    icp_set_mfrr(spapr->icp, server, mfrr);
    return H_SUCCESS;
}

static target_ulong h_xirr(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                           target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);
    uint32_t xirr = icp_accept(spapr->icp->ss + cs->cpu_index);

    args[0] = xirr;
    return H_SUCCESS;
}

static target_ulong h_xirr_x(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                             target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);
    ICPState *ss = &spapr->icp->ss[cs->cpu_index];
    uint32_t xirr = icp_accept(ss);

    args[0] = xirr;
    args[1] = cpu_get_real_ticks();
    return H_SUCCESS;
}

static target_ulong h_eoi(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                          target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);
    target_ulong xirr = args[0];

    icp_eoi(spapr->icp, cs->cpu_index, xirr);
    return H_SUCCESS;
}

static target_ulong h_ipoll(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                            target_ulong opcode, target_ulong *args)
{
    CPUState *cs = CPU(cpu);
    ICPState *ss = &spapr->icp->ss[cs->cpu_index];

    args[0] = ss->xirr;
    args[1] = ss->mfrr;

    return H_SUCCESS;
}

static void rtas_set_xive(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                          uint32_t token,
                          uint32_t nargs, target_ulong args,
                          uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->icp->ics;
    uint32_t nr, server, priority;

    if ((nargs != 3) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);
    server = get_cpu_index_by_dt_id(rtas_ld(args, 1));
    priority = rtas_ld(args, 2);

    if (!ics_valid_irq(ics, nr) || (server >= ics->icp->nr_servers)
        || (priority > 0xff)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    ics_write_xive(ics, nr, server, priority, priority);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_get_xive(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                          uint32_t token,
                          uint32_t nargs, target_ulong args,
                          uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->icp->ics;
    uint32_t nr;

    if ((nargs != 1) || (nret != 3)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, ics->irqs[nr - ics->offset].server);
    rtas_st(rets, 2, ics->irqs[nr - ics->offset].priority);
}

static void rtas_int_off(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                         uint32_t token,
                         uint32_t nargs, target_ulong args,
                         uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->icp->ics;
    uint32_t nr;

    if ((nargs != 1) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    ics_write_xive(ics, nr, ics->irqs[nr - ics->offset].server, 0xff,
                   ics->irqs[nr - ics->offset].priority);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_int_on(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                        uint32_t token,
                        uint32_t nargs, target_ulong args,
                        uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->icp->ics;
    uint32_t nr;

    if ((nargs != 1) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    ics_write_xive(ics, nr, ics->irqs[nr - ics->offset].server,
                   ics->irqs[nr - ics->offset].saved_priority,
                   ics->irqs[nr - ics->offset].saved_priority);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

/*
 * XICS
 */

static void xics_set_nr_irqs(XICSState *icp, uint32_t nr_irqs, Error **errp)
{
    icp->nr_irqs = icp->ics->nr_irqs = nr_irqs;
}

static void xics_set_nr_servers(XICSState *icp, uint32_t nr_servers,
                                Error **errp)
{
    int i;

    icp->nr_servers = nr_servers;

    icp->ss = g_malloc0(icp->nr_servers*sizeof(ICPState));
    for (i = 0; i < icp->nr_servers; i++) {
        char buffer[32];
        object_initialize(&icp->ss[i], sizeof(icp->ss[i]), TYPE_ICP);
        snprintf(buffer, sizeof(buffer), "icp[%d]", i);
        object_property_add_child(OBJECT(icp), buffer, OBJECT(&icp->ss[i]),
                                  errp);
    }
}

static void xics_realize(DeviceState *dev, Error **errp)
{
    XICSState *icp = XICS(dev);
    Error *error = NULL;
    int i;

    if (!icp->nr_servers) {
        error_setg(errp, "Number of servers needs to be greater 0");
        return;
    }

    /* Registration of global state belongs into realize */
    spapr_rtas_register(RTAS_IBM_SET_XIVE, "ibm,set-xive", rtas_set_xive);
    spapr_rtas_register(RTAS_IBM_GET_XIVE, "ibm,get-xive", rtas_get_xive);
    spapr_rtas_register(RTAS_IBM_INT_OFF, "ibm,int-off", rtas_int_off);
    spapr_rtas_register(RTAS_IBM_INT_ON, "ibm,int-on", rtas_int_on);

    spapr_register_hypercall(H_CPPR, h_cppr);
    spapr_register_hypercall(H_IPI, h_ipi);
    spapr_register_hypercall(H_XIRR, h_xirr);
    spapr_register_hypercall(H_XIRR_X, h_xirr_x);
    spapr_register_hypercall(H_EOI, h_eoi);
    spapr_register_hypercall(H_IPOLL, h_ipoll);

    object_property_set_bool(OBJECT(icp->ics), true, "realized", &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    for (i = 0; i < icp->nr_servers; i++) {
        object_property_set_bool(OBJECT(&icp->ss[i]), true, "realized", &error);
        if (error) {
            error_propagate(errp, error);
            return;
        }
    }
}

static void xics_initfn(Object *obj)
{
    XICSState *xics = XICS(obj);

    xics->ics = ICS(object_new(TYPE_ICS));
    object_property_add_child(obj, "ics", OBJECT(xics->ics), NULL);
    xics->ics->icp = xics;
}

static void xics_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    XICSStateClass *xsc = XICS_CLASS(oc);

    dc->realize = xics_realize;
    xsc->set_nr_irqs = xics_set_nr_irqs;
    xsc->set_nr_servers = xics_set_nr_servers;
}

static const TypeInfo xics_info = {
    .name          = TYPE_XICS,
    .parent        = TYPE_XICS_COMMON,
    .instance_size = sizeof(XICSState),
    .class_size = sizeof(XICSStateClass),
    .class_init    = xics_class_init,
    .instance_init = xics_initfn,
};

static void xics_register_types(void)
{
    type_register_static(&xics_common_info);
    type_register_static(&xics_info);
    type_register_static(&ics_info);
    type_register_static(&icp_info);
}

type_init(xics_register_types)
