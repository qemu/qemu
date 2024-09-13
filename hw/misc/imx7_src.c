/*
 * IMX7 System Reset Controller
 *
 * Copyright (c) 2023 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/imx7_src.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "target/arm/arm-powerctl.h"
#include "hw/core/cpu.h"
#include "hw/registerfields.h"

#include "trace.h"

static const char *imx7_src_reg_name(uint32_t reg)
{
    static char unknown[20];

    switch (reg) {
    case SRC_SCR:
        return "SRC_SCR";
    case SRC_A7RCR0:
        return "SRC_A7RCR0";
    case SRC_A7RCR1:
        return "SRC_A7RCR1";
    case SRC_M4RCR:
        return "SRC_M4RCR";
    case SRC_ERCR:
        return "SRC_ERCR";
    case SRC_HSICPHY_RCR:
        return "SRC_HSICPHY_RCR";
    case SRC_USBOPHY1_RCR:
        return "SRC_USBOPHY1_RCR";
    case SRC_USBOPHY2_RCR:
        return "SRC_USBOPHY2_RCR";
    case SRC_PCIEPHY_RCR:
        return "SRC_PCIEPHY_RCR";
    case SRC_SBMR1:
        return "SRC_SBMR1";
    case SRC_SRSR:
        return "SRC_SRSR";
    case SRC_SISR:
        return "SRC_SISR";
    case SRC_SIMR:
        return "SRC_SIMR";
    case SRC_SBMR2:
        return "SRC_SBMR2";
    case SRC_GPR1:
        return "SRC_GPR1";
    case SRC_GPR2:
        return "SRC_GPR2";
    case SRC_GPR3:
        return "SRC_GPR3";
    case SRC_GPR4:
        return "SRC_GPR4";
    case SRC_GPR5:
        return "SRC_GPR5";
    case SRC_GPR6:
        return "SRC_GPR6";
    case SRC_GPR7:
        return "SRC_GPR7";
    case SRC_GPR8:
        return "SRC_GPR8";
    case SRC_GPR9:
        return "SRC_GPR9";
    case SRC_GPR10:
        return "SRC_GPR10";
    default:
        snprintf(unknown, sizeof(unknown), "%u ?", reg);
        return unknown;
    }
}

static const VMStateDescription vmstate_imx7_src = {
    .name = TYPE_IMX7_SRC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMX7SRCState, SRC_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx7_src_reset(DeviceState *dev)
{
    IMX7SRCState *s = IMX7_SRC(dev);

    memset(s->regs, 0, sizeof(s->regs));

    /* Set reset values */
    s->regs[SRC_SCR] = 0xA0;
    s->regs[SRC_SRSR] = 0x1;
    s->regs[SRC_SIMR] = 0x1F;
}

static uint64_t imx7_src_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    IMX7SRCState *s = (IMX7SRCState *)opaque;
    uint32_t index = offset >> 2;

    if (index < SRC_MAX) {
        value = s->regs[index];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX7_SRC, __func__, offset);
    }

    trace_imx7_src_read(imx7_src_reg_name(index), value);

    return value;
}


/*
 * The reset is asynchronous so we need to defer clearing the reset
 * bit until the work is completed.
 */

struct SRCSCRResetInfo {
    IMX7SRCState *s;
    uint32_t reset_bit;
};

static void imx7_clear_reset_bit(CPUState *cpu, run_on_cpu_data data)
{
    struct SRCSCRResetInfo *ri = data.host_ptr;
    IMX7SRCState *s = ri->s;

    assert(bql_locked());

    s->regs[SRC_A7RCR0] = deposit32(s->regs[SRC_A7RCR0], ri->reset_bit, 1, 0);

    trace_imx7_src_write(imx7_src_reg_name(SRC_A7RCR0), s->regs[SRC_A7RCR0]);

    g_free(ri);
}

static void imx7_defer_clear_reset_bit(uint32_t cpuid,
                                       IMX7SRCState *s,
                                       uint32_t reset_shift)
{
    struct SRCSCRResetInfo *ri;
    CPUState *cpu = arm_get_cpu_by_id(cpuid);

    if (!cpu) {
        return;
    }

    ri = g_new(struct SRCSCRResetInfo, 1);
    ri->s = s;
    ri->reset_bit = reset_shift;

    async_run_on_cpu(cpu, imx7_clear_reset_bit, RUN_ON_CPU_HOST_PTR(ri));
}


static void imx7_src_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMX7SRCState *s = (IMX7SRCState *)opaque;
    uint32_t index = offset >> 2;
    long unsigned int change_mask;
    uint32_t current_value = value;

    if (index >= SRC_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX7_SRC, __func__, offset);
        return;
    }

    trace_imx7_src_write(imx7_src_reg_name(SRC_A7RCR0), s->regs[SRC_A7RCR0]);

    change_mask = s->regs[index] ^ (uint32_t)current_value;

    switch (index) {
    case SRC_A7RCR0:
        if (FIELD_EX32(change_mask, CORE0, RST)) {
            arm_reset_cpu(0);
            imx7_defer_clear_reset_bit(0, s, R_CORE0_RST_SHIFT);
        }
        if (FIELD_EX32(change_mask, CORE1, RST)) {
            arm_reset_cpu(1);
            imx7_defer_clear_reset_bit(1, s, R_CORE1_RST_SHIFT);
        }
        s->regs[index] = current_value;
        break;
    case SRC_A7RCR1:
        /*
         * On real hardware when the system reset controller starts a
         * secondary CPU it runs through some boot ROM code which reads
         * the SRC_GPRX registers controlling the start address and branches
         * to it.
         * Here we are taking a short cut and branching directly to the
         * requested address (we don't want to run the boot ROM code inside
         * QEMU)
         */
        if (FIELD_EX32(change_mask, CORE1, ENABLE)) {
            if (FIELD_EX32(current_value, CORE1, ENABLE)) {
                /* CORE 1 is brought up */
                arm_set_cpu_on(1, s->regs[SRC_GPR3], s->regs[SRC_GPR4],
                               3, false);
            } else {
                /* CORE 1 is shut down */
                arm_set_cpu_off(1);
            }
            /* We clear the reset bits as the processor changed state */
            imx7_defer_clear_reset_bit(1, s, R_CORE1_RST_SHIFT);
            clear_bit(R_CORE1_RST_SHIFT, &change_mask);
        }
        s->regs[index] = current_value;
        break;
    default:
        s->regs[index] = current_value;
        break;
    }
}

static const struct MemoryRegionOps imx7_src_ops = {
    .read = imx7_src_read,
    .write = imx7_src_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx7_src_realize(DeviceState *dev, Error **errp)
{
    IMX7SRCState *s = IMX7_SRC(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx7_src_ops, s,
                          TYPE_IMX7_SRC, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void imx7_src_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx7_src_realize;
    device_class_set_legacy_reset(dc, imx7_src_reset);
    dc->vmsd = &vmstate_imx7_src;
    dc->desc = "i.MX6 System Reset Controller";
}

static const TypeInfo imx7_src_info = {
    .name          = TYPE_IMX7_SRC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX7SRCState),
    .class_init    = imx7_src_class_init,
};

static void imx7_src_register_types(void)
{
    type_register_static(&imx7_src_info);
}

type_init(imx7_src_register_types)
