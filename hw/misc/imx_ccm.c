/*
 * IMX31 Clock Control Module
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This is an abstract base class used to get a common interface to
 * retrieve the CCM frequencies from the various i.MX SOC.
 */

#include "qemu/osdep.h"
#include "hw/misc/imx_ccm.h"
#include "qemu/log.h"
#include "qemu/module.h"

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


uint32_t imx_ccm_get_clock_frequency(IMXCCMState *dev, IMXClk clock)
{
    uint32_t freq = 0;
    IMXCCMClass *klass = IMX_CCM_GET_CLASS(dev);

    if (klass->get_clock_frequency) {
        freq = klass->get_clock_frequency(dev, clock);
    }

    DPRINTF("(clock = %d) = %u\n", clock, freq);

    return freq;
}

/*
 * Calculate PLL output frequency
 */
uint32_t imx_ccm_calc_pll(uint32_t pllreg, uint32_t base_freq)
{
    int32_t freq;
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

    freq = ((2 * (base_freq >> 10) * (mfi * mfd + mfn)) /
            (mfd * pd)) << 10;

    DPRINTF("(pllreg = 0x%08x, base_freq = %u) = %d\n", pllreg, base_freq,
            freq);

    return freq;
}

static const TypeInfo imx_ccm_info = {
    .name          = TYPE_IMX_CCM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXCCMState),
    .class_size    = sizeof(IMXCCMClass),
    .abstract      = true,
};

static void imx_ccm_register_types(void)
{
    type_register_static(&imx_ccm_info);
}

type_init(imx_ccm_register_types)
