/*
 * IMX6UL Clock Control Module
 *
 * Copyright (c) 2018 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * To get the timer frequencies right, we need to emulate at least part of
 * the CCM.
 */

#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "hw/misc/imx6ul_ccm.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "trace.h"

static const uint32_t ccm_mask[CCM_MAX] = {
    [CCM_CCR] = 0xf01fef80,
    [CCM_CCDR] = 0xfffeffff,
    [CCM_CSR] = 0xffffffff,
    [CCM_CCSR] = 0xfffffef2,
    [CCM_CACRR] = 0xfffffff8,
    [CCM_CBCDR] = 0xc1f8e000,
    [CCM_CBCMR] = 0xfc03cfff,
    [CCM_CSCMR1] = 0x80700000,
    [CCM_CSCMR2] = 0xe01ff003,
    [CCM_CSCDR1] = 0xfe00c780,
    [CCM_CS1CDR] = 0xfe00fe00,
    [CCM_CS2CDR] = 0xf8007000,
    [CCM_CDCDR] = 0xf00fffff,
    [CCM_CHSCCDR] = 0xfffc01ff,
    [CCM_CSCDR2] = 0xfe0001ff,
    [CCM_CSCDR3] = 0xffffc1ff,
    [CCM_CDHIPR] = 0xffffffff,
    [CCM_CTOR] = 0x00000000,
    [CCM_CLPCR] = 0xf39ff01c,
    [CCM_CISR] = 0xfb85ffbe,
    [CCM_CIMR] = 0xfb85ffbf,
    [CCM_CCOSR] = 0xfe00fe00,
    [CCM_CGPR] = 0xfffc3fea,
    [CCM_CCGR0] = 0x00000000,
    [CCM_CCGR1] = 0x00000000,
    [CCM_CCGR2] = 0x00000000,
    [CCM_CCGR3] = 0x00000000,
    [CCM_CCGR4] = 0x00000000,
    [CCM_CCGR5] = 0x00000000,
    [CCM_CCGR6] = 0x00000000,
    [CCM_CMEOR] = 0xafffff1f,
};

static const uint32_t analog_mask[CCM_ANALOG_MAX] = {
    [CCM_ANALOG_PLL_ARM] = 0xfff60f80,
    [CCM_ANALOG_PLL_USB1] = 0xfffe0fbc,
    [CCM_ANALOG_PLL_USB2] = 0xfffe0fbc,
    [CCM_ANALOG_PLL_SYS] = 0xfffa0ffe,
    [CCM_ANALOG_PLL_SYS_SS] = 0x00000000,
    [CCM_ANALOG_PLL_SYS_NUM] = 0xc0000000,
    [CCM_ANALOG_PLL_SYS_DENOM] = 0xc0000000,
    [CCM_ANALOG_PLL_AUDIO] = 0xffe20f80,
    [CCM_ANALOG_PLL_AUDIO_NUM] = 0xc0000000,
    [CCM_ANALOG_PLL_AUDIO_DENOM] = 0xc0000000,
    [CCM_ANALOG_PLL_VIDEO] = 0xffe20f80,
    [CCM_ANALOG_PLL_VIDEO_NUM] = 0xc0000000,
    [CCM_ANALOG_PLL_VIDEO_DENOM] = 0xc0000000,
    [CCM_ANALOG_PLL_ENET] = 0xffc20ff0,
    [CCM_ANALOG_PFD_480] = 0x40404040,
    [CCM_ANALOG_PFD_528] = 0x40404040,
    [PMU_MISC0] = 0x01fe8306,
    [PMU_MISC1] = 0x07fcede0,
    [PMU_MISC2] = 0x005f5f5f,
};

