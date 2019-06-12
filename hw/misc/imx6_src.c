/*
 * IMX6 System Reset Controller
 *
 * Copyright (c) 2015 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/misc/imx6_src.h"
#include "sysemu/sysemu.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "arm-powerctl.h"
#include "qom/cpu.h"

#ifndef DEBUG_IMX6_SRC
#define DEBUG_IMX6_SRC 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX6_SRC) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX6_SRC, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx6_src_reg_name(uint32_t reg)
{
    static char unknown[20];

    switch (reg) {
    case SRC_SCR:
        return "SRC_SCR";
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
        sprintf(unknown, "%d ?", reg);
        return unknown;
    }
}

static const VMStateDescription vmstate_imx6_src = {
    .name = TYPE_IMX6_SRC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMX6SRCState, SRC_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx6_src_reset(DeviceState *dev)
{
    IMX6SRCState *s = IMX6_SRC(dev);

    DPRINTF("\n");

    memset(s->regs, 0, sizeof(s->regs));

    /* Set reset values */
    s->regs[SRC_SCR] = 0x521;
    s->regs[SRC_SRSR] = 0x1;
    s->regs[SRC_SIMR] = 0x1F;
}

static uint64_t imx6_src_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    IMX6SRCState *s = (IMX6SRCState *)opaque;
    uint32_t index = offset >> 2;

    if (index < SRC_MAX) {
        value = s->regs[index];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX6_SRC, __func__, offset);

    }

    DPRINTF("reg[%s] => 0x%" PRIx32 "\n", imx6_src_reg_name(index), value);

    return value;
}


/* The reset is asynchronous so we need to defer clearing the reset
 * bit until the work is completed.
 */

struct SRCSCRResetInfo {
    IMX6SRCState *s;
    int reset_bit;
};

static void imx6_clear_reset_bit(CPUState *cpu, run_on_cpu_data data)
{
    struct SRCSCRResetInfo *ri = data.host_ptr;
    IMX6SRCState *s = ri->s;

    assert(qemu_mutex_iothread_locked());

    s->regs[SRC_SCR] = deposit32(s->regs[SRC_SCR], ri->reset_bit, 1, 0);
    DPRINTF("reg[%s] <= 0x%" PRIx32 "\n",
            imx6_src_reg_name(SRC_SCR), s->regs[SRC_SCR]);

    g_free(ri);
}

static void imx6_defer_clear_reset_bit(int cpuid,
                                       IMX6SRCState *s,
                                       unsigned long reset_shift)
{
    struct SRCSCRResetInfo *ri;
    CPUState *cpu = arm_get_cpu_by_id(cpuid);

    if (!cpu) {
        return;
    }

    ri = g_malloc(sizeof(struct SRCSCRResetInfo));
    ri->s = s;
    ri->reset_bit = reset_shift;

    async_run_on_cpu(cpu, imx6_clear_reset_bit, RUN_ON_CPU_HOST_PTR(ri));
}


