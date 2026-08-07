/*
 * QEMU Universal Flash Storage (UFS) sysbus controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_UFS_UFS_SYSBUS_H
#define HW_UFS_UFS_SYSBUS_H

#include "hw/core/sysbus.h"
#include "ufs.h"

#define TYPE_SYSBUS_UFS "sysbus-ufs"
OBJECT_DECLARE_SIMPLE_TYPE(SysbusUfsState, SYSBUS_UFS)

struct SysbusUfsState {
    SysBusDevice parent_obj;

    UfsHc ufs;
};

#endif /* HW_UFS_UFS_SYSBUS_H */
