/*
 * QEMU PowerPC sPAPR IRQ interface
 *
 * Copyright (c) 2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/xics_spapr.h"
#include "hw/qdev-properties.h"
#include "cpu-models.h"
#include "sysemu/kvm.h"

#include "trace.h"

static const TypeInfo spapr_intc_info = {
    .name = TYPE_SPAPR_INTC,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(SpaprInterruptControllerClass),
};

void spapr_irq_msi_init(SpaprMachineState *spapr, uint32_t nr_msis)
{
    spapr->irq_map_nr = nr_msis;
    spapr->irq_map = bitmap_new(spapr->irq_map_nr);
}

int spapr_irq_msi_alloc(SpaprMachineState *spapr, uint32_t num, bool align,
                        Error **errp)
{
    int irq;

    /*
     * The 'align_mask' parameter of bitmap_find_next_zero_area()
     * should be one less than a power of 2; 0 means no
     * alignment. Adapt the 'align' value of the former allocator
     * to fit the requirements of bitmap_find_next_zero_area()
     */
    align -= 1;

    irq = bitmap_find_next_zero_area(spapr->irq_map, spapr->irq_map_nr, 0, num,
                                     align);
    if (irq == spapr->irq_map_nr) {
        error_setg(errp, "can't find a free %d-IRQ block", num);
        return -1;
    }

    bitmap_set(spapr->irq_map, irq, num);

    return irq + SPAPR_IRQ_MSI;
}

void spapr_irq_msi_free(SpaprMachineState *spapr, int irq, uint32_t num)
{
    bitmap_clear(spapr->irq_map, irq - SPAPR_IRQ_MSI, num);
}

static void spapr_irq_init_kvm(SpaprMachineState *spapr,
                                  SpaprIrq *irq, Error **errp)
{
    MachineState *machine = MACHINE(spapr);
    Error *local_err = NULL;

    if (kvm_enabled() && machine_kernel_irqchip_allowed(machine)) {
        irq->init_kvm(spapr, &local_err);
        if (local_err && machine_kernel_irqchip_required(machine)) {
            error_prepend(&local_err,
                          "kernel_irqchip requested but unavailable: ");
            error_propagate(errp, local_err);
            return;
        }

        if (!local_err) {
            return;
        }

        /*
         * We failed to initialize the KVM device, fallback to
         * emulated mode
         */
        error_prepend(&local_err, "kernel_irqchip allowed but unavailable: ");
        error_append_hint(&local_err, "Falling back to kernel-irqchip=off\n");
        warn_report_err(local_err);
    }
}

/*
 * XICS IRQ backend.
 */

static int spapr_irq_claim_xics(SpaprMachineState *spapr, int irq, bool lsi,
                                Error **errp)
{
    ICSState *ics = spapr->ics;

    assert(ics);
    assert(ics_valid_irq(ics, irq));

    if (!ics_irq_free(ics, irq - ics->offset)) {
        error_setg(errp, "IRQ %d is not free", irq);
        return -1;
    }

    ics_set_irq_type(ics, irq - ics->offset, lsi);
    return 0;
}

static void spapr_irq_free_xics(SpaprMachineState *spapr, int irq)
{
    ICSState *ics = spapr->ics;
    uint32_t srcno = irq - ics->offset;

    assert(ics_valid_irq(ics, irq));

    memset(&ics->irqs[srcno], 0, sizeof(ICSIRQState));
}

static void spapr_irq_print_info_xics(SpaprMachineState *spapr, Monitor *mon)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        icp_pic_print_info(spapr_cpu_state(cpu)->icp, mon);
    }

    ics_pic_print_info(spapr->ics, mon);
}

