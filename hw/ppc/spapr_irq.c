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
#include "system/kvm.h"

#include "trace.h"

QEMU_BUILD_BUG_ON(SPAPR_IRQ_NR_IPIS > SPAPR_XIRQ_BASE);

static const TypeInfo spapr_intc_info = {
    .name = TYPE_SPAPR_INTC,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(SpaprInterruptControllerClass),
};

static void spapr_irq_msi_init(SpaprMachineState *spapr)
{
    spapr->irq_map_nr = SPAPR_IRQ_NR_MSIS;
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

int spapr_irq_init_kvm(SpaprInterruptControllerInitKvm fn,
                       SpaprInterruptController *intc,
                       uint32_t nr_servers,
                       Error **errp)
{
    Error *local_err = NULL;

    if (kvm_enabled() && kvm_kernel_irqchip_allowed()) {
        if (fn(intc, nr_servers, &local_err) < 0) {
            if (kvm_kernel_irqchip_required()) {
                error_prepend(&local_err,
                              "kernel_irqchip requested but unavailable: ");
                error_propagate(errp, local_err);
                return -1;
            }

            /*
             * We failed to initialize the KVM device, fallback to
             * emulated mode
             */
            error_prepend(&local_err,
                          "kernel_irqchip allowed but unavailable: ");
            error_append_hint(&local_err,
                              "Falling back to kernel-irqchip=off\n");
            warn_report_err(local_err);
        }
    }

    return 0;
}

/*
 * XICS IRQ backend.
 */

SpaprIrq spapr_irq_xics = {
    .xics        = true,
    .xive        = false,
};

/*
 * XIVE IRQ backend.
 */

SpaprIrq spapr_irq_xive = {
    .xics        = false,
    .xive        = true,
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
 * Define values in sync with the XIVE and XICS backend
 */
SpaprIrq spapr_irq_dual = {
    .xics        = true,
    .xive        = true,
};


static int spapr_irq_check(SpaprMachineState *spapr, Error **errp)
{
    ERRP_GUARD();
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
        if (!spapr->irq->xics) {
            error_setg(errp, "XIVE-only machines require a POWER9 CPU");
            return -1;
        }
    }

    /*
     * On a POWER9 host, some older KVM XICS devices cannot be destroyed and
     * re-created. Same happens with KVM nested guests. Detect that early to
     * avoid QEMU to exit later when the guest reboots.
     */
    if (kvm_enabled() &&
        spapr->irq == &spapr_irq_dual &&
        kvm_kernel_irqchip_required() &&
        xics_kvm_has_broken_disconnect()) {
        error_setg(errp,
            "KVM is incompatible with ic-mode=dual,kernel-irqchip=on");
        error_append_hint(errp,
            "This can happen with an old KVM or in a KVM nested guest.\n");
        error_append_hint(errp,
            "Try without kernel-irqchip or with kernel-irqchip=off.\n");
        return -1;
    }

    return 0;
}

/*
 * sPAPR IRQ frontend routines for devices
 */
#define ALL_INTCS(spapr_) \
    { SPAPR_INTC((spapr_)->ics), SPAPR_INTC((spapr_)->xive), }

int spapr_irq_cpu_intc_create(SpaprMachineState *spapr,
                              PowerPCCPU *cpu, Error **errp)
{
    SpaprInterruptController *intcs[] = ALL_INTCS(spapr);
    int i;
    int rc;

    for (i = 0; i < ARRAY_SIZE(intcs); i++) {
        SpaprInterruptController *intc = intcs[i];
        if (intc) {
            SpaprInterruptControllerClass *sicc = SPAPR_INTC_GET_CLASS(intc);
            rc = sicc->cpu_intc_create(intc, cpu, errp);
            if (rc < 0) {
                return rc;
            }
        }
    }

    return 0;
}

void spapr_irq_cpu_intc_reset(SpaprMachineState *spapr, PowerPCCPU *cpu)
{
    SpaprInterruptController *intcs[] = ALL_INTCS(spapr);
    int i;

    for (i = 0; i < ARRAY_SIZE(intcs); i++) {
        SpaprInterruptController *intc = intcs[i];
        if (intc) {
            SpaprInterruptControllerClass *sicc = SPAPR_INTC_GET_CLASS(intc);
            sicc->cpu_intc_reset(intc, cpu);
        }
    }
}

void spapr_irq_cpu_intc_destroy(SpaprMachineState *spapr, PowerPCCPU *cpu)
{
    SpaprInterruptController *intcs[] = ALL_INTCS(spapr);
    int i;

    for (i = 0; i < ARRAY_SIZE(intcs); i++) {
        SpaprInterruptController *intc = intcs[i];
        if (intc) {
            SpaprInterruptControllerClass *sicc = SPAPR_INTC_GET_CLASS(intc);
            sicc->cpu_intc_destroy(intc, cpu);
        }
    }
}

static void spapr_set_irq(void *opaque, int irq, int level)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(opaque);
    SpaprInterruptControllerClass *sicc
        = SPAPR_INTC_GET_CLASS(spapr->active_intc);

    sicc->set_irq(spapr->active_intc, irq, level);
}

