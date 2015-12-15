/*
 * IMX31 Clock Control Module
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * To get the timer frequencies right, we need to emulate at least part of
 * the i.MX31 CCM.
 */

#include "qemu/osdep.h"
#include "hw/misc/imx31_ccm.h"
#include "qemu/log.h"

#define CKIH_FREQ 26000000 /* 26MHz crystal input */

#ifndef DEBUG_IMX31_CCM
#define DEBUG_IMX31_CCM 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX31_CCM) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX31_CCM, \
                                             __func__, ##args); \
        } \
    } while (0)

static char const *imx31_ccm_reg_name(uint32_t reg)
{
    static char unknown[20];

    switch (reg) {
    case IMX31_CCM_CCMR_REG:
        return "CCMR";
    case IMX31_CCM_PDR0_REG:
        return "PDR0";
    case IMX31_CCM_PDR1_REG:
        return "PDR1";
    case IMX31_CCM_RCSR_REG:
        return "RCSR";
    case IMX31_CCM_MPCTL_REG:
        return "MPCTL";
    case IMX31_CCM_UPCTL_REG:
        return "UPCTL";
    case IMX31_CCM_SPCTL_REG:
        return "SPCTL";
    case IMX31_CCM_COSR_REG:
        return "COSR";
    case IMX31_CCM_CGR0_REG:
        return "CGR0";
    case IMX31_CCM_CGR1_REG:
        return "CGR1";
    case IMX31_CCM_CGR2_REG:
        return "CGR2";
    case IMX31_CCM_WIMR_REG:
        return "WIMR";
    case IMX31_CCM_LDC_REG:
        return "LDC";
    case IMX31_CCM_DCVR0_REG:
        return "DCVR0";
    case IMX31_CCM_DCVR1_REG:
        return "DCVR1";
    case IMX31_CCM_DCVR2_REG:
        return "DCVR2";
    case IMX31_CCM_DCVR3_REG:
        return "DCVR3";
    case IMX31_CCM_LTR0_REG:
        return "LTR0";
    case IMX31_CCM_LTR1_REG:
        return "LTR1";
    case IMX31_CCM_LTR2_REG:
        return "LTR2";
    case IMX31_CCM_LTR3_REG:
        return "LTR3";
    case IMX31_CCM_LTBR0_REG:
        return "LTBR0";
    case IMX31_CCM_LTBR1_REG:
        return "LTBR1";
    case IMX31_CCM_PMCR0_REG:
        return "PMCR0";
    case IMX31_CCM_PMCR1_REG:
        return "PMCR1";
    case IMX31_CCM_PDR2_REG:
        return "PDR2";
    default:
        sprintf(unknown, "[%d ?]", reg);
        return unknown;
    }
}

static const VMStateDescription vmstate_imx31_ccm = {
    .name = TYPE_IMX31_CCM,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, IMX31CCMState, IMX31_CCM_MAX_REG),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t imx31_ccm_get_pll_ref_clk(IMXCCMState *dev)
{
    uint32_t freq = 0;
    IMX31CCMState *s = IMX31_CCM(dev);

    if ((s->reg[IMX31_CCM_CCMR_REG] & CCMR_PRCS) == 2) {
        if (s->reg[IMX31_CCM_CCMR_REG] & CCMR_FPME) {
            freq = CKIL_FREQ;
            if (s->reg[IMX31_CCM_CCMR_REG] & CCMR_FPMF) {
                freq *= 1024;
            }
        } 
    } else {
        freq = CKIH_FREQ;
    }

    DPRINTF("freq = %d\n", freq);

    return freq;
}

static uint32_t imx31_ccm_get_mpll_clk(IMXCCMState *dev)
{
    uint32_t freq;
    IMX31CCMState *s = IMX31_CCM(dev);

    freq = imx_ccm_calc_pll(s->reg[IMX31_CCM_MPCTL_REG],
                            imx31_ccm_get_pll_ref_clk(dev));

    DPRINTF("freq = %d\n", freq);

    return freq;
}

static uint32_t imx31_ccm_get_mcu_main_clk(IMXCCMState *dev)
{
    uint32_t freq;
    IMX31CCMState *s = IMX31_CCM(dev);

    if ((s->reg[IMX31_CCM_CCMR_REG] & CCMR_MDS) ||
        !(s->reg[IMX31_CCM_CCMR_REG] & CCMR_MPE)) {
        freq = imx31_ccm_get_pll_ref_clk(dev);
    } else {
        freq = imx31_ccm_get_mpll_clk(dev);
    }

    DPRINTF("freq = %d\n", freq);

    return freq;
}

static uint32_t imx31_ccm_get_hclk_clk(IMXCCMState *dev)
{
    uint32_t freq;
    IMX31CCMState *s = IMX31_CCM(dev);

    freq = imx31_ccm_get_mcu_main_clk(dev)
           / (1 + EXTRACT(s->reg[IMX31_CCM_PDR0_REG], MAX));

    DPRINTF("freq = %d\n", freq);

    return freq;
}

static uint32_t imx31_ccm_get_ipg_clk(IMXCCMState *dev)
{
    uint32_t freq;
    IMX31CCMState *s = IMX31_CCM(dev);

    freq = imx31_ccm_get_hclk_clk(dev)
           / (1 + EXTRACT(s->reg[IMX31_CCM_PDR0_REG], IPG));

    DPRINTF("freq = %d\n", freq);

    return freq;
}

