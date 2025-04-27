/*
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * i.MX 8M Plus ANALOG IP block emulation code
 *
 * Based on hw/misc/imx7_ccm.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"

#include "hw/misc/imx8mp_analog.h"
#include "migration/vmstate.h"

#define ANALOG_PLL_LOCK BIT(31)

static void imx8mp_analog_reset(DeviceState *dev)
{
    IMX8MPAnalogState *s = IMX8MP_ANALOG(dev);

    memset(s->analog, 0, sizeof(s->analog));

    s->analog[ANALOG_AUDIO_PLL1_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_AUDIO_PLL1_FDIV_CTL0] = 0x00145032;
    s->analog[ANALOG_AUDIO_PLL1_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL1_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL1_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_AUDIO_PLL2_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_AUDIO_PLL2_FDIV_CTL0] = 0x00145032;
    s->analog[ANALOG_AUDIO_PLL2_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL2_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_AUDIO_PLL2_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_VIDEO_PLL1_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_VIDEO_PLL1_FDIV_CTL0] = 0x00145032;
    s->analog[ANALOG_VIDEO_PLL1_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_VIDEO_PLL1_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_VIDEO_PLL1_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_DRAM_PLL_GEN_CTRL] = 0x00002010;
    s->analog[ANALOG_DRAM_PLL_FDIV_CTL0] = 0x0012c032;
    s->analog[ANALOG_DRAM_PLL_FDIV_CTL1] = 0x00000000;
    s->analog[ANALOG_DRAM_PLL_SSCG_CTRL] = 0x00000000;
    s->analog[ANALOG_DRAM_PLL_MNIT_CTRL] = 0x00100103;
    s->analog[ANALOG_GPU_PLL_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_GPU_PLL_FDIV_CTL0] = 0x000c8031;
    s->analog[ANALOG_GPU_PLL_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_GPU_PLL_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_VPU_PLL_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_VPU_PLL_FDIV_CTL0] = 0x0012c032;
    s->analog[ANALOG_VPU_PLL_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_VPU_PLL_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_ARM_PLL_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_ARM_PLL_FDIV_CTL0] = 0x000fa031;
    s->analog[ANALOG_ARM_PLL_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_ARM_PLL_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_SYS_PLL1_GEN_CTRL] = 0x0aaaa810;
    s->analog[ANALOG_SYS_PLL1_FDIV_CTL0] = 0x00190032;
    s->analog[ANALOG_SYS_PLL1_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_SYS_PLL1_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_SYS_PLL2_GEN_CTRL] = 0x0aaaa810;
    s->analog[ANALOG_SYS_PLL2_FDIV_CTL0] = 0x000fa031;
    s->analog[ANALOG_SYS_PLL2_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_SYS_PLL2_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_SYS_PLL3_GEN_CTRL] = 0x00000810;
    s->analog[ANALOG_SYS_PLL3_FDIV_CTL0] = 0x000fa031;
    s->analog[ANALOG_SYS_PLL3_LOCKD_CTRL] = 0x0010003f;
    s->analog[ANALOG_SYS_PLL3_MNIT_CTRL] = 0x00280081;
    s->analog[ANALOG_OSC_MISC_CFG] = 0x00000000;
    s->analog[ANALOG_ANAMIX_PLL_MNIT_CTL] = 0x00000000;
    s->analog[ANALOG_DIGPROG] = 0x00824010;

    /* all PLLs need to be locked */
    s->analog[ANALOG_AUDIO_PLL1_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_AUDIO_PLL2_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_VIDEO_PLL1_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_DRAM_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_GPU_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_VPU_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_ARM_PLL_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_SYS_PLL1_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_SYS_PLL2_GEN_CTRL] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_SYS_PLL3_GEN_CTRL] |= ANALOG_PLL_LOCK;
}

static uint64_t imx8mp_analog_read(void *opaque, hwaddr offset, unsigned size)
{
    IMX8MPAnalogState *s = opaque;

    return s->analog[offset >> 2];
}

static void imx8mp_analog_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    IMX8MPAnalogState *s = opaque;

    if (offset >> 2 == ANALOG_DIGPROG) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Guest write to read-only ANALOG_DIGPROG register\n");
    } else {
        s->analog[offset >> 2] = value;
    }
}

static const struct MemoryRegionOps imx8mp_analog_ops = {
    .read = imx8mp_analog_read,
    .write = imx8mp_analog_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx8mp_analog_init(Object *obj)
{
    IMX8MPAnalogState *s = IMX8MP_ANALOG(obj);
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);

    memory_region_init(&s->mmio.container, obj, TYPE_IMX8MP_ANALOG, 0x10000);

    memory_region_init_io(&s->mmio.analog, obj, &imx8mp_analog_ops, s,
                          TYPE_IMX8MP_ANALOG, sizeof(s->analog));
    memory_region_add_subregion(&s->mmio.container, 0, &s->mmio.analog);

    sysbus_init_mmio(sd, &s->mmio.container);
}

static const VMStateDescription imx8mp_analog_vmstate = {
    .name = TYPE_IMX8MP_ANALOG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(analog, IMX8MPAnalogState, ANALOG_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx8mp_analog_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, imx8mp_analog_reset);
    dc->vmsd  = &imx8mp_analog_vmstate;
    dc->desc  = "i.MX 8M Plus Analog Module";
}

static const TypeInfo imx8mp_analog_types[] = {
    {
        .name          = TYPE_IMX8MP_ANALOG,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(IMX8MPAnalogState),
        .instance_init = imx8mp_analog_init,
        .class_init    = imx8mp_analog_class_init,
    }
};

DEFINE_TYPES(imx8mp_analog_types);