static const char *imx6ul_ccm_reg_name(uint32_t reg)
{
    static char unknown[20];

    switch (reg) {
    case CCM_CCR:
        return "CCR";
    case CCM_CCDR:
        return "CCDR";
    case CCM_CSR:
        return "CSR";
    case CCM_CCSR:
        return "CCSR";
    case CCM_CACRR:
        return "CACRR";
    case CCM_CBCDR:
        return "CBCDR";
    case CCM_CBCMR:
        return "CBCMR";
    case CCM_CSCMR1:
        return "CSCMR1";
    case CCM_CSCMR2:
        return "CSCMR2";
    case CCM_CSCDR1:
        return "CSCDR1";
    case CCM_CS1CDR:
        return "CS1CDR";
    case CCM_CS2CDR:
        return "CS2CDR";
    case CCM_CDCDR:
        return "CDCDR";
    case CCM_CHSCCDR:
        return "CHSCCDR";
    case CCM_CSCDR2:
        return "CSCDR2";
    case CCM_CSCDR3:
        return "CSCDR3";
    case CCM_CDHIPR:
        return "CDHIPR";
    case CCM_CTOR:
        return "CTOR";
    case CCM_CLPCR:
        return "CLPCR";
    case CCM_CISR:
        return "CISR";
    case CCM_CIMR:
        return "CIMR";
    case CCM_CCOSR:
        return "CCOSR";
    case CCM_CGPR:
        return "CGPR";
    case CCM_CCGR0:
        return "CCGR0";
    case CCM_CCGR1:
        return "CCGR1";
    case CCM_CCGR2:
        return "CCGR2";
    case CCM_CCGR3:
        return "CCGR3";
    case CCM_CCGR4:
        return "CCGR4";
    case CCM_CCGR5:
        return "CCGR5";
    case CCM_CCGR6:
        return "CCGR6";
    case CCM_CMEOR:
        return "CMEOR";
    default:
        snprintf(unknown, sizeof(unknown), "%u ?", reg);
        return unknown;
    }
}

static const char *imx6ul_analog_reg_name(uint32_t reg)
{
    static char unknown[20];

    switch (reg) {
    case CCM_ANALOG_PLL_ARM:
        return "PLL_ARM";
    case CCM_ANALOG_PLL_ARM_SET:
        return "PLL_ARM_SET";
    case CCM_ANALOG_PLL_ARM_CLR:
        return "PLL_ARM_CLR";
    case CCM_ANALOG_PLL_ARM_TOG:
        return "PLL_ARM_TOG";
    case CCM_ANALOG_PLL_USB1:
        return "PLL_USB1";
    case CCM_ANALOG_PLL_USB1_SET:
        return "PLL_USB1_SET";
    case CCM_ANALOG_PLL_USB1_CLR:
        return "PLL_USB1_CLR";
    case CCM_ANALOG_PLL_USB1_TOG:
        return "PLL_USB1_TOG";
    case CCM_ANALOG_PLL_USB2:
        return "PLL_USB2";
    case CCM_ANALOG_PLL_USB2_SET:
        return "PLL_USB2_SET";
    case CCM_ANALOG_PLL_USB2_CLR:
        return "PLL_USB2_CLR";
    case CCM_ANALOG_PLL_USB2_TOG:
        return "PLL_USB2_TOG";
    case CCM_ANALOG_PLL_SYS:
        return "PLL_SYS";
    case CCM_ANALOG_PLL_SYS_SET:
        return "PLL_SYS_SET";
    case CCM_ANALOG_PLL_SYS_CLR:
        return "PLL_SYS_CLR";
    case CCM_ANALOG_PLL_SYS_TOG:
        return "PLL_SYS_TOG";
    case CCM_ANALOG_PLL_SYS_SS:
        return "PLL_SYS_SS";
    case CCM_ANALOG_PLL_SYS_NUM:
        return "PLL_SYS_NUM";
    case CCM_ANALOG_PLL_SYS_DENOM:
        return "PLL_SYS_DENOM";
    case CCM_ANALOG_PLL_AUDIO:
        return "PLL_AUDIO";
    case CCM_ANALOG_PLL_AUDIO_SET:
        return "PLL_AUDIO_SET";
    case CCM_ANALOG_PLL_AUDIO_CLR:
        return "PLL_AUDIO_CLR";
    case CCM_ANALOG_PLL_AUDIO_TOG:
        return "PLL_AUDIO_TOG";
    case CCM_ANALOG_PLL_AUDIO_NUM:
        return "PLL_AUDIO_NUM";
    case CCM_ANALOG_PLL_AUDIO_DENOM:
        return "PLL_AUDIO_DENOM";
    case CCM_ANALOG_PLL_VIDEO:
        return "PLL_VIDEO";
    case CCM_ANALOG_PLL_VIDEO_SET:
        return "PLL_VIDEO_SET";
    case CCM_ANALOG_PLL_VIDEO_CLR:
        return "PLL_VIDEO_CLR";
    case CCM_ANALOG_PLL_VIDEO_TOG:
        return "PLL_VIDEO_TOG";
    case CCM_ANALOG_PLL_VIDEO_NUM:
        return "PLL_VIDEO_NUM";
    case CCM_ANALOG_PLL_VIDEO_DENOM:
        return "PLL_VIDEO_DENOM";
    case CCM_ANALOG_PLL_ENET:
        return "PLL_ENET";
    case CCM_ANALOG_PLL_ENET_SET:
        return "PLL_ENET_SET";
    case CCM_ANALOG_PLL_ENET_CLR:
        return "PLL_ENET_CLR";
    case CCM_ANALOG_PLL_ENET_TOG:
        return "PLL_ENET_TOG";
    case CCM_ANALOG_PFD_480:
        return "PFD_480";
    case CCM_ANALOG_PFD_480_SET:
        return "PFD_480_SET";
    case CCM_ANALOG_PFD_480_CLR:
        return "PFD_480_CLR";
    case CCM_ANALOG_PFD_480_TOG:
        return "PFD_480_TOG";
    case CCM_ANALOG_PFD_528:
        return "PFD_528";
    case CCM_ANALOG_PFD_528_SET:
        return "PFD_528_SET";
    case CCM_ANALOG_PFD_528_CLR:
        return "PFD_528_CLR";
    case CCM_ANALOG_PFD_528_TOG:
        return "PFD_528_TOG";
    case CCM_ANALOG_MISC0:
        return "MISC0";
    case CCM_ANALOG_MISC0_SET:
        return "MISC0_SET";
    case CCM_ANALOG_MISC0_CLR:
        return "MISC0_CLR";
    case CCM_ANALOG_MISC0_TOG:
        return "MISC0_TOG";
    case CCM_ANALOG_MISC2:
        return "MISC2";
    case CCM_ANALOG_MISC2_SET:
        return "MISC2_SET";
    case CCM_ANALOG_MISC2_CLR:
        return "MISC2_CLR";
    case CCM_ANALOG_MISC2_TOG:
        return "MISC2_TOG";
    case PMU_REG_1P1:
        return "PMU_REG_1P1";
    case PMU_REG_3P0:
        return "PMU_REG_3P0";
    case PMU_REG_2P5:
        return "PMU_REG_2P5";
    case PMU_REG_CORE:
        return "PMU_REG_CORE";
    case PMU_MISC1:
        return "PMU_MISC1";
    case PMU_MISC1_SET:
        return "PMU_MISC1_SET";
    case PMU_MISC1_CLR:
        return "PMU_MISC1_CLR";
    case PMU_MISC1_TOG:
        return "PMU_MISC1_TOG";
    case USB_ANALOG_DIGPROG:
        return "USB_ANALOG_DIGPROG";
    default:
        snprintf(unknown, sizeof(unknown), "%u ?", reg);
        return unknown;
    }
}

