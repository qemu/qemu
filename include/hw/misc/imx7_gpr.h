/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX7 GPR IP block emulation code
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX7_GPR_H
#define IMX7_GPR_H

#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX7_GPR "imx7.gpr"
OBJECT_DECLARE_SIMPLE_TYPE(IMX7GPRState, IMX7_GPR)

struct IMX7GPRState {
    /* <private> */
    SysBusDevice parent_obj;

    MemoryRegion mmio;
};

#endif /* IMX7_GPR_H */