static void imx6_src_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMX6SRCState *s = (IMX6SRCState *)opaque;
    uint32_t index = offset >> 2;
    unsigned long change_mask;
    unsigned long current_value = value;

    if (index >=  SRC_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX6_SRC, __func__, offset);
        return;
    }

    DPRINTF("reg[%s] <= 0x%" PRIx32 "\n", imx6_src_reg_name(index),
            (uint32_t)current_value);

    change_mask = s->regs[index] ^ (uint32_t)current_value;

    switch (index) {
    case SRC_SCR:
        /*
         * On real hardware when the system reset controller starts a
         * secondary CPU it runs through some boot ROM code which reads
         * the SRC_GPRX registers controlling the start address and branches
         * to it.
         * Here we are taking a short cut and branching directly to the
         * requested address (we don't want to run the boot ROM code inside
         * QEMU)
         */
        if (EXTRACT(change_mask, CORE3_ENABLE)) {
            if (EXTRACT(current_value, CORE3_ENABLE)) {
                /* CORE 3 is brought up */
                arm_set_cpu_on(3, s->regs[SRC_GPR7], s->regs[SRC_GPR8],
                               3, false);
            } else {
                /* CORE 3 is shut down */
                arm_set_cpu_off(3);
            }
            /* We clear the reset bits as the processor changed state */
            imx6_defer_clear_reset_bit(3, s, CORE3_RST_SHIFT);
            clear_bit(CORE3_RST_SHIFT, &change_mask);
        }
        if (EXTRACT(change_mask, CORE2_ENABLE)) {
            if (EXTRACT(current_value, CORE2_ENABLE)) {
                /* CORE 2 is brought up */
                arm_set_cpu_on(2, s->regs[SRC_GPR5], s->regs[SRC_GPR6],
                               3, false);
            } else {
                /* CORE 2 is shut down */
                arm_set_cpu_off(2);
            }
            /* We clear the reset bits as the processor changed state */
            imx6_defer_clear_reset_bit(2, s, CORE2_RST_SHIFT);
            clear_bit(CORE2_RST_SHIFT, &change_mask);
        }
        if (EXTRACT(change_mask, CORE1_ENABLE)) {
            if (EXTRACT(current_value, CORE1_ENABLE)) {
                /* CORE 1 is brought up */
                arm_set_cpu_on(1, s->regs[SRC_GPR3], s->regs[SRC_GPR4],
                               3, false);
            } else {
                /* CORE 1 is shut down */
                arm_set_cpu_off(1);
            }
            /* We clear the reset bits as the processor changed state */
            imx6_defer_clear_reset_bit(1, s, CORE1_RST_SHIFT);
            clear_bit(CORE1_RST_SHIFT, &change_mask);
        }
        if (EXTRACT(change_mask, CORE0_RST)) {
            arm_reset_cpu(0);
            imx6_defer_clear_reset_bit(0, s, CORE0_RST_SHIFT);
        }
        if (EXTRACT(change_mask, CORE1_RST)) {
            arm_reset_cpu(1);
            imx6_defer_clear_reset_bit(1, s, CORE1_RST_SHIFT);
        }
        if (EXTRACT(change_mask, CORE2_RST)) {
            arm_reset_cpu(2);
            imx6_defer_clear_reset_bit(2, s, CORE2_RST_SHIFT);
        }
        if (EXTRACT(change_mask, CORE3_RST)) {
            arm_reset_cpu(3);
            imx6_defer_clear_reset_bit(3, s, CORE3_RST_SHIFT);
        }
        if (EXTRACT(change_mask, SW_IPU2_RST)) {
            /* We pretend the IPU2 is reset */
            clear_bit(SW_IPU2_RST_SHIFT, &current_value);
        }
        if (EXTRACT(change_mask, SW_IPU1_RST)) {
            /* We pretend the IPU1 is reset */
            clear_bit(SW_IPU1_RST_SHIFT, &current_value);
        }
        s->regs[index] = current_value;
        break;
    default:
        s->regs[index] = current_value;
        break;
    }
}

static const struct MemoryRegionOps imx6_src_ops = {
    .read = imx6_src_read,
    .write = imx6_src_write,
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

static void imx6_src_realize(DeviceState *dev, Error **errp)
{
    IMX6SRCState *s = IMX6_SRC(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx6_src_ops, s,
                          TYPE_IMX6_SRC, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void imx6_src_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx6_src_realize;
    dc->reset = imx6_src_reset;
    dc->vmsd = &vmstate_imx6_src;
    dc->desc = "i.MX6 System Reset Controller";
}

static const TypeInfo imx6_src_info = {
    .name          = TYPE_IMX6_SRC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX6SRCState),
    .class_init    = imx6_src_class_init,
};

static void imx6_src_register_types(void)
{
    type_register_static(&imx6_src_info);
}

type_init(imx6_src_register_types)