#define CKIH_FREQ 24000000 /* 24MHz crystal input */

static const VMStateDescription vmstate_imx6ul_ccm = {
    .name = TYPE_IMX6UL_CCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ccm, IMX6ULCCMState, CCM_MAX),
        VMSTATE_UINT32_ARRAY(analog, IMX6ULCCMState, CCM_ANALOG_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static uint64_t imx6ul_analog_get_osc_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = CKIH_FREQ;

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_analog_get_pll2_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = imx6ul_analog_get_osc_clk(dev);

    if (FIELD_EX32(dev->analog[CCM_ANALOG_PLL_SYS],
                   ANALOG_PLL_SYS, DIV_SELECT)) {
        freq *= 22;
    } else {
        freq *= 20;
    }

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_analog_get_pll3_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = imx6ul_analog_get_osc_clk(dev) * 20;

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_analog_get_pll2_pfd0_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    freq = imx6ul_analog_get_pll2_clk(dev) * 18
           / FIELD_EX32(dev->analog[CCM_ANALOG_PFD_528],
                        ANALOG_PFD_528, PFD0_FRAC);

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_analog_get_pll2_pfd2_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    freq = imx6ul_analog_get_pll2_clk(dev) * 18
           / FIELD_EX32(dev->analog[CCM_ANALOG_PFD_528],
                        ANALOG_PFD_528, PFD2_FRAC);

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_analog_pll2_bypass_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_periph_clk2_sel_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    switch (FIELD_EX32(dev->ccm[CCM_CBCMR], CBCMR, PERIPH_CLK2_SEL)) {
    case 0:
        freq = imx6ul_analog_get_pll3_clk(dev);
        break;
    case 1:
        freq = imx6ul_analog_get_osc_clk(dev);
        break;
    case 2:
        freq = imx6ul_analog_pll2_bypass_clk(dev);
        break;
    case 3:
        /* We should never get there as 3 is a reserved value */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[%s]%s: unsupported PERIPH_CLK2_SEL value 3\n",
                      TYPE_IMX6UL_CCM, __func__);
        /* freq is set to 0 as we don't know what it should be */
        break;
    default:
        g_assert_not_reached();
    }

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_periph_clk_sel_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    switch (FIELD_EX32(dev->ccm[CCM_CBCMR], CBCMR, PRE_PERIPH_CLK_SEL)) {
    case 0:
        freq = imx6ul_analog_get_pll2_clk(dev);
        break;
    case 1:
        freq = imx6ul_analog_get_pll2_pfd2_clk(dev);
        break;
    case 2:
        freq = imx6ul_analog_get_pll2_pfd0_clk(dev);
        break;
    case 3:
        freq = imx6ul_analog_get_pll2_pfd2_clk(dev) / 2;
        break;
    default:
        g_assert_not_reached();
    }

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_periph_clk2_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    freq = imx6ul_ccm_get_periph_clk2_sel_clk(dev)
           / (1 + FIELD_EX32(dev->ccm[CCM_CBCDR], CBCDR, PERIPH_CLK2_PODF));

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_periph_sel_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    switch (FIELD_EX32(dev->ccm[CCM_CBCDR], CBCDR, PERIPH_CLK_SEL)) {
    case 0:
        freq = imx6ul_ccm_get_periph_clk_sel_clk(dev);
        break;
    case 1:
        freq = imx6ul_ccm_get_periph_clk2_clk(dev);
        break;
    default:
        g_assert_not_reached();
    }

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_ahb_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    freq = imx6ul_ccm_get_periph_sel_clk(dev)
           / (1 + FIELD_EX32(dev->ccm[CCM_CBCDR], CBCDR, AHB_PODF));

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_ipg_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    freq = imx6ul_ccm_get_ahb_clk(dev)
           / (1 + FIELD_EX32(dev->ccm[CCM_CBCDR], CBCDR, IPG_PODF));

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_per_sel_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    switch (FIELD_EX32(dev->ccm[CCM_CSCMR1], CSCMR1, PERCLK_CLK_SEL)) {
    case 0:
        freq = imx6ul_ccm_get_ipg_clk(dev);
        break;
    case 1:
        freq = imx6ul_analog_get_osc_clk(dev);
        break;
    default:
        g_assert_not_reached();
    }

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint64_t imx6ul_ccm_get_per_clk(IMX6ULCCMState *dev)
{
    uint64_t freq = 0;

    freq = imx6ul_ccm_get_per_sel_clk(dev)
           / (1 + FIELD_EX32(dev->ccm[CCM_CSCMR1], CSCMR1, PERCLK_PODF));

    trace_ccm_freq((uint32_t)freq);

    return freq;
}