void spapr_irq_print_info(SpaprMachineState *spapr, GString *buf)
{
    SpaprInterruptControllerClass *sicc
        = SPAPR_INTC_GET_CLASS(spapr->active_intc);

    sicc->print_info(spapr->active_intc, buf);
}

void spapr_irq_dt(SpaprMachineState *spapr, uint32_t nr_servers,
                  void *fdt, uint32_t phandle)
{
    SpaprInterruptControllerClass *sicc
        = SPAPR_INTC_GET_CLASS(spapr->active_intc);

    sicc->dt(spapr->active_intc, nr_servers, fdt, phandle);
}

void spapr_irq_init(SpaprMachineState *spapr, Error **errp)
{
    if (kvm_enabled() && kvm_kernel_irqchip_split()) {
        error_setg(errp, "kernel_irqchip split mode not supported on pseries");
        return;
    }

    if (spapr_irq_check(spapr, errp) < 0) {
        return;
    }

    /* Initialize the MSI IRQ allocator. */
    spapr_irq_msi_init(spapr);

    if (spapr->irq->xics) {
        Object *obj;

        obj = object_new(TYPE_ICS_SPAPR);

        object_property_add_child(OBJECT(spapr), "ics", obj);
        object_property_set_link(obj, ICS_PROP_XICS, OBJECT(spapr),
                                 &error_abort);
        object_property_set_int(obj, "nr-irqs", SPAPR_NR_XIRQS, &error_abort);
        if (!qdev_realize(DEVICE(obj), NULL, errp)) {
            return;
        }

        spapr->ics = ICS_SPAPR(obj);
    }

    if (spapr->irq->xive) {
        uint32_t nr_servers = spapr_max_server_number(spapr);
        DeviceState *dev;
        int i;

        dev = qdev_new(TYPE_SPAPR_XIVE);
        qdev_prop_set_uint32(dev, "nr-irqs", SPAPR_NR_XIRQS + SPAPR_IRQ_NR_IPIS);
        /*
         * 8 XIVE END structures per CPU. One for each available
         * priority
         */
        qdev_prop_set_uint32(dev, "nr-ends", nr_servers << 3);
        object_property_set_link(OBJECT(dev), "xive-fabric", OBJECT(spapr),
                                 &error_abort);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

        spapr->xive = SPAPR_XIVE(dev);

        /* Enable the CPU IPIs */
        for (i = 0; i < nr_servers; ++i) {
            SpaprInterruptControllerClass *sicc
                = SPAPR_INTC_GET_CLASS(spapr->xive);

            if (sicc->claim_irq(SPAPR_INTC(spapr->xive), SPAPR_IRQ_IPI + i,
                                false, errp) < 0) {
                return;
            }
        }

        spapr_xive_hcall_init(spapr);
    }

    spapr->qirqs = qemu_allocate_irqs(spapr_set_irq, spapr,
                                      SPAPR_NR_XIRQS + SPAPR_IRQ_NR_IPIS);

    /*
     * Mostly we don't actually need this until reset, except that not
     * having this set up can cause VFIO devices to issue a
     * false-positive warning during realize(), because they don't yet
     * have an in-kernel irq chip.
     */
    spapr_irq_update_active_intc(spapr);
}

