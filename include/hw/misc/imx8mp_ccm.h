/*
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * i.MX 8M Plus CCM IP block emulation code
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef IMX8MP_CCM_H
#define IMX8MP_CCM_H

#include "hw/misc/imx_ccm.h"
#include "qom/object.h"

enum IMX8MPCCMRegisters {
    CCM_MAX = 0xc6fc / sizeof(uint32_t) + 1,
};

#define TYPE_IMX8MP_CCM "imx8mp.ccm"
OBJECT_DECLARE_SIMPLE_TYPE(IMX8MPCCMState, IMX8MP_CCM)

struct IMX8MPCCMState {
    IMXCCMState parent_obj;

    MemoryRegion iomem;

    uint32_t ccm[CCM_MAX];
};

#endif /* IMX8MP_CCM_H */
