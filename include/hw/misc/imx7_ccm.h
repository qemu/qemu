/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX7 CCM, PMU and ANALOG IP blocks emulation code
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX7_CCM_H
#define IMX7_CCM_H

#include "hw/misc/imx_ccm.h"
#include "qemu/bitops.h"
#include "qom/object.h"

enum IMX7AnalogRegisters {
    ANALOG_PLL_ARM,
    ANALOG_PLL_ARM_SET,
    ANALOG_PLL_ARM_CLR,
    ANALOG_PLL_ARM_TOG,
    ANALOG_PLL_DDR,
    ANALOG_PLL_DDR_SET,
    ANALOG_PLL_DDR_CLR,
    ANALOG_PLL_DDR_TOG,
    ANALOG_PLL_DDR_SS,
    ANALOG_PLL_DDR_SS_SET,
    ANALOG_PLL_DDR_SS_CLR,
    ANALOG_PLL_DDR_SS_TOG,
    ANALOG_PLL_DDR_NUM,
    ANALOG_PLL_DDR_NUM_SET,
    ANALOG_PLL_DDR_NUM_CLR,
    ANALOG_PLL_DDR_NUM_TOG,
    ANALOG_PLL_DDR_DENOM,
    ANALOG_PLL_DDR_DENOM_SET,
    ANALOG_PLL_DDR_DENOM_CLR,
    ANALOG_PLL_DDR_DENOM_TOG,
    ANALOG_PLL_480,
    ANALOG_PLL_480_SET,
    ANALOG_PLL_480_CLR,
    ANALOG_PLL_480_TOG,
    ANALOG_PLL_480A,
    ANALOG_PLL_480A_SET,
    ANALOG_PLL_480A_CLR,
    ANALOG_PLL_480A_TOG,
    ANALOG_PLL_480B,
    ANALOG_PLL_480B_SET,
    ANALOG_PLL_480B_CLR,
    ANALOG_PLL_480B_TOG,
    ANALOG_PLL_ENET,
    ANALOG_PLL_ENET_SET,
    ANALOG_PLL_ENET_CLR,
    ANALOG_PLL_ENET_TOG,
    ANALOG_PLL_AUDIO,
    ANALOG_PLL_AUDIO_SET,
    ANALOG_PLL_AUDIO_CLR,
    ANALOG_PLL_AUDIO_TOG,
    ANALOG_PLL_AUDIO_SS,
    ANALOG_PLL_AUDIO_SS_SET,
    ANALOG_PLL_AUDIO_SS_CLR,
    ANALOG_PLL_AUDIO_SS_TOG,
    ANALOG_PLL_AUDIO_NUM,
    ANALOG_PLL_AUDIO_NUM_SET,
    ANALOG_PLL_AUDIO_NUM_CLR,
    ANALOG_PLL_AUDIO_NUM_TOG,
    ANALOG_PLL_AUDIO_DENOM,
    ANALOG_PLL_AUDIO_DENOM_SET,
    ANALOG_PLL_AUDIO_DENOM_CLR,
    ANALOG_PLL_AUDIO_DENOM_TOG,
    ANALOG_PLL_VIDEO,
    ANALOG_PLL_VIDEO_SET,
    ANALOG_PLL_VIDEO_CLR,
    ANALOG_PLL_VIDEO_TOG,
    ANALOG_PLL_VIDEO_SS,
    ANALOG_PLL_VIDEO_SS_SET,
    ANALOG_PLL_VIDEO_SS_CLR,
    ANALOG_PLL_VIDEO_SS_TOG,
    ANALOG_PLL_VIDEO_NUM,
    ANALOG_PLL_VIDEO_NUM_SET,
    ANALOG_PLL_VIDEO_NUM_CLR,
    ANALOG_PLL_VIDEO_NUM_TOG,
    ANALOG_PLL_VIDEO_DENOM,
    ANALOG_PLL_VIDEO_DENOM_SET,
    ANALOG_PLL_VIDEO_DENOM_CLR,
    ANALOG_PLL_VIDEO_DENOM_TOG,
    ANALOG_PLL_MISC0,
    ANALOG_PLL_MISC0_SET,
    ANALOG_PLL_MISC0_CLR,
    ANALOG_PLL_MISC0_TOG,

    ANALOG_DIGPROG = 0x800 / sizeof(uint32_t),
    ANALOG_MAX,

    ANALOG_PLL_LOCK = BIT(31)
};

enum IMX7CCMRegisters {
    CCM_MAX = 0xBE00 / sizeof(uint32_t) + 1,
};

enum IMX7PMURegisters {
    PMU_MAX = 0x140 / sizeof(uint32_t),
};

#define TYPE_IMX7_CCM "imx7.ccm"
OBJECT_DECLARE_SIMPLE_TYPE(IMX7CCMState, IMX7_CCM)

struct IMX7CCMState {
    /* <private> */
    IMXCCMState parent_obj;

    /* <public> */
    MemoryRegion iomem;

    uint32_t ccm[CCM_MAX];
};


#define TYPE_IMX7_ANALOG "imx7.analog"
OBJECT_DECLARE_SIMPLE_TYPE(IMX7AnalogState, IMX7_ANALOG)

struct IMX7AnalogState {
    /* <private> */
    IMXCCMState parent_obj;

    /* <public> */
    struct {
        MemoryRegion container;
        MemoryRegion analog;
        MemoryRegion digprog;
        MemoryRegion pmu;
    } mmio;

    uint32_t analog[ANALOG_MAX];
    uint32_t pmu[PMU_MAX];
};

#endif /* IMX7_CCM_H */
