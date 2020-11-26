/*
 * IMX25 Clock Control Module
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * To get the timer frequencies right, we need to emulate at least part of
 * the CCM.
 */

#include "qemu/osdep.h"
#include "hw/misc/imx25_ccm.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef DEBUG_IMX25_CCM
#define DEBUG_IMX25_CCM 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX25_CCM) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX25_CCM, \
                                             __func__, ##args); \
        } \
    } while (0)

static const char *imx25_ccm_reg_name(uint32_t reg)
{
    static char unknown[20];

    switch (reg) {
    case IMX25_CCM_MPCTL_REG:
        return "mpctl";
    case IMX25_CCM_UPCTL_REG:
        return "upctl";
    case IMX25_CCM_CCTL_REG:
        return "cctl";
    case IMX25_CCM_CGCR0_REG:
        return "cgcr0";
    case IMX25_CCM_CGCR1_REG:
        return "cgcr1";
    case IMX25_CCM_CGCR2_REG:
        return "cgcr2";
    case IMX25_CCM_PCDR0_REG:
        return "pcdr0";
    case IMX25_CCM_PCDR1_REG:
        return "pcdr1";
    case IMX25_CCM_PCDR2_REG:
        return "pcdr2";
    case IMX25_CCM_PCDR3_REG:
        return "pcdr3";
    case IMX25_CCM_RCSR_REG:
        return "rcsr";
    case IMX25_CCM_CRDR_REG:
        return "crdr";
    case IMX25_CCM_DCVR0_REG:
        return "dcvr0";
    case IMX25_CCM_DCVR1_REG:
        return "dcvr1";
    case IMX25_CCM_DCVR2_REG:
        return "dcvr2";
    case IMX25_CCM_DCVR3_REG:
        return "dcvr3";
    case IMX25_CCM_LTR0_REG:
        return "ltr0";
    case IMX25_CCM_LTR1_REG:
        return "ltr1";
    case IMX25_CCM_LTR2_REG:
        return "ltr2";
    case IMX25_CCM_LTR3_REG:
        return "ltr3";
    case IMX25_CCM_LTBR0_REG:
        return "ltbr0";
    case IMX25_CCM_LTBR1_REG:
        return "ltbr1";
    case IMX25_CCM_PMCR0_REG:
        return "pmcr0";
    case IMX25_CCM_PMCR1_REG:
        return "pmcr1";
    case IMX25_CCM_PMCR2_REG:
        return "pmcr2";
    case IMX25_CCM_MCR_REG:
        return "mcr";
    case IMX25_CCM_LPIMR0_REG:
        return "lpimr0";
    case IMX25_CCM_LPIMR1_REG:
        return "lpimr1";
    default:
        sprintf(unknown, "[%u ?]", reg);
        return unknown;
    }
}
#define CKIH_FREQ 24000000 /* 24MHz crystal input */

static const VMStateDescription vmstate_imx25_ccm = {
    .name = TYPE_IMX25_CCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, IMX25CCMState, IMX25_CCM_MAX_REG),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t imx25_ccm_get_mpll_clk(IMXCCMState *dev)
{
    uint32_t freq;
    IMX25CCMState *s = IMX25_CCM(dev);

    if (EXTRACT(s->reg[IMX25_CCM_CCTL_REG], MPLL_BYPASS)) {
        freq = CKIH_FREQ;
    } else {
        freq = imx_ccm_calc_pll(s->reg[IMX25_CCM_MPCTL_REG], CKIH_FREQ);
    }

    DPRINTF("freq = %u\n", freq);

    return freq;
}

static uint32_t imx25_ccm_get_mcu_clk(IMXCCMState *dev)
{
    uint32_t freq;
    IMX25CCMState *s = IMX25_CCM(dev);

    freq = imx25_ccm_get_mpll_clk(dev);

    if (EXTRACT(s->reg[IMX25_CCM_CCTL_REG], ARM_SRC)) {
        freq = (freq * 3 / 4);
    }

    freq = freq / (1 + EXTRACT(s->reg[IMX25_CCM_CCTL_REG], ARM_CLK_DIV));

    DPRINTF("freq = %u\n", freq);

    return freq;
}

static uint32_t imx25_ccm_get_ahb_clk(IMXCCMState *dev)
{
    uint32_t freq;
    IMX25CCMState *s = IMX25_CCM(dev);

    freq = imx25_ccm_get_mcu_clk(dev)
           / (1 + EXTRACT(s->reg[IMX25_CCM_CCTL_REG], AHB_CLK_DIV));

    DPRINTF("freq = %u\n", freq);

    return freq;
}

static uint32_t imx25_ccm_get_ipg_clk(IMXCCMState *dev)
{
    uint32_t freq;

    freq = imx25_ccm_get_ahb_clk(dev) / 2;

    DPRINTF("freq = %u\n", freq);

    return freq;
}