static uint32_t imx6ul_ccm_get_clock_frequency(IMXCCMState *dev, IMXClk clock)
{
    uint32_t freq = 0;
    IMX6ULCCMState *s = IMX6UL_CCM(dev);

    switch (clock) {
    case CLK_NONE:
        break;
    case CLK_IPG:
        freq = imx6ul_ccm_get_ipg_clk(s);
        break;
    case CLK_IPG_HIGH:
        freq = imx6ul_ccm_get_per_clk(s);
        break;
    case CLK_32k:
        freq = CKIL_FREQ;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: unsupported clock %d\n",
                      TYPE_IMX6UL_CCM, __func__, clock);
        break;
    }

    trace_ccm_clock_freq(clock, freq);

    return freq;
}

static void imx6ul_ccm_reset(DeviceState *dev)
{
    IMX6ULCCMState *s = IMX6UL_CCM(dev);

    trace_ccm_entry();

    s->ccm[CCM_CCR] = 0x0401167F;
    s->ccm[CCM_CCDR] = 0x00000000;
    s->ccm[CCM_CSR] = 0x00000010;
    s->ccm[CCM_CCSR] = 0x00000100;
    s->ccm[CCM_CACRR] = 0x00000000;
    s->ccm[CCM_CBCDR] = 0x00018D00;
    s->ccm[CCM_CBCMR] = 0x24860324;
    s->ccm[CCM_CSCMR1] = 0x04900080;
    s->ccm[CCM_CSCMR2] = 0x03192F06;
    s->ccm[CCM_CSCDR1] = 0x00490B00;
    s->ccm[CCM_CS1CDR] = 0x0EC102C1;
    s->ccm[CCM_CS2CDR] = 0x000336C1;
    s->ccm[CCM_CDCDR] = 0x33F71F92;
    s->ccm[CCM_CHSCCDR] = 0x000248A4;
    s->ccm[CCM_CSCDR2] = 0x00029B48;
    s->ccm[CCM_CSCDR3] = 0x00014841;
    s->ccm[CCM_CDHIPR] = 0x00000000;
    s->ccm[CCM_CTOR] = 0x00000000;
    s->ccm[CCM_CLPCR] = 0x00000079;
    s->ccm[CCM_CISR] = 0x00000000;
    s->ccm[CCM_CIMR] = 0xFFFFFFFF;
    s->ccm[CCM_CCOSR] = 0x000A0001;
    s->ccm[CCM_CGPR] = 0x0000FE62;
    s->ccm[CCM_CCGR0] = 0xFFFFFFFF;
    s->ccm[CCM_CCGR1] = 0xFFFFFFFF;
    s->ccm[CCM_CCGR2] = 0xFC3FFFFF;
    s->ccm[CCM_CCGR3] = 0xFFFFFFFF;
    s->ccm[CCM_CCGR4] = 0xFFFFFFFF;
    s->ccm[CCM_CCGR5] = 0xFFFFFFFF;
    s->ccm[CCM_CCGR6] = 0xFFFFFFFF;
    s->ccm[CCM_CMEOR] = 0xFFFFFFFF;

    s->analog[CCM_ANALOG_PLL_ARM] = 0x00013063;
    s->analog[CCM_ANALOG_PLL_USB1] = 0x00012000;
    s->analog[CCM_ANALOG_PLL_USB2] = 0x00012000;
    s->analog[CCM_ANALOG_PLL_SYS] = 0x00013001;
    s->analog[CCM_ANALOG_PLL_SYS_SS] = 0x00000000;
    s->analog[CCM_ANALOG_PLL_SYS_NUM] = 0x00000000;
    s->analog[CCM_ANALOG_PLL_SYS_DENOM] = 0x00000012;
    s->analog[CCM_ANALOG_PLL_AUDIO] = 0x00011006;
    s->analog[CCM_ANALOG_PLL_AUDIO_NUM] = 0x05F5E100;
    s->analog[CCM_ANALOG_PLL_AUDIO_DENOM] = 0x2964619C;
    s->analog[CCM_ANALOG_PLL_VIDEO] = 0x0001100C;
    s->analog[CCM_ANALOG_PLL_VIDEO_NUM] = 0x05F5E100;
    s->analog[CCM_ANALOG_PLL_VIDEO_DENOM] = 0x10A24447;
    s->analog[CCM_ANALOG_PLL_ENET] = 0x00011001;
    s->analog[CCM_ANALOG_PFD_480] = 0x1311100C;
    s->analog[CCM_ANALOG_PFD_528] = 0x1018101B;

    s->analog[PMU_REG_1P1] = 0x00001073;
    s->analog[PMU_REG_3P0] = 0x00000F74;
    s->analog[PMU_REG_2P5] = 0x00001073;
    s->analog[PMU_REG_CORE] = 0x00482012;
    s->analog[PMU_MISC0] = 0x04000000;
    s->analog[PMU_MISC1] = 0x00000000;
    s->analog[PMU_MISC2] = 0x00272727;
    s->analog[PMU_LOWPWR_CTRL] = 0x00004009;

    s->analog[USB_ANALOG_USB1_VBUS_DETECT] = 0x01000004;
    s->analog[USB_ANALOG_USB1_CHRG_DETECT] = 0x00000000;
    s->analog[USB_ANALOG_USB1_VBUS_DETECT_STAT] = 0x00000000;
    s->analog[USB_ANALOG_USB1_CHRG_DETECT_STAT] = 0x00000000;
    s->analog[USB_ANALOG_USB1_MISC] = 0x00000002;
    s->analog[USB_ANALOG_USB2_VBUS_DETECT] = 0x01000004;
    s->analog[USB_ANALOG_USB2_CHRG_DETECT] = 0x00000000;
    s->analog[USB_ANALOG_USB2_MISC] = 0x00000002;
    s->analog[USB_ANALOG_DIGPROG] = 0x00640000;

    /* all PLLs need to be locked */
    s->analog[CCM_ANALOG_PLL_ARM]   |= CCM_ANALOG_PLL_LOCK;
    s->analog[CCM_ANALOG_PLL_USB1]  |= CCM_ANALOG_PLL_LOCK;
    s->analog[CCM_ANALOG_PLL_USB2]  |= CCM_ANALOG_PLL_LOCK;
    s->analog[CCM_ANALOG_PLL_SYS]   |= CCM_ANALOG_PLL_LOCK;
    s->analog[CCM_ANALOG_PLL_AUDIO] |= CCM_ANALOG_PLL_LOCK;
    s->analog[CCM_ANALOG_PLL_VIDEO] |= CCM_ANALOG_PLL_LOCK;
    s->analog[CCM_ANALOG_PLL_ENET]  |= CCM_ANALOG_PLL_LOCK;

    s->analog[TEMPMON_TEMPSENSE0] = 0x00000001;
    s->analog[TEMPMON_TEMPSENSE1] = 0x00000001;
    s->analog[TEMPMON_TEMPSENSE2] = 0x00000000;
}

