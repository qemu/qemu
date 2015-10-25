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
 * the CCM.
 */

#include "hw/misc/imx_ccm.h"

#define CKIH_FREQ 26000000 /* 26MHz crystal input */
#define CKIL_FREQ    32768 /* nominal 32khz clock */

#ifndef DEBUG_IMX_CCM
#define DEBUG_IMX_CCM 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_CCM) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_CCM, \
                                             __func__, ##args); \
        } \
    } while (0)

static int imx_ccm_post_load(void *opaque, int version_id);

static const VMStateDescription vmstate_imx_ccm = {
    .name = TYPE_IMX_CCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ccmr, IMXCCMState),
        VMSTATE_UINT32(pdr0, IMXCCMState),
        VMSTATE_UINT32(pdr1, IMXCCMState),
        VMSTATE_UINT32(mpctl, IMXCCMState),
        VMSTATE_UINT32(spctl, IMXCCMState),
        VMSTATE_UINT32_ARRAY(cgr, IMXCCMState, 3),
        VMSTATE_UINT32(pmcr0, IMXCCMState),
        VMSTATE_UINT32(pmcr1, IMXCCMState),
        VMSTATE_UINT32(pll_refclk_freq, IMXCCMState),
        VMSTATE_END_OF_LIST()
    },
    .post_load = imx_ccm_post_load,
};

uint32_t imx_clock_frequency(DeviceState *dev, IMXClk clock)
{
    IMXCCMState *s = IMX_CCM(dev);

    switch (clock) {
    case NOCLK:
        return 0;
    case MCU:
        return s->mcu_clk_freq;
    case HSP:
        return s->hsp_clk_freq;
    case IPG:
        return s->ipg_clk_freq;
    case CLK_32k:
        return CKIL_FREQ;
    }
    return 0;
}

/*
 * Calculate PLL output frequency
 */
static uint32_t calc_pll(uint32_t pllreg, uint32_t base_freq)
{
    int32_t mfn = MFN(pllreg);  /* Numerator */
    uint32_t mfi = MFI(pllreg); /* Integer part */
    uint32_t mfd = 1 + MFD(pllreg); /* Denominator */
    uint32_t pd = 1 + PD(pllreg);   /* Pre-divider */

    if (mfi < 5) {
        mfi = 5;
    }
    /* mfn is 10-bit signed twos-complement */
    mfn <<= 32 - 10;
    mfn >>= 32 - 10;

    return ((2 * (base_freq >> 10) * (mfi * mfd + mfn)) /
            (mfd * pd)) << 10;
}

static void update_clocks(IMXCCMState *s)
{
    /*
     * If we ever emulate more clocks, this should switch to a data-driven
     * approach
     */

    if ((s->ccmr & CCMR_PRCS) == 2) {
        s->pll_refclk_freq = CKIL_FREQ * 1024;
    } else {
        s->pll_refclk_freq = CKIH_FREQ;
    }

    /* ipg_clk_arm aka MCU clock */
    if ((s->ccmr & CCMR_MDS) || !(s->ccmr & CCMR_MPE)) {
        s->mcu_clk_freq = s->pll_refclk_freq;
    } else {
        s->mcu_clk_freq = calc_pll(s->mpctl, s->pll_refclk_freq);
    }

    /* High-speed clock */
    s->hsp_clk_freq = s->mcu_clk_freq / (1 + EXTRACT(s->pdr0, HSP));
    s->ipg_clk_freq = s->hsp_clk_freq / (1 + EXTRACT(s->pdr0, IPG));

    DPRINTF("mcu %uMHz, HSP %uMHz, IPG %uHz\n",
            s->mcu_clk_freq / 1000000,
            s->hsp_clk_freq / 1000000,
            s->ipg_clk_freq);
}

