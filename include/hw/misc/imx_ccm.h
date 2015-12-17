/*
 * IMX Clock Control Module base class
 *
 * Copyright (C) 2012 NICTA
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX_CCM_H
#define IMX_CCM_H

#include "hw/sysbus.h"

#define CKIL_FREQ 32768 /* nominal 32khz clock */

/* PLL control registers */
#define PD(v) (((v) >> 26) & 0xf)
#define MFD(v) (((v) >> 16) & 0x3ff)
#define MFI(v) (((v) >> 10) & 0xf);
#define MFN(v) ((v) & 0x3ff)

#define PLL_PD(x)               (((x) & 0xf) << 26)
#define PLL_MFD(x)              (((x) & 0x3ff) << 16)
#define PLL_MFI(x)              (((x) & 0xf) << 10)
#define PLL_MFN(x)              (((x) & 0x3ff) << 0)

#define TYPE_IMX_CCM "imx.ccm"
#define IMX_CCM(obj) \
     OBJECT_CHECK(IMXCCMState, (obj), TYPE_IMX_CCM)
#define IMX_CCM_CLASS(klass) \
     OBJECT_CLASS_CHECK(IMXCCMClass, (klass), TYPE_IMX_CCM)
#define IMX_GET_CLASS(obj) \
     OBJECT_GET_CLASS(IMXCCMClass, (obj), TYPE_IMX_CCM)

typedef struct IMXCCMState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */

} IMXCCMState;

typedef enum  {
    NOCLK,
    CLK_MPLL,
    CLK_UPLL,
    CLK_MCU,
    CLK_HSP,
    CLK_MAX,
    CLK_AHB,
    CLK_IPG,
    CLK_PER,
    CLK_32k
} IMXClk;

typedef struct IMXCCMClass {
    /* <private> */
    SysBusDeviceClass parent_class;

    /* <public> */
    uint32_t (*get_clock_frequency)(IMXCCMState *s, IMXClk clk);
} IMXCCMClass;

uint32_t imx_ccm_calc_pll(uint32_t pllreg, uint32_t base_freq);

uint32_t imx_ccm_get_clock_frequency(IMXCCMState *s, IMXClk clock);

#endif /* IMX_CCM_H */