int spapr_irq_claim(SpaprMachineState *spapr, int irq, bool lsi, Error **errp)
{
    SpaprInterruptController *intcs[] = ALL_INTCS(spapr);
    int i;
    int rc;

    assert(irq >= SPAPR_XIRQ_BASE);
    assert(irq < (SPAPR_NR_XIRQS + SPAPR_XIRQ_BASE));

    for (i = 0; i < ARRAY_SIZE(intcs); i++) {
        SpaprInterruptController *intc = intcs[i];
        if (intc) {
            SpaprInterruptControllerClass *sicc = SPAPR_INTC_GET_CLASS(intc);
            rc = sicc->claim_irq(intc, irq, lsi, errp);
            if (rc < 0) {
                return rc;
            }
        }
    }

    return 0;
}

void spapr_irq_free(SpaprMachineState *spapr, int irq, int num)
{
    SpaprInterruptController *intcs[] = ALL_INTCS(spapr);
    int i, j;

    assert(irq >= SPAPR_XIRQ_BASE);
    assert((irq + num) <= (SPAPR_NR_XIRQS + SPAPR_XIRQ_BASE));

    for (i = irq; i < (irq + num); i++) {
        for (j = 0; j < ARRAY_SIZE(intcs); j++) {
            SpaprInterruptController *intc = intcs[j];

            if (intc) {
                SpaprInterruptControllerClass *sicc
                    = SPAPR_INTC_GET_CLASS(intc);
                sicc->free_irq(intc, i);
            }
        }
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
    assert(irq < (SPAPR_NR_XIRQS + SPAPR_XIRQ_BASE));

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
    SpaprInterruptControllerClass *sicc;

    spapr_irq_update_active_intc(spapr);
    sicc = SPAPR_INTC_GET_CLASS(spapr->active_intc);
    return sicc->post_load(spapr->active_intc, version_id);
}

void spapr_irq_reset(SpaprMachineState *spapr, Error **errp)
{
    assert(!spapr->irq_map || bitmap_empty(spapr->irq_map, spapr->irq_map_nr));

    spapr_irq_update_active_intc(spapr);
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

static void set_active_intc(SpaprMachineState *spapr,
                            SpaprInterruptController *new_intc)
{
    SpaprInterruptControllerClass *sicc;
    uint32_t nr_servers = spapr_max_server_number(spapr);

    assert(new_intc);

    if (new_intc == spapr->active_intc) {
        /* Nothing to do */
        return;
    }

    if (spapr->active_intc) {
        sicc = SPAPR_INTC_GET_CLASS(spapr->active_intc);
        if (sicc->deactivate) {
            sicc->deactivate(spapr->active_intc);
        }
    }

    sicc = SPAPR_INTC_GET_CLASS(new_intc);
    if (sicc->activate) {
        sicc->activate(new_intc, nr_servers, &error_fatal);
    }

    spapr->active_intc = new_intc;

    /*
     * We've changed the kernel irqchip, let VFIO devices know they
     * need to readjust.
     */
    kvm_irqchip_change_notify();
}

void spapr_irq_update_active_intc(SpaprMachineState *spapr)
{
    SpaprInterruptController *new_intc;

    if (!spapr->ics) {
        /*
         * XXX before we run CAS, ov5_cas is initialized empty, which
         * indicates XICS, even if we have ic-mode=xive.  TODO: clean
         * up the CAS path so that we have a clearer way of handling
         * this.
         */
        new_intc = SPAPR_INTC(spapr->xive);
    } else if (spapr->ov5_cas
               && spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT)) {
        new_intc = SPAPR_INTC(spapr->xive);
    } else {
        new_intc = SPAPR_INTC(spapr->ics);
    }

    set_active_intc(spapr, new_intc);
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

static void spapr_irq_register_types(void)
{
    type_register_static(&spapr_intc_info);
}

type_init(spapr_irq_register_types)