static void imx_ccm_reset(DeviceState *dev)
{
    IMXCCMState *s = IMX_CCM(dev);

    s->ccmr = 0x074b0b7b;
    s->pdr0 = 0xff870b48;
    s->pdr1 = 0x49fcfe7f;
    s->mpctl = PLL_PD(1) | PLL_MFD(0) | PLL_MFI(6) | PLL_MFN(0);
    s->cgr[0] = s->cgr[1] = s->cgr[2] = 0xffffffff;
    s->spctl = PLL_PD(1) | PLL_MFD(4) | PLL_MFI(0xc) | PLL_MFN(1);
    s->pmcr0 = 0x80209828;

    update_clocks(s);
}

static uint64_t imx_ccm_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    IMXCCMState *s = (IMXCCMState *)opaque;

    DPRINTF("(offset=0x%" HWADDR_PRIx ")\n", offset);

    switch (offset >> 2) {
    case 0: /* CCMR */
        DPRINTF(" ccmr = 0x%x\n", s->ccmr);
        return s->ccmr;
    case 1:
        DPRINTF(" pdr0 = 0x%x\n", s->pdr0);
        return s->pdr0;
    case 2:
        DPRINTF(" pdr1 = 0x%x\n", s->pdr1);
        return s->pdr1;
    case 4:
        DPRINTF(" mpctl = 0x%x\n", s->mpctl);
        return s->mpctl;
    case 6:
        DPRINTF(" spctl = 0x%x\n", s->spctl);
        return s->spctl;
    case 8:
        DPRINTF(" cgr0 = 0x%x\n", s->cgr[0]);
        return s->cgr[0];
    case 9:
        DPRINTF(" cgr1 = 0x%x\n", s->cgr[1]);
        return s->cgr[1];
    case 10:
        DPRINTF(" cgr2 = 0x%x\n", s->cgr[2]);
        return s->cgr[2];
    case 18: /* LTR1 */
        return 0x00004040;
    case 23:
        DPRINTF(" pcmr0 = 0x%x\n", s->pmcr0);
        return s->pmcr0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_CCM, __func__, offset);
        return 0;
    }
}

static void imx_ccm_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    IMXCCMState *s = (IMXCCMState *)opaque;

    DPRINTF("(offset=0x%" HWADDR_PRIx ", value = 0x%x)\n",
            offset, (unsigned int)value);

    switch (offset >> 2) {
    case 0:
        s->ccmr = CCMR_FPMF | (value & 0x3b6fdfff);
        break;
    case 1:
        s->pdr0 = value & 0xff9f3fff;
        break;
    case 2:
        s->pdr1 = value;
        break;
    case 4:
        s->mpctl = value & 0xbfff3fff;
        break;
    case 6:
        s->spctl = value & 0xbfff3fff;
        break;
    case 8:
        s->cgr[0] = value;
        return;
    case 9:
        s->cgr[1] = value;
        return;
    case 10:
        s->cgr[2] = value;
        return;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_CCM, __func__, offset);
        return;
    }
    update_clocks(s);
}

static const struct MemoryRegionOps imx_ccm_ops = {
    .read = imx_ccm_read,
    .write = imx_ccm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int imx_ccm_init(SysBusDevice *dev)
{
    IMXCCMState *s = IMX_CCM(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx_ccm_ops, s,
                          TYPE_IMX_CCM, 0x1000);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static int imx_ccm_post_load(void *opaque, int version_id)
{
    IMXCCMState *s = (IMXCCMState *)opaque;

    update_clocks(s);
    return 0;
}

static void imx_ccm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    sbc->init = imx_ccm_init;
    dc->reset = imx_ccm_reset;
    dc->vmsd = &vmstate_imx_ccm;
    dc->desc = "i.MX Clock Control Module";
}

static const TypeInfo imx_ccm_info = {
    .name = TYPE_IMX_CCM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXCCMState),
    .class_init = imx_ccm_class_init,
};

static void imx_ccm_register_types(void)
{
    type_register_static(&imx_ccm_info);
}

type_init(imx_ccm_register_types)
