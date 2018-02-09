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

#define TYPE_IMX7_GPR "imx7.gpr"
#define IMX7_GPR(obj) OBJECT_CHECK(IMX7GPRState, (obj), TYPE_IMX7_GPR)

typedef struct IMX7GPRState {
    /* <private> */
    SysBusDevice parent_obj;

    MemoryRegion mmio;
} IMX7GPRState;

#endif /* IMX7_GPR_H */