static void spapr_irq_cpu_intc_create_xics(SpaprMachineState *spapr,
                                           PowerPCCPU *cpu, Error **errp)
{
    Error *local_err = NULL;
    Object *obj;
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    obj = icp_create(OBJECT(cpu), TYPE_ICP, XICS_FABRIC(spapr),
                     &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    spapr_cpu->icp = ICP(obj);
}

static int spapr_irq_post_load_xics(SpaprMachineState *spapr, int version_id)
{
    if (!kvm_irqchip_in_kernel()) {
        CPUState *cs;
        CPU_FOREACH(cs) {
            PowerPCCPU *cpu = POWERPC_CPU(cs);
            icp_resend(spapr_cpu_state(cpu)->icp);
        }
    }
    return 0;
}

static void spapr_irq_set_irq_xics(void *opaque, int irq, int val)
{
    SpaprMachineState *spapr = opaque;
    uint32_t srcno = irq - spapr->ics->offset;

    ics_set_irq(spapr->ics, srcno, val);
}

static void spapr_irq_reset_xics(SpaprMachineState *spapr, Error **errp)
{
    Error *local_err = NULL;

    spapr_irq_init_kvm(spapr, &spapr_irq_xics, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void spapr_irq_init_kvm_xics(SpaprMachineState *spapr, Error **errp)
{
    if (kvm_enabled()) {
        xics_kvm_connect(spapr, errp);
    }
}

SpaprIrq spapr_irq_xics = {
    .nr_xirqs    = SPAPR_NR_XIRQS,
    .nr_msis     = SPAPR_NR_MSIS,
    .xics        = true,
    .xive        = false,

    .claim       = spapr_irq_claim_xics,
    .free        = spapr_irq_free_xics,
    .print_info  = spapr_irq_print_info_xics,
    .dt_populate = spapr_dt_xics,
    .cpu_intc_create = spapr_irq_cpu_intc_create_xics,
    .post_load   = spapr_irq_post_load_xics,
    .reset       = spapr_irq_reset_xics,
    .set_irq     = spapr_irq_set_irq_xics,
    .init_kvm    = spapr_irq_init_kvm_xics,
};

/*
 * XIVE IRQ backend.
 */

static int spapr_irq_claim_xive(SpaprMachineState *spapr, int irq, bool lsi,
                                Error **errp)
{
    return spapr_xive_irq_claim(spapr->xive, irq, lsi, errp);
}

static void spapr_irq_free_xive(SpaprMachineState *spapr, int irq)
{
    spapr_xive_irq_free(spapr->xive, irq);
}

static void spapr_irq_print_info_xive(SpaprMachineState *spapr,
                                      Monitor *mon)
{
    CPUState *cs;

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        xive_tctx_pic_print_info(spapr_cpu_state(cpu)->tctx, mon);
    }

    spapr_xive_pic_print_info(spapr->xive, mon);
}

static void spapr_irq_cpu_intc_create_xive(SpaprMachineState *spapr,
                                           PowerPCCPU *cpu, Error **errp)
{
    Error *local_err = NULL;
    Object *obj;
    SpaprCpuState *spapr_cpu = spapr_cpu_state(cpu);

    obj = xive_tctx_create(OBJECT(cpu), XIVE_ROUTER(spapr->xive), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    spapr_cpu->tctx = XIVE_TCTX(obj);

    /*
     * (TCG) Early setting the OS CAM line for hotplugged CPUs as they
     * don't beneficiate from the reset of the XIVE IRQ backend
     */
    spapr_xive_set_tctx_os_cam(spapr_cpu->tctx);
}

static int spapr_irq_post_load_xive(SpaprMachineState *spapr, int version_id)
{
    return spapr_xive_post_load(spapr->xive, version_id);
}

static void spapr_irq_reset_xive(SpaprMachineState *spapr, Error **errp)
{
    CPUState *cs;
    Error *local_err = NULL;

    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        /* (TCG) Set the OS CAM line of the thread interrupt context. */
        spapr_xive_set_tctx_os_cam(spapr_cpu_state(cpu)->tctx);
    }

    spapr_irq_init_kvm(spapr, &spapr_irq_xive, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Activate the XIVE MMIOs */
    spapr_xive_mmio_set_enabled(spapr->xive, true);
}

static void spapr_irq_set_irq_xive(void *opaque, int irq, int val)
{
    SpaprMachineState *spapr = opaque;

    if (kvm_irqchip_in_kernel()) {
        kvmppc_xive_source_set_irq(&spapr->xive->source, irq, val);
    } else {
        xive_source_set_irq(&spapr->xive->source, irq, val);
    }
}

static void spapr_irq_init_kvm_xive(SpaprMachineState *spapr, Error **errp)
{
    if (kvm_enabled()) {
        kvmppc_xive_connect(spapr->xive, errp);
    }
}

SpaprIrq spapr_irq_xive = {
    .nr_xirqs    = SPAPR_NR_XIRQS,
    .nr_msis     = SPAPR_NR_MSIS,
    .xics        = false,
    .xive        = true,

    .claim       = spapr_irq_claim_xive,
    .free        = spapr_irq_free_xive,
    .print_info  = spapr_irq_print_info_xive,
    .dt_populate = spapr_dt_xive,
    .cpu_intc_create = spapr_irq_cpu_intc_create_xive,
    .post_load   = spapr_irq_post_load_xive,
    .reset       = spapr_irq_reset_xive,
    .set_irq     = spapr_irq_set_irq_xive,
    .init_kvm    = spapr_irq_init_kvm_xive,
};

/*
 * Dual XIVE and XICS IRQ backend.
 *
 * Both interrupt mode, XIVE and XICS, objects are created but the
 * machine starts in legacy interrupt mode (XICS). It can be changed
 * by the CAS negotiation process and, in that case, the new mode is
 * activated after an extra machine reset.
 */

/*
 * Returns the sPAPR IRQ backend negotiated by CAS. XICS is the
 * default.
 */
static SpaprIrq *spapr_irq_current(SpaprMachineState *spapr)
{
    return spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT) ?
        &spapr_irq_xive : &spapr_irq_xics;
}

static int spapr_irq_claim_dual(SpaprMachineState *spapr, int irq, bool lsi,
                                Error **errp)
{
    Error *local_err = NULL;
    int ret;

    ret = spapr_irq_xics.claim(spapr, irq, lsi, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return ret;
    }

    ret = spapr_irq_xive.claim(spapr, irq, lsi, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return ret;
    }

    return ret;
}

static void spapr_irq_free_dual(SpaprMachineState *spapr, int irq)
{
    spapr_irq_xics.free(spapr, irq);
    spapr_irq_xive.free(spapr, irq);
}

static void spapr_irq_print_info_dual(SpaprMachineState *spapr, Monitor *mon)
{
    spapr_irq_current(spapr)->print_info(spapr, mon);
}

static void spapr_irq_dt_populate_dual(SpaprMachineState *spapr,
                                       uint32_t nr_servers, void *fdt,
                                       uint32_t phandle)
{
    spapr_irq_current(spapr)->dt_populate(spapr, nr_servers, fdt, phandle);
}

static void spapr_irq_cpu_intc_create_dual(SpaprMachineState *spapr,
                                           PowerPCCPU *cpu, Error **errp)
{
    Error *local_err = NULL;

    spapr_irq_xive.cpu_intc_create(spapr, cpu, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    spapr_irq_xics.cpu_intc_create(spapr, cpu, errp);
}

static int spapr_irq_post_load_dual(SpaprMachineState *spapr, int version_id)
{
    /*
     * Force a reset of the XIVE backend after migration. The machine
     * defaults to XICS at startup.
     */
    if (spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        if (kvm_irqchip_in_kernel()) {
            xics_kvm_disconnect(spapr, &error_fatal);
        }
        spapr_irq_xive.reset(spapr, &error_fatal);
    }

    return spapr_irq_current(spapr)->post_load(spapr, version_id);
}

static void spapr_irq_reset_dual(SpaprMachineState *spapr, Error **errp)
{
    Error *local_err = NULL;

    /*
     * Deactivate the XIVE MMIOs. The XIVE backend will reenable them
     * if selected.
     */
    spapr_xive_mmio_set_enabled(spapr->xive, false);

    /* Destroy all KVM devices */
    if (kvm_irqchip_in_kernel()) {
        xics_kvm_disconnect(spapr, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_prepend(errp, "KVM XICS disconnect failed: ");
            return;
        }
        kvmppc_xive_disconnect(spapr->xive, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_prepend(errp, "KVM XIVE disconnect failed: ");
            return;
        }
    }

    spapr_irq_current(spapr)->reset(spapr, errp);
}

static void spapr_irq_set_irq_dual(void *opaque, int irq, int val)
{
    SpaprMachineState *spapr = opaque;

    spapr_irq_current(spapr)->set_irq(spapr, irq, val);
}

/*
 * Define values in sync with the XIVE and XICS backend
 */
SpaprIrq spapr_irq_dual = {
    .nr_xirqs    = SPAPR_NR_XIRQS,
    .nr_msis     = SPAPR_NR_MSIS,
    .xics        = true,
    .xive        = true,

    .claim       = spapr_irq_claim_dual,
    .free        = spapr_irq_free_dual,
    .print_info  = spapr_irq_print_info_dual,
    .dt_populate = spapr_irq_dt_populate_dual,
    .cpu_intc_create = spapr_irq_cpu_intc_create_dual,
    .post_load   = spapr_irq_post_load_dual,
    .reset       = spapr_irq_reset_dual,
    .set_irq     = spapr_irq_set_irq_dual,
    .init_kvm    = NULL, /* should not be used */
};


static int spapr_irq_check(SpaprMachineState *spapr, Error **errp)
{
    MachineState *machine = MACHINE(spapr);

    /*
     * Sanity checks on non-P9 machines. On these, XIVE is not
     * advertised, see spapr_dt_ov5_platform_support()
     */
    if (!ppc_type_check_compat(machine->cpu_type, CPU_POWERPC_LOGICAL_3_00,
                               0, spapr->max_compat_pvr)) {
        /*
         * If the 'dual' interrupt mode is selected, force XICS as CAS
         * negotiation is useless.
         */
        if (spapr->irq == &spapr_irq_dual) {
            spapr->irq = &spapr_irq_xics;
            return 0;
        }

        /*
         * Non-P9 machines using only XIVE is a bogus setup. We have two
         * scenarios to take into account because of the compat mode:
         *
         * 1. POWER7/8 machines should fail to init later on when creating
         *    the XIVE interrupt presenters because a POWER9 exception
         *    model is required.

         * 2. POWER9 machines using the POWER8 compat mode won't fail and
         *    will let the OS boot with a partial XIVE setup : DT
         *    properties but no hcalls.
         *
         * To cover both and not confuse the OS, add an early failure in
         * QEMU.
         */
        if (spapr->irq == &spapr_irq_xive) {
            error_setg(errp, "XIVE-only machines require a POWER9 CPU");
            return -1;
        }
    }

    /*
     * On a POWER9 host, some older KVM XICS devices cannot be destroyed and
     * re-created. Detect that early to avoid QEMU to exit later when the
     * guest reboots.
     */
    if (kvm_enabled() &&
        spapr->irq == &spapr_irq_dual &&
        machine_kernel_irqchip_required(machine) &&
        xics_kvm_has_broken_disconnect(spapr)) {
        error_setg(errp, "KVM is too old to support ic-mode=dual,kernel-irqchip=on");
        return -1;
    }

    return 0;
}

/*
 * sPAPR IRQ frontend routines for devices
 */
void spapr_irq_init(SpaprMachineState *spapr, Error **errp)
{
    MachineState *machine = MACHINE(spapr);

    if (machine_kernel_irqchip_split(machine)) {
        error_setg(errp, "kernel_irqchip split mode not supported on pseries");
        return;
    }

    if (!kvm_enabled() && machine_kernel_irqchip_required(machine)) {
        error_setg(errp,
                   "kernel_irqchip requested but only available with KVM");
        return;
    }

    if (spapr_irq_check(spapr, errp) < 0) {
        return;
    }

    /* Initialize the MSI IRQ allocator. */
    if (!SPAPR_MACHINE_GET_CLASS(spapr)->legacy_irq_allocation) {
        spapr_irq_msi_init(spapr, spapr->irq->nr_msis);
    }

    if (spapr->irq->xics) {
        Error *local_err = NULL;
        Object *obj;

        obj = object_new(TYPE_ICS_SPAPR);
        object_property_add_child(OBJECT(spapr), "ics", obj, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        object_property_add_const_link(obj, ICS_PROP_XICS, OBJECT(spapr),
                                       &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        object_property_set_int(obj, spapr->irq->nr_xirqs, "nr-irqs",
                                &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        object_property_set_bool(obj, true, "realized", &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        spapr->ics = ICS_SPAPR(obj);
    }

    if (spapr->irq->xive) {
        uint32_t nr_servers = spapr_max_server_number(spapr);
        DeviceState *dev;
        int i;

        dev = qdev_create(NULL, TYPE_SPAPR_XIVE);
        qdev_prop_set_uint32(dev, "nr-irqs",
                             spapr->irq->nr_xirqs + SPAPR_XIRQ_BASE);
        /*
         * 8 XIVE END structures per CPU. One for each available
         * priority
         */
        qdev_prop_set_uint32(dev, "nr-ends", nr_servers << 3);
        qdev_init_nofail(dev);

        spapr->xive = SPAPR_XIVE(dev);

        /* Enable the CPU IPIs */
        for (i = 0; i < nr_servers; ++i) {
            if (spapr_xive_irq_claim(spapr->xive, SPAPR_IRQ_IPI + i,
                                     false, errp) < 0) {
                return;
            }
        }

        spapr_xive_hcall_init(spapr);
    }

    spapr->qirqs = qemu_allocate_irqs(spapr->irq->set_irq, spapr,
                                      spapr->irq->nr_xirqs + SPAPR_XIRQ_BASE);
}

int spapr_irq_claim(SpaprMachineState *spapr, int irq, bool lsi, Error **errp)
{
    assert(irq >= SPAPR_XIRQ_BASE);
    assert(irq < (spapr->irq->nr_xirqs + SPAPR_XIRQ_BASE));

    return spapr->irq->claim(spapr, irq, lsi, errp);
}

void spapr_irq_free(SpaprMachineState *spapr, int irq, int num)
{
    int i;

    assert(irq >= SPAPR_XIRQ_BASE);
    assert((irq + num) <= (spapr->irq->nr_xirqs + SPAPR_XIRQ_BASE));

    for (i = irq; i < (irq + num); i++) {
        spapr->irq->free(spapr, i);
    }
}

qemu_irq spapr_qirq(SpaprMachineState *spapr, int irq)
{
    /*
     * This interface is basically for VIO and PHB devices to find the
     * right qemu_irq to manipulate, so we only allow access to the
     * external irqs for now.  Currently anything which needs to
     * access the IPIs most naturally gets there via the guest side
     * interfaces, we can change this if we need to in future.
     */
    assert(irq >= SPAPR_XIRQ_BASE);
    assert(irq < (spapr->irq->nr_xirqs + SPAPR_XIRQ_BASE));

    if (spapr->ics) {
        assert(ics_valid_irq(spapr->ics, irq));
    }
    if (spapr->xive) {
        assert(irq < spapr->xive->nr_irqs);
        assert(xive_eas_is_valid(&spapr->xive->eat[irq]));
    }

    return spapr->qirqs[irq];
}

int spapr_irq_post_load(SpaprMachineState *spapr, int version_id)
{
    return spapr->irq->post_load(spapr, version_id);
}

void spapr_irq_reset(SpaprMachineState *spapr, Error **errp)
{
    assert(!spapr->irq_map || bitmap_empty(spapr->irq_map, spapr->irq_map_nr));

    if (spapr->irq->reset) {
        spapr->irq->reset(spapr, errp);
    }
}

int spapr_irq_get_phandle(SpaprMachineState *spapr, void *fdt, Error **errp)
{
    const char *nodename = "interrupt-controller";
    int offset, phandle;

    offset = fdt_subnode_offset(fdt, 0, nodename);
    if (offset < 0) {
        error_setg(errp, "Can't find node \"%s\": %s",
                   nodename, fdt_strerror(offset));
        return -1;
    }

    phandle = fdt_get_phandle(fdt, offset);
    if (!phandle) {
        error_setg(errp, "Can't get phandle of node \"%s\"", nodename);
        return -1;
    }

    return phandle;
}

/*
 * XICS legacy routines - to deprecate one day
 */

static int ics_find_free_block(ICSState *ics, int num, int alignnum)
{
    int first, i;

    for (first = 0; first < ics->nr_irqs; first += alignnum) {
        if (num > (ics->nr_irqs - first)) {
            return -1;
        }
        for (i = first; i < first + num; ++i) {
            if (!ics_irq_free(ics, i)) {
                break;
            }
        }
        if (i == (first + num)) {
            return first;
        }
    }

    return -1;
}

int spapr_irq_find(SpaprMachineState *spapr, int num, bool align, Error **errp)
{
    ICSState *ics = spapr->ics;
    int first = -1;

    assert(ics);

    /*
     * MSIMesage::data is used for storing VIRQ so
     * it has to be aligned to num to support multiple
     * MSI vectors. MSI-X is not affected by this.
     * The hint is used for the first IRQ, the rest should
     * be allocated continuously.
     */
    if (align) {
        assert((num == 1) || (num == 2) || (num == 4) ||
               (num == 8) || (num == 16) || (num == 32));
        first = ics_find_free_block(ics, num, num);
    } else {
        first = ics_find_free_block(ics, num, 1);
    }

    if (first < 0) {
        error_setg(errp, "can't find a free %d-IRQ block", num);
        return -1;
    }

    return first + ics->offset;
}

#define SPAPR_IRQ_XICS_LEGACY_NR_XIRQS     0x400

SpaprIrq spapr_irq_xics_legacy = {
    .nr_xirqs    = SPAPR_IRQ_XICS_LEGACY_NR_XIRQS,
    .nr_msis     = SPAPR_IRQ_XICS_LEGACY_NR_XIRQS,
    .xics        = true,
    .xive        = false,

    .claim       = spapr_irq_claim_xics,
    .free        = spapr_irq_free_xics,
    .print_info  = spapr_irq_print_info_xics,
    .dt_populate = spapr_dt_xics,
    .cpu_intc_create = spapr_irq_cpu_intc_create_xics,
    .post_load   = spapr_irq_post_load_xics,
    .reset       = spapr_irq_reset_xics,
    .set_irq     = spapr_irq_set_irq_xics,
    .init_kvm    = spapr_irq_init_kvm_xics,
};

static void spapr_irq_register_types(void)
{
    type_register_static(&spapr_intc_info);
}

type_init(spapr_irq_register_types)
