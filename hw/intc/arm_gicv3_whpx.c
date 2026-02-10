/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM Generic Interrupt Controller using HVF platform support
 *
 * Copyright (c) 2025 Mohamed Mediouni
 * Based on vGICv3 KVM code by Pavel Fedin
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/arm_gicv3_common.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "system/runstate.h"
#include "system/whpx.h"
#include "system/whpx-internal.h"
#include "gicv3_internal.h"
#include "vgic_common.h"
#include "migration/blocker.h"
#include "qom/object.h"
#include "target/arm/cpregs.h"

#include "hw/arm/bsa.h"
#include <winhvplatform.h>
#include <winhvplatformdefs.h>
#include <winnt.h>

struct WHPXARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

OBJECT_DECLARE_TYPE(GICv3State, WHPXARMGICv3Class, WHPX_GICV3)

/* TODO: Implement GIC state save-restore */
static void whpx_gicv3_check(GICv3State *s)
{
}

static void whpx_gicv3_put(GICv3State *s)
{
    whpx_gicv3_check(s);
}

static void whpx_gicv3_get(GICv3State *s)
{
}

static void whpx_gicv3_set_irq(void *opaque, int irq, int level)
{
    struct whpx_state *whpx = &whpx_global;
    GICv3State *s = opaque;
    WHV_INTERRUPT_CONTROL interrupt_control = {
        .InterruptControl.InterruptType = WHvArm64InterruptTypeFixed,
        .RequestedVector = GIC_INTERNAL + irq,
        .InterruptControl.Asserted = level
    };

    if (irq > s->num_irq) {
        return;
    }


    whp_dispatch.WHvRequestInterrupt(whpx->partition, &interrupt_control,
         sizeof(interrupt_control));
}

static void whpx_gicv3_icc_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    GICv3CPUState *c;

    c = env->gicv3state;

    c->icc_pmr_el1 = 0;
    /*
     * Architecturally the reset value of the ICC_BPR registers
     * is UNKNOWN. We set them all to 0 here; when the kernel
     * uses these values to program the ICH_VMCR_EL2 fields that
     * determine the guest-visible ICC_BPR register values, the
     * hardware's "writing a value less than the minimum sets
     * the field to the minimum value" behaviour will result in
     * them effectively resetting to the correct minimum value
     * for the host GIC.
     */
    c->icc_bpr[GICV3_G0] = 0;
    c->icc_bpr[GICV3_G1] = 0;
    c->icc_bpr[GICV3_G1NS] = 0;

    c->icc_sre_el1 = 0x7;
    memset(c->icc_apr, 0, sizeof(c->icc_apr));
    memset(c->icc_igrpen, 0, sizeof(c->icc_igrpen));
}

static void whpx_gicv3_reset_hold(Object *obj, ResetType type)
{
    GICv3State *s = ARM_GICV3_COMMON(obj);
    WHPXARMGICv3Class *kgc = WHPX_GICV3_GET_CLASS(s);

    if (kgc->parent_phases.hold) {
        kgc->parent_phases.hold(obj, type);
    }

    whpx_gicv3_put(s);
}


/*
 * CPU interface registers of GIC needs to be reset on CPU reset.
 * For the calling arm_gicv3_icc_reset() on CPU reset, we register
 * below ARMCPRegInfo. As we reset the whole cpu interface under single
 * register reset, we define only one register of CPU interface instead
 * of defining all the registers.
 */
static const ARMCPRegInfo gicv3_cpuif_reginfo[] = {
    { .name = "ICC_CTLR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 12, .opc2 = 4,
      /*
       * If ARM_CP_NOP is used, resetfn is not called,
       * So ARM_CP_NO_RAW is appropriate type.
       */
      .type = ARM_CP_NO_RAW,
      .access = PL1_RW,
      .readfn = arm_cp_read_zero,
      .writefn = arm_cp_write_ignore,
      /*
       * We hang the whole cpu interface reset routine off here
       * rather than parcelling it out into one little function
       * per register
       */
      .resetfn = whpx_gicv3_icc_reset,
    },
};

static void whpx_set_reg(CPUState *cpu, WHV_REGISTER_NAME reg, WHV_REGISTER_VALUE val)
{
    struct whpx_state *whpx = &whpx_global;
    HRESULT hr;

    hr = whp_dispatch.WHvSetVirtualProcessorRegisters(whpx->partition, cpu->cpu_index,
         &reg, 1, &val);

    if (FAILED(hr)) {
        error_report("WHPX: Failed to set register %08x, hr=%08lx", reg, hr);
    }
}

static void whpx_gicv3_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    GICv3State *s = WHPX_GICV3(dev);
    WHPXARMGICv3Class *kgc = WHPX_GICV3_GET_CLASS(s);
    int i;

    kgc->parent_realize(dev, errp);
    if (*errp) {
        return;
    }

    if (s->revision != 3) {
        error_setg(errp, "unsupported GIC revision %d for platform GIC",
                   s->revision);
        return;
    }

    if (s->security_extn) {
        error_setg(errp, "the platform vGICv3 does not implement the "
                   "security extensions");
        return;
    }

    if (s->nmi_support) {
        error_setg(errp, "NMI is not supported with the platform GIC");
        return;
    }

    if (s->nb_redist_regions > 1) {
        error_setg(errp, "Multiple VGICv3 redistributor regions are not "
                   "supported by WHPX");
        error_append_hint(errp, "A maximum of %d VCPUs can be used",
                          s->redist_region_count[0]);
        return;
    }

    gicv3_init_irqs_and_mmio(s, whpx_gicv3_set_irq, NULL);

    for (i = 0; i < s->num_cpu; i++) {
        CPUState *cpu_state = qemu_get_cpu(i);
        ARMCPU *cpu = ARM_CPU(cpu_state);
        WHV_REGISTER_VALUE val = {.Reg64 = 0x080A0000 + (GICV3_REDIST_SIZE * i)};
        whpx_set_reg(cpu_state, WHvArm64RegisterGicrBaseGpa, val);
        define_arm_cp_regs(cpu, gicv3_cpuif_reginfo);
    }

    if (s->maint_irq) {
        error_setg(errp, "Nested virtualisation not currently supported by WHPX.");
        return;
    }

    error_setg(&s->migration_blocker,
        "Live migration disabled because GIC state save/restore not supported on WHPX");
    if (migrate_add_blocker(&s->migration_blocker, errp) < 0) {
        error_report_err(*errp);
    }
}

static void whpx_gicv3_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_CLASS(klass);
    WHPXARMGICv3Class *kgc = WHPX_GICV3_CLASS(klass);

    agcc->pre_save = whpx_gicv3_get;
    agcc->post_load = whpx_gicv3_put;

    device_class_set_parent_realize(dc, whpx_gicv3_realize,
                                    &kgc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, whpx_gicv3_reset_hold, NULL,
                                       &kgc->parent_phases);
}

static const TypeInfo whpx_arm_gicv3_info = {
    .name = TYPE_WHPX_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = whpx_gicv3_class_init,
    .class_size = sizeof(WHPXARMGICv3Class),
};

static void whpx_gicv3_register_types(void)
{
    type_register_static(&whpx_arm_gicv3_info);
}

type_init(whpx_gicv3_register_types)