static uint64_t imx6ul_ccm_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    uint32_t index = offset >> 2;
    IMX6ULCCMState *s = (IMX6ULCCMState *)opaque;

    assert(index < CCM_MAX);

    value = s->ccm[index];

    trace_ccm_read_reg(imx6ul_ccm_reg_name(index), (uint32_t)value);

    return (uint64_t)value;
}

static void imx6ul_ccm_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    uint32_t index = offset >> 2;
    IMX6ULCCMState *s = (IMX6ULCCMState *)opaque;

    assert(index < CCM_MAX);

    trace_ccm_write_reg(imx6ul_ccm_reg_name(index), (uint32_t)value);

    s->ccm[index] = (s->ccm[index] & ccm_mask[index]) |
                           ((uint32_t)value & ~ccm_mask[index]);
}

static uint64_t imx6ul_analog_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value;
    uint32_t index = offset >> 2;
    IMX6ULCCMState *s = (IMX6ULCCMState *)opaque;

    assert(index < CCM_ANALOG_MAX);

    switch (index) {
    case CCM_ANALOG_PLL_ARM_SET:
    case CCM_ANALOG_PLL_USB1_SET:
    case CCM_ANALOG_PLL_USB2_SET:
    case CCM_ANALOG_PLL_SYS_SET:
    case CCM_ANALOG_PLL_AUDIO_SET:
    case CCM_ANALOG_PLL_VIDEO_SET:
    case CCM_ANALOG_PLL_ENET_SET:
    case CCM_ANALOG_PFD_480_SET:
    case CCM_ANALOG_PFD_528_SET:
    case CCM_ANALOG_MISC0_SET:
    case PMU_MISC1_SET:
    case CCM_ANALOG_MISC2_SET:
    case USB_ANALOG_USB1_VBUS_DETECT_SET:
    case USB_ANALOG_USB1_CHRG_DETECT_SET:
    case USB_ANALOG_USB1_MISC_SET:
    case USB_ANALOG_USB2_VBUS_DETECT_SET:
    case USB_ANALOG_USB2_CHRG_DETECT_SET:
    case USB_ANALOG_USB2_MISC_SET:
    case TEMPMON_TEMPSENSE0_SET:
    case TEMPMON_TEMPSENSE1_SET:
    case TEMPMON_TEMPSENSE2_SET:
        /*
         * All REG_NAME_SET register access are in fact targeting
         * the REG_NAME register.
         */
        value = s->analog[index - 1];
        break;
    case CCM_ANALOG_PLL_ARM_CLR:
    case CCM_ANALOG_PLL_USB1_CLR:
    case CCM_ANALOG_PLL_USB2_CLR:
    case CCM_ANALOG_PLL_SYS_CLR:
    case CCM_ANALOG_PLL_AUDIO_CLR:
    case CCM_ANALOG_PLL_VIDEO_CLR:
    case CCM_ANALOG_PLL_ENET_CLR:
    case CCM_ANALOG_PFD_480_CLR:
    case CCM_ANALOG_PFD_528_CLR:
    case CCM_ANALOG_MISC0_CLR:
    case PMU_MISC1_CLR:
    case CCM_ANALOG_MISC2_CLR:
    case USB_ANALOG_USB1_VBUS_DETECT_CLR:
    case USB_ANALOG_USB1_CHRG_DETECT_CLR:
    case USB_ANALOG_USB1_MISC_CLR:
    case USB_ANALOG_USB2_VBUS_DETECT_CLR:
    case USB_ANALOG_USB2_CHRG_DETECT_CLR:
    case USB_ANALOG_USB2_MISC_CLR:
    case TEMPMON_TEMPSENSE0_CLR:
    case TEMPMON_TEMPSENSE1_CLR:
    case TEMPMON_TEMPSENSE2_CLR:
        /*
         * All REG_NAME_CLR register access are in fact targeting
         * the REG_NAME register.
         */
        value = s->analog[index - 2];
        break;
    case CCM_ANALOG_PLL_ARM_TOG:
    case CCM_ANALOG_PLL_USB1_TOG:
    case CCM_ANALOG_PLL_USB2_TOG:
    case CCM_ANALOG_PLL_SYS_TOG:
    case CCM_ANALOG_PLL_AUDIO_TOG:
    case CCM_ANALOG_PLL_VIDEO_TOG:
    case CCM_ANALOG_PLL_ENET_TOG:
    case CCM_ANALOG_PFD_480_TOG:
    case CCM_ANALOG_PFD_528_TOG:
    case CCM_ANALOG_MISC0_TOG:
    case PMU_MISC1_TOG:
    case CCM_ANALOG_MISC2_TOG:
    case USB_ANALOG_USB1_VBUS_DETECT_TOG:
    case USB_ANALOG_USB1_CHRG_DETECT_TOG:
    case USB_ANALOG_USB1_MISC_TOG:
    case USB_ANALOG_USB2_VBUS_DETECT_TOG:
    case USB_ANALOG_USB2_CHRG_DETECT_TOG:
    case USB_ANALOG_USB2_MISC_TOG:
    case TEMPMON_TEMPSENSE0_TOG:
    case TEMPMON_TEMPSENSE1_TOG:
    case TEMPMON_TEMPSENSE2_TOG:
        /*
         * All REG_NAME_TOG register access are in fact targeting
         * the REG_NAME register.
         */
        value = s->analog[index - 3];
        break;
    default:
        value = s->analog[index];
        break;
    }

    trace_ccm_read_reg(imx6ul_analog_reg_name(index), (uint32_t)value);

    return (uint64_t)value;
}