static uint32_t imx25_ccm_get_clock_frequency(IMXCCMState *dev, IMXClk clock)
{
    uint32_t freq = 0;
    DPRINTF("Clock = %d)\n", clock);

    switch (clock) {
    case CLK_NONE:
        break;
    case CLK_IPG:
    case CLK_IPG_HIGH:
        freq = imx25_ccm_get_ipg_clk(dev);
        break;
    case CLK_32k:
        freq = CKIL_FREQ;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: unsupported clock %d\n",
                      TYPE_IMX25_CCM, __func__, clock);
        break;
    }

    DPRINTF("Clock = %d) = %u\n", clock, freq);

    return freq;
}

static void imx25_ccm_reset(DeviceState *dev)
{
    IMX25CCMState *s = IMX25_CCM(dev);

    DPRINTF("\n");

    memset(s->reg, 0, IMX25_CCM_MAX_REG * sizeof(uint32_t));
    s->reg[IMX25_CCM_MPCTL_REG] = 0x800b2c01;
    s->reg[IMX25_CCM_UPCTL_REG] = 0x84042800;
    /* 
     * The value below gives:
     * CPU = 133 MHz, AHB = 66,5 MHz, IPG = 33 MHz. 
     */
    s->reg[IMX25_CCM_CCTL_REG]  = 0xd0030000;
    s->reg[IMX25_CCM_CGCR0_REG] = 0x028A0100;
    s->reg[IMX25_CCM_CGCR1_REG] = 0x04008100;
    s->reg[IMX25_CCM_CGCR2_REG] = 0x00000438;
    s->reg[IMX25_CCM_PCDR0_REG] = 0x01010101;
    s->reg[IMX25_CCM_PCDR1_REG] = 0x01010101;
    s->reg[IMX25_CCM_PCDR2_REG] = 0x01010101;
    s->reg[IMX25_CCM_PCDR3_REG] = 0x01010101;
    s->reg[IMX25_CCM_PMCR0_REG] = 0x00A00000;
    s->reg[IMX25_CCM_PMCR1_REG] = 0x0000A030;
    s->reg[IMX25_CCM_PMCR2_REG] = 0x0000A030;
    s->reg[IMX25_CCM_MCR_REG]   = 0x43000000;

    /*
     * default boot will change the reset values to allow:
     * CPU = 399 MHz, AHB = 133 MHz, IPG = 66,5 MHz. 
     * For some reason, this doesn't work. With the value below, linux
     * detects a 88 MHz IPG CLK instead of 66,5 MHz.
    s->reg[IMX25_CCM_CCTL_REG]  = 0x20032000;
     */
}

static uint64_t imx25_ccm_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    IMX25CCMState *s = (IMX25CCMState *)opaque;

    if (offset < 0x70) {
        value = s->reg[offset >> 2];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX25_CCM, __func__, offset);
    }

    DPRINTF("reg[%s] => 0x%" PRIx32 "\n", imx25_ccm_reg_name(offset >> 2),
            value);

    return value;
}

static void imx25_ccm_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    IMX25CCMState *s = (IMX25CCMState *)opaque;

    DPRINTF("reg[%s] <= 0x%" PRIx32 "\n", imx25_ccm_reg_name(offset >> 2),
            (uint32_t)value);

    if (offset < 0x70) {
        /*
         * We will do a better implementation later. In particular some bits
         * cannot be written to.
         */
        s->reg[offset >> 2] = value;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX25_CCM, __func__, offset);
    }
}

static const struct MemoryRegionOps imx25_ccm_ops = {
    .read = imx25_ccm_read,
    .write = imx25_ccm_write,
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

static void imx25_ccm_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX25CCMState *s = IMX25_CCM(obj);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx25_ccm_ops, s,
                          TYPE_IMX25_CCM, 0x1000);
    sysbus_init_mmio(sd, &s->iomem);
}

static void imx25_ccm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IMXCCMClass *ccm = IMX_CCM_CLASS(klass);

    dc->reset = imx25_ccm_reset;
    dc->vmsd = &vmstate_imx25_ccm;
    dc->desc = "i.MX25 Clock Control Module";

    ccm->get_clock_frequency = imx25_ccm_get_clock_frequency;
}

static const TypeInfo imx25_ccm_info = {
    .name          = TYPE_IMX25_CCM,
    .parent        = TYPE_IMX_CCM,
    .instance_size = sizeof(IMX25CCMState),
    .instance_init = imx25_ccm_init,
    .class_init    = imx25_ccm_class_init,
};

static void imx25_ccm_register_types(void)
{
    type_register_static(&imx25_ccm_info);
}

type_init(imx25_ccm_register_types)