static uint32_t imx31_ccm_get_clock_frequency(IMXCCMState *dev, IMXClk clock)
{
    uint32_t freq = 0;

    switch (clock) {
    case CLK_NONE:
        break;
    case CLK_IPG:
    case CLK_IPG_HIGH:
        freq = imx31_ccm_get_ipg_clk(dev);
        break;
    case CLK_32k:
        freq = CKIL_FREQ;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: unsupported clock %d\n",
                      TYPE_IMX31_CCM, __func__, clock);
        break;
    }

    DPRINTF("Clock = %d) = %d\n", clock, freq);

    return freq;
}

static void imx31_ccm_reset(DeviceState *dev)
{
    IMX31CCMState *s = IMX31_CCM(dev);

    DPRINTF("()\n");

    memset(s->reg, 0, sizeof(uint32_t) * IMX31_CCM_MAX_REG);

    s->reg[IMX31_CCM_CCMR_REG]   = 0x074b0b7d;
    s->reg[IMX31_CCM_PDR0_REG]   = 0xff870b48;
    s->reg[IMX31_CCM_PDR1_REG]   = 0x49fcfe7f;
    s->reg[IMX31_CCM_RCSR_REG]   = 0x007f0000;
    s->reg[IMX31_CCM_MPCTL_REG]  = 0x04001800;
    s->reg[IMX31_CCM_UPCTL_REG]  = 0x04051c03;
    s->reg[IMX31_CCM_SPCTL_REG]  = 0x04043001;
    s->reg[IMX31_CCM_COSR_REG]   = 0x00000280;
    s->reg[IMX31_CCM_CGR0_REG]   = 0xffffffff;
    s->reg[IMX31_CCM_CGR1_REG]   = 0xffffffff;
    s->reg[IMX31_CCM_CGR2_REG]   = 0xffffffff;
    s->reg[IMX31_CCM_WIMR_REG]   = 0xffffffff;
    s->reg[IMX31_CCM_LTR1_REG]   = 0x00004040;
    s->reg[IMX31_CCM_PMCR0_REG]  = 0x80209828;
    s->reg[IMX31_CCM_PMCR1_REG]  = 0x00aa0000;
    s->reg[IMX31_CCM_PDR2_REG]   = 0x00000285;
}

static uint64_t imx31_ccm_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    IMX31CCMState *s = (IMX31CCMState *)opaque;

    if ((offset >> 2) < IMX31_CCM_MAX_REG) {
        value = s->reg[offset >> 2];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX31_CCM, __func__, offset);
    }

    DPRINTF("reg[%s] => 0x%" PRIx32 "\n", imx31_ccm_reg_name(offset >> 2),
            value);

    return (uint64_t)value;
}

static void imx31_ccm_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    IMX31CCMState *s = (IMX31CCMState *)opaque;

    DPRINTF("reg[%s] <= 0x%" PRIx32 "\n", imx31_ccm_reg_name(offset >> 2),
            (uint32_t)value);

    switch (offset >> 2) {
    case IMX31_CCM_CCMR_REG:
        s->reg[IMX31_CCM_CCMR_REG] = CCMR_FPMF | (value & 0x3b6fdfff);
        break;
    case IMX31_CCM_PDR0_REG:
        s->reg[IMX31_CCM_PDR0_REG] = value & 0xff9f3fff;
        break;
    case IMX31_CCM_PDR1_REG:
        s->reg[IMX31_CCM_PDR1_REG] = value;
        break;
    case IMX31_CCM_MPCTL_REG:
        s->reg[IMX31_CCM_MPCTL_REG] = value & 0xbfff3fff;
        break;
    case IMX31_CCM_SPCTL_REG:
        s->reg[IMX31_CCM_SPCTL_REG] = value & 0xbfff3fff;
        break;
    case IMX31_CCM_CGR0_REG:
        s->reg[IMX31_CCM_CGR0_REG] = value;
        break;
    case IMX31_CCM_CGR1_REG:
        s->reg[IMX31_CCM_CGR1_REG] = value;
        break;
    case IMX31_CCM_CGR2_REG:
        s->reg[IMX31_CCM_CGR2_REG] = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX31_CCM, __func__, offset);
        break;
    }
}

static const struct MemoryRegionOps imx31_ccm_ops = {
    .read = imx31_ccm_read,
    .write = imx31_ccm_write,
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

static void imx31_ccm_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX31CCMState *s = IMX31_CCM(obj);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx31_ccm_ops, s,
                          TYPE_IMX31_CCM, 0x1000);
    sysbus_init_mmio(sd, &s->iomem);
}

static void imx31_ccm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc  = DEVICE_CLASS(klass);
    IMXCCMClass *ccm = IMX_CCM_CLASS(klass);

    dc->reset = imx31_ccm_reset;
    dc->vmsd  = &vmstate_imx31_ccm;
    dc->desc  = "i.MX31 Clock Control Module";

    ccm->get_clock_frequency = imx31_ccm_get_clock_frequency;
}

static const TypeInfo imx31_ccm_info = {
    .name          = TYPE_IMX31_CCM,
    .parent        = TYPE_IMX_CCM,
    .instance_size = sizeof(IMX31CCMState),
    .instance_init = imx31_ccm_init,
    .class_init    = imx31_ccm_class_init,
};

static void imx31_ccm_register_types(void)
{
    type_register_static(&imx31_ccm_info);
}

type_init(imx31_ccm_register_types)
