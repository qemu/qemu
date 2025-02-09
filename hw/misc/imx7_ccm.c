/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * i.MX7 CCM, PMU and ANALOG IP blocks emulation code
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "hw/misc/imx7_ccm.h"
#include "migration/vmstate.h"

#include "trace.h"

#define CKIH_FREQ 24000000 /* 24MHz crystal input */

static void imx7_analog_reset(DeviceState *dev)
{
    IMX7AnalogState *s = IMX7_ANALOG(dev);

    memset(s->pmu, 0, sizeof(s->pmu));
    memset(s->analog, 0, sizeof(s->analog));

    s->analog[ANALOG_PLL_ARM]         = 0x00002042;
    s->analog[ANALOG_PLL_DDR]         = 0x0060302c;
    s->analog[ANALOG_PLL_DDR_SS]      = 0x00000000;
    s->analog[ANALOG_PLL_DDR_NUM]     = 0x06aaac4d;
    s->analog[ANALOG_PLL_DDR_DENOM]   = 0x100003ec;
    s->analog[ANALOG_PLL_480]         = 0x00002000;
    s->analog[ANALOG_PLL_480A]        = 0x52605a56;
    s->analog[ANALOG_PLL_480B]        = 0x52525216;
    s->analog[ANALOG_PLL_ENET]        = 0x00001fc0;
    s->analog[ANALOG_PLL_AUDIO]       = 0x0001301b;
    s->analog[ANALOG_PLL_AUDIO_SS]    = 0x00000000;
    s->analog[ANALOG_PLL_AUDIO_NUM]   = 0x05f5e100;
    s->analog[ANALOG_PLL_AUDIO_DENOM] = 0x2964619c;
    s->analog[ANALOG_PLL_VIDEO]       = 0x0008201b;
    s->analog[ANALOG_PLL_VIDEO_SS]    = 0x00000000;
    s->analog[ANALOG_PLL_VIDEO_NUM]   = 0x0000f699;
    s->analog[ANALOG_PLL_VIDEO_DENOM] = 0x000f4240;
    s->analog[ANALOG_PLL_MISC0]       = 0x00000000;

    /* all PLLs need to be locked */
    s->analog[ANALOG_PLL_ARM]   |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_DDR]   |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_480]   |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_480A]  |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_480B]  |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_ENET]  |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_AUDIO] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_VIDEO] |= ANALOG_PLL_LOCK;
    s->analog[ANALOG_PLL_MISC0] |= ANALOG_PLL_LOCK;

    /*
     * Since I couldn't find any info about this in the reference
     * manual the value of this register is based strictly on matching
     * what Linux kernel expects it to be.
     */
    s->analog[ANALOG_DIGPROG]  = 0x720000;
    /*
     * Set revision to be 1.0 (Arbitrary choice, no particular
     * reason).
     */
    s->analog[ANALOG_DIGPROG] |= 0x000010;
}

static void imx7_ccm_reset(DeviceState *dev)
{
    IMX7CCMState *s = IMX7_CCM(dev);

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

static uint64_t imx7_set_clr_tog_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const uint32_t *mmio = opaque;

    return mmio[CCM_INDEX(offset)];
}

static void imx7_set_clr_tog_write(void *opaque, hwaddr offset,
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

static const struct MemoryRegionOps imx7_set_clr_tog_ops = {
    .read = imx7_set_clr_tog_read,
    .write = imx7_set_clr_tog_write,
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

static void imx7_digprog_write(void *opaque, hwaddr addr,
                                        uint64_t data, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "Guest write to read-only ANALOG_DIGPROG register\n");
}

static const struct MemoryRegionOps imx7_digprog_ops = {
    .read = imx7_set_clr_tog_read,
    .write = imx7_digprog_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx7_ccm_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX7CCMState *s = IMX7_CCM(obj);

    memory_region_init_io(&s->iomem,
                          obj,
                          &imx7_set_clr_tog_ops,
                          s->ccm,
                          TYPE_IMX7_CCM ".ccm",
                          sizeof(s->ccm));

    sysbus_init_mmio(sd, &s->iomem);
}

static void imx7_analog_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX7AnalogState *s = IMX7_ANALOG(obj);

    memory_region_init(&s->mmio.container, obj, TYPE_IMX7_ANALOG,
                       0x10000);

    memory_region_init_io(&s->mmio.analog,
                          obj,
                          &imx7_set_clr_tog_ops,
                          s->analog,
                          TYPE_IMX7_ANALOG,
                          sizeof(s->analog));

    memory_region_add_subregion(&s->mmio.container,
                                0x60, &s->mmio.analog);

    memory_region_init_io(&s->mmio.pmu,
                          obj,
                          &imx7_set_clr_tog_ops,
                          s->pmu,
                          TYPE_IMX7_ANALOG ".pmu",
                          sizeof(s->pmu));

    memory_region_add_subregion(&s->mmio.container,
                                0x200, &s->mmio.pmu);

    memory_region_init_io(&s->mmio.digprog,
                          obj,
                          &imx7_digprog_ops,
                          &s->analog[ANALOG_DIGPROG],
                          TYPE_IMX7_ANALOG ".digprog",
                          sizeof(uint32_t));

    memory_region_add_subregion_overlap(&s->mmio.container,
                                        0x800, &s->mmio.digprog, 10);


    sysbus_init_mmio(sd, &s->mmio.container);
}

static const VMStateDescription vmstate_imx7_ccm = {
    .name = TYPE_IMX7_CCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ccm, IMX7CCMState, CCM_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t imx7_ccm_get_clock_frequency(IMXCCMState *dev, IMXClk clock)
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
                      TYPE_IMX7_CCM, __func__, clock);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: unsupported clock %d\n",
                      TYPE_IMX7_CCM, __func__, clock);
        break;
    }

    trace_ccm_clock_freq(clock, freq);

    return freq;
}

static void imx7_ccm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IMXCCMClass *ccm = IMX_CCM_CLASS(klass);

    device_class_set_legacy_reset(dc, imx7_ccm_reset);
    dc->vmsd  = &vmstate_imx7_ccm;
    dc->desc  = "i.MX7 Clock Control Module";

    ccm->get_clock_frequency = imx7_ccm_get_clock_frequency;
}

static const TypeInfo imx7_ccm_info = {
    .name          = TYPE_IMX7_CCM,
    .parent        = TYPE_IMX_CCM,
    .instance_size = sizeof(IMX7CCMState),
    .instance_init = imx7_ccm_init,
    .class_init    = imx7_ccm_class_init,
};

static const VMStateDescription vmstate_imx7_analog = {
    .name = TYPE_IMX7_ANALOG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(analog, IMX7AnalogState, ANALOG_MAX),
        VMSTATE_UINT32_ARRAY(pmu,    IMX7AnalogState, PMU_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static void imx7_analog_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, imx7_analog_reset);
    dc->vmsd  = &vmstate_imx7_analog;
    dc->desc  = "i.MX7 Analog Module";
}

static const TypeInfo imx7_analog_info = {
    .name          = TYPE_IMX7_ANALOG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX7AnalogState),
    .instance_init = imx7_analog_init,
    .class_init    = imx7_analog_class_init,
};

static void imx7_ccm_register_type(void)
{
    type_register_static(&imx7_ccm_info);
    type_register_static(&imx7_analog_info);
}
type_init(imx7_ccm_register_type)