static void imx6ul_analog_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    uint32_t index = offset >> 2;
    IMX6ULCCMState *s = (IMX6ULCCMState *)opaque;

    assert(index < CCM_ANALOG_MAX);

    trace_ccm_write_reg(imx6ul_analog_reg_name(index), (uint32_t)value);

    switch (index) {
    case CCM_ANALOG_PLL_ARM_SET:
    case CCM_ANALOG_PLL_USB1_SET:
    case CCM_ANALOG_PLL_USB2_SET:
    case CCM_ANALOG_PLL_SYS_SET:
    case CCM_ANALOG_PLL_AUDIO_SET:
    case CCM_ANALOG_PLL_VIDEO_SET:
    case CCM_ANALOG_PLL_ENET_SET:
    case CCM_ANALOG_PFD_480_SET:
    case CCM_ANALOG_PFD_528_SET:
    case CCM_ANALOG_MISC0_SET:
    case PMU_MISC1_SET:
    case CCM_ANALOG_MISC2_SET:
    case USB_ANALOG_USB1_VBUS_DETECT_SET:
    case USB_ANALOG_USB1_CHRG_DETECT_SET:
    case USB_ANALOG_USB1_MISC_SET:
    case USB_ANALOG_USB2_VBUS_DETECT_SET:
    case USB_ANALOG_USB2_CHRG_DETECT_SET:
    case USB_ANALOG_USB2_MISC_SET:
        /*
         * All REG_NAME_SET register access are in fact targeting
         * the REG_NAME register. So we change the value of the
         * REG_NAME register, setting bits passed in the value.
         */
        s->analog[index - 1] |= (value & ~analog_mask[index - 1]);
        break;
    case CCM_ANALOG_PLL_ARM_CLR:
    case CCM_ANALOG_PLL_USB1_CLR:
    case CCM_ANALOG_PLL_USB2_CLR:
    case CCM_ANALOG_PLL_SYS_CLR:
    case CCM_ANALOG_PLL_AUDIO_CLR:
    case CCM_ANALOG_PLL_VIDEO_CLR:
    case CCM_ANALOG_PLL_ENET_CLR:
    case CCM_ANALOG_PFD_480_CLR:
    case CCM_ANALOG_PFD_528_CLR:
    case CCM_ANALOG_MISC0_CLR:
    case PMU_MISC1_CLR:
    case CCM_ANALOG_MISC2_CLR:
    case USB_ANALOG_USB1_VBUS_DETECT_CLR:
    case USB_ANALOG_USB1_CHRG_DETECT_CLR:
    case USB_ANALOG_USB1_MISC_CLR:
    case USB_ANALOG_USB2_VBUS_DETECT_CLR:
    case USB_ANALOG_USB2_CHRG_DETECT_CLR:
    case USB_ANALOG_USB2_MISC_CLR:
        /*
         * All REG_NAME_CLR register access are in fact targeting
         * the REG_NAME register. So we change the value of the
         * REG_NAME register, unsetting bits passed in the value.
         */
        s->analog[index - 2] &= ~(value & ~analog_mask[index - 2]);
        break;
    case CCM_ANALOG_PLL_ARM_TOG:
    case CCM_ANALOG_PLL_USB1_TOG:
    case CCM_ANALOG_PLL_USB2_TOG:
    case CCM_ANALOG_PLL_SYS_TOG:
    case CCM_ANALOG_PLL_AUDIO_TOG:
    case CCM_ANALOG_PLL_VIDEO_TOG:
    case CCM_ANALOG_PLL_ENET_TOG:
    case CCM_ANALOG_PFD_480_TOG:
    case CCM_ANALOG_PFD_528_TOG:
    case CCM_ANALOG_MISC0_TOG:
    case PMU_MISC1_TOG:
    case CCM_ANALOG_MISC2_TOG:
    case USB_ANALOG_USB1_VBUS_DETECT_TOG:
    case USB_ANALOG_USB1_CHRG_DETECT_TOG:
    case USB_ANALOG_USB1_MISC_TOG:
    case USB_ANALOG_USB2_VBUS_DETECT_TOG:
    case USB_ANALOG_USB2_CHRG_DETECT_TOG:
    case USB_ANALOG_USB2_MISC_TOG:
        /*
         * All REG_NAME_TOG register access are in fact targeting
         * the REG_NAME register. So we change the value of the
         * REG_NAME register, toggling bits passed in the value.
         */
        s->analog[index - 3] ^= (value & ~analog_mask[index - 3]);
        break;
    default:
        s->analog[index] = (s->analog[index] & analog_mask[index]) |
                           (value & ~analog_mask[index]);
        break;
    }
}

