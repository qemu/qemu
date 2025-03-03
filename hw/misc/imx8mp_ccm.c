/*
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * i.MX 8M Plus CCM IP block emulation code
 *
 * Based on hw/misc/imx7_ccm.c
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"

#include "hw/misc/imx8mp_ccm.h"
#include "migration/vmstate.h"

#include "trace.h"

#define CKIH_FREQ 16000000 /* 16MHz crystal input */

static void imx8mp_ccm_reset(DeviceState *dev)
{
    IMX8MPCCMState *s = IMX8MP_CCM(dev);

    memset(s->ccm, 0, sizeof(s->ccm));
}

#define CCM_INDEX(offset)   (((offset) & ~(hwaddr)0xF) / sizeof(uint32_t))
#define CCM_BITOP(offset)   ((offset) & (hwaddr)0xF)

enum {
    CCM_BITOP_NONE = 0x00,
    CCM_BITOP_SET  = 0x04,
    CCM_BITOP_CLR  = 0x08,
    CCM_BITOP_TOG  = 0x0C,
};

static uint64_t imx8mp_set_clr_tog_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    const uint32_t *mmio = opaque;

    return mmio[CCM_INDEX(offset)];
}

static void imx8mp_set_clr_tog_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    const uint8_t  bitop = CCM_BITOP(offset);
    const uint32_t index = CCM_INDEX(offset);
    uint32_t *mmio = opaque;

    switch (bitop) {
    case CCM_BITOP_NONE:
        mmio[index]  = value;
        break;
    case CCM_BITOP_SET:
        mmio[index] |= value;
        break;
    case CCM_BITOP_CLR:
        mmio[index] &= ~value;
        break;
    case CCM_BITOP_TOG:
        mmio[index] ^= value;
        break;
    };
}

static const struct MemoryRegionOps imx8mp_set_clr_tog_ops = {
    .read = imx8mp_set_clr_tog_read,
    .write = imx8mp_set_clr_tog_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
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

static void imx8mp_ccm_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX8MPCCMState *s = IMX8MP_CCM(obj);

    memory_region_init_io(&s->iomem,
                          obj,
                          &imx8mp_set_clr_tog_ops,
                          s->ccm,
                          TYPE_IMX8MP_CCM ".ccm",
                          sizeof(s->ccm));

    sysbus_init_mmio(sd, &s->iomem);
}

static const VMStateDescription imx8mp_ccm_vmstate = {
    .name = TYPE_IMX8MP_CCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ccm, IMX8MPCCMState, CCM_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t imx8mp_ccm_get_clock_frequency(IMXCCMState *dev, IMXClk clock)
{
    /*
     * This function is "consumed" by GPT emulation code. Some clocks
     * have fixed frequencies and we can provide requested frequency
     * easily. However for CCM provided clocks (like IPG) each GPT
     * timer can have its own clock root.
     * This means we need additional information when calling this
     * function to know the requester's identity.
     */
    uint32_t freq = 0;

    switch (clock) {
    case CLK_NONE:
        break;
    case CLK_32k:
        freq = CKIL_FREQ;
        break;
    case CLK_HIGH:
        freq = CKIH_FREQ;
        break;
    case CLK_IPG:
    case CLK_IPG_HIGH:
        /*
         * For now we don't have a way to figure out the device this
         * function is called for. Until then the IPG derived clocks
         * are left unimplemented.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Clock %d Not implemented\n",
                      TYPE_IMX8MP_CCM, __func__, clock);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: unsupported clock %d\n",
                      TYPE_IMX8MP_CCM, __func__, clock);
        break;
    }

    trace_ccm_clock_freq(clock, freq);

    return freq;
}

static void imx8mp_ccm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IMXCCMClass *ccm = IMX_CCM_CLASS(klass);

    device_class_set_legacy_reset(dc, imx8mp_ccm_reset);
    dc->vmsd  = &imx8mp_ccm_vmstate;
    dc->desc  = "i.MX 8M Plus Clock Control Module";

    ccm->get_clock_frequency = imx8mp_ccm_get_clock_frequency;
}

static const TypeInfo imx8mp_ccm_types[] = {
    {
        .name          = TYPE_IMX8MP_CCM,
        .parent        = TYPE_IMX_CCM,
        .instance_size = sizeof(IMX8MPCCMState),
        .instance_init = imx8mp_ccm_init,
        .class_init    = imx8mp_ccm_class_init,
    },
};

DEFINE_TYPES(imx8mp_ccm_types);
