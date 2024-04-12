/*
 * QEMU model of the ZynqMP APU Control.
 *
 * Copyright (c) 2013-2022 Xilinx Inc
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com> and
 * Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/register.h"

#include "qemu/bitops.h"

#include "hw/misc/xlnx-zynqmp-apu-ctrl.h"

#ifndef XILINX_ZYNQMP_APU_ERR_DEBUG
#define XILINX_ZYNQMP_APU_ERR_DEBUG 0
#endif

static void update_wfi_out(void *opaque)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(opaque);
    unsigned int i, wfi_pending;

    wfi_pending = s->cpu_pwrdwn_req & s->cpu_in_wfi;
    for (i = 0; i < APU_MAX_CPU; i++) {
        qemu_set_irq(s->wfi_out[i], !!(wfi_pending & (1 << i)));
    }
}

static void zynqmp_apu_rvbar_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(reg->opaque);
    int i;

    for (i = 0; i < APU_MAX_CPU; ++i) {
        uint64_t rvbar = s->regs[R_RVBARADDR0L + 2 * i] +
                         ((uint64_t)s->regs[R_RVBARADDR0H + 2 * i] << 32);
        if (s->cpus[i]) {
            object_property_set_int(OBJECT(s->cpus[i]), "rvbar", rvbar,
                                    &error_abort);
        }
    }
}

static void zynqmp_apu_pwrctl_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(reg->opaque);
    unsigned int i, new;

    for (i = 0; i < APU_MAX_CPU; i++) {
        new = val & (1 << i);
        /* Check if CPU's CPUPWRDNREQ has changed. If yes, update GPIOs. */
        if (new != (s->cpu_pwrdwn_req & (1 << i))) {
            qemu_set_irq(s->cpu_power_status[i], !!new);
        }
        s->cpu_pwrdwn_req &= ~(1 << i);
        s->cpu_pwrdwn_req |= new;
    }
    update_wfi_out(s);
}

static void imr_update_irq(XlnxZynqMPAPUCtrl *s)
{
    bool pending = s->regs[R_ISR] & ~s->regs[R_IMR];
    qemu_set_irq(s->irq_imr, pending);
}

static void isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(reg->opaque);
    imr_update_irq(s);
}

static uint64_t ien_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] &= ~val;
    imr_update_irq(s);
    return 0;
}

static uint64_t ids_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] |= val;
    imr_update_irq(s);
    return 0;
}

static const RegisterAccessInfo zynqmp_apu_regs_info[] = {
#define RVBAR_REGDEF(n) \
    {   .name = "RVBAR CPU " #n " Low",  .addr = A_RVBARADDR ## n ## L,    \
            .reset = 0xffff0000ul,                                         \
            .post_write = zynqmp_apu_rvbar_post_write,                     \
    },{ .name = "RVBAR CPU " #n " High", .addr = A_RVBARADDR ## n ## H,    \
            .post_write = zynqmp_apu_rvbar_post_write,                     \
    }
    {   .name = "ERR_CTRL",  .addr = A_APU_ERR_CTRL,
    },{ .name = "ISR",  .addr = A_ISR,
        .w1c = 0x1,
        .post_write = isr_postw,
    },{ .name = "IMR",  .addr = A_IMR,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "IEN",  .addr = A_IEN,
        .pre_write = ien_prew,
    },{ .name = "IDS",  .addr = A_IDS,
        .pre_write = ids_prew,
    },{ .name = "CONFIG_0",  .addr = A_CONFIG_0,
        .reset = 0xf0f,
    },{ .name = "CONFIG_1",  .addr = A_CONFIG_1,
    },
    RVBAR_REGDEF(0),
    RVBAR_REGDEF(1),
    RVBAR_REGDEF(2),
    RVBAR_REGDEF(3),
    { .name = "ACE_CTRL",  .addr = A_ACE_CTRL,
        .reset = 0xf000f,
    },{ .name = "SNOOP_CTRL",  .addr = A_SNOOP_CTRL,
    },{ .name = "PWRCTL",  .addr = A_PWRCTL,
        .post_write = zynqmp_apu_pwrctl_post_write,
    },{ .name = "PWRSTAT",  .addr = A_PWRSTAT,
        .ro = 0x3000f,
    }
};

static void zynqmp_apu_reset_enter(Object *obj, ResetType type)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(obj);
    int i;

    for (i = 0; i < APU_R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }

    s->cpu_pwrdwn_req = 0;
    s->cpu_in_wfi = 0;
}

static void zynqmp_apu_reset_hold(Object *obj, ResetType type)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(obj);

    update_wfi_out(s);
    imr_update_irq(s);
}

static const MemoryRegionOps zynqmp_apu_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void zynqmp_apu_handle_wfi(void *opaque, int irq, int level)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(opaque);

    s->cpu_in_wfi = deposit32(s->cpu_in_wfi, irq, 1, level);
    update_wfi_out(s);
}

static void zynqmp_apu_init(Object *obj)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(obj);
    int i;

    s->reg_array =
        register_init_block32(DEVICE(obj), zynqmp_apu_regs_info,
                              ARRAY_SIZE(zynqmp_apu_regs_info),
                              s->regs_info, s->regs,
                              &zynqmp_apu_ops,
                              XILINX_ZYNQMP_APU_ERR_DEBUG,
                              APU_R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->reg_array->mem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_imr);

    for (i = 0; i < APU_MAX_CPU; ++i) {
        g_autofree gchar *prop_name = g_strdup_printf("cpu%d", i);
        object_property_add_link(obj, prop_name, TYPE_ARM_CPU,
                                 (Object **)&s->cpus[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    /* wfi_out is used to connect to PMU GPIs. */
    qdev_init_gpio_out_named(DEVICE(obj), s->wfi_out, "wfi_out", 4);
    /* CPU_POWER_STATUS is used to connect to INTC redirect. */
    qdev_init_gpio_out_named(DEVICE(obj), s->cpu_power_status,
                             "CPU_POWER_STATUS", 4);
    /* wfi_in is used as input from CPUs as wfi request. */
    qdev_init_gpio_in_named(DEVICE(obj), zynqmp_apu_handle_wfi, "wfi_in", 4);
}

static void zynqmp_apu_finalize(Object *obj)
{
    XlnxZynqMPAPUCtrl *s = XLNX_ZYNQMP_APU_CTRL(obj);
    register_finalize_block(s->reg_array);
}

static const VMStateDescription vmstate_zynqmp_apu = {
    .name = TYPE_XLNX_ZYNQMP_APU_CTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxZynqMPAPUCtrl, APU_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void zynqmp_apu_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_zynqmp_apu;

    rc->phases.enter = zynqmp_apu_reset_enter;
    rc->phases.hold = zynqmp_apu_reset_hold;
}

static const TypeInfo zynqmp_apu_info = {
    .name              = TYPE_XLNX_ZYNQMP_APU_CTRL,
    .parent            = TYPE_SYS_BUS_DEVICE,
    .instance_size     = sizeof(XlnxZynqMPAPUCtrl),
    .class_init        = zynqmp_apu_class_init,
    .instance_init     = zynqmp_apu_init,
    .instance_finalize = zynqmp_apu_finalize,
};

static void zynqmp_apu_register_types(void)
{
    type_register_static(&zynqmp_apu_info);
}

type_init(zynqmp_apu_register_types)