static const struct MemoryRegionOps imx6ul_ccm_ops = {
    .read = imx6ul_ccm_read,
    .write = imx6ul_ccm_write,
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

static const struct MemoryRegionOps imx6ul_analog_ops = {
    .read = imx6ul_analog_read,
    .write = imx6ul_analog_write,
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

static void imx6ul_ccm_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX6ULCCMState *s = IMX6UL_CCM(obj);

    /* initialize a container for the all memory range */
    memory_region_init(&s->container, OBJECT(dev), TYPE_IMX6UL_CCM, 0x8000);

    /* We initialize an IO memory region for the CCM part */
    memory_region_init_io(&s->ioccm, OBJECT(dev), &imx6ul_ccm_ops, s,
                          TYPE_IMX6UL_CCM ".ccm", CCM_MAX * sizeof(uint32_t));

    /* Add the CCM as a subregion at offset 0 */
    memory_region_add_subregion(&s->container, 0, &s->ioccm);

    /* We initialize an IO memory region for the ANALOG part */
    memory_region_init_io(&s->ioanalog, OBJECT(dev), &imx6ul_analog_ops, s,
                          TYPE_IMX6UL_CCM ".analog",
                          CCM_ANALOG_MAX * sizeof(uint32_t));

    /* Add the ANALOG as a subregion at offset 0x4000 */
    memory_region_add_subregion(&s->container, 0x4000, &s->ioanalog);

    sysbus_init_mmio(sd, &s->container);
}

static void imx6ul_ccm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IMXCCMClass *ccm = IMX_CCM_CLASS(klass);

    device_class_set_legacy_reset(dc, imx6ul_ccm_reset);
    dc->vmsd = &vmstate_imx6ul_ccm;
    dc->desc = "i.MX6UL Clock Control Module";

    ccm->get_clock_frequency = imx6ul_ccm_get_clock_frequency;
}

static const TypeInfo imx6ul_ccm_info = {
    .name          = TYPE_IMX6UL_CCM,
    .parent        = TYPE_IMX_CCM,
    .instance_size = sizeof(IMX6ULCCMState),
    .instance_init = imx6ul_ccm_init,
    .class_init    = imx6ul_ccm_class_init,
};

static void imx6ul_ccm_register_types(void)
{
    type_register_static(&imx6ul_ccm_info);
}

type_init(imx6ul_ccm_register_types)
