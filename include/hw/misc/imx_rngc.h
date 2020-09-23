/*
 * Freescale i.MX RNGC emulation
 *
 * Copyright (C) 2020 Martin Kaiser <martin@kaiser.cx>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX_RNGC_H
#define IMX_RNGC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IMX_RNGC "imx.rngc"
OBJECT_DECLARE_SIMPLE_TYPE(IMXRNGCState, IMX_RNGC)

struct IMXRNGCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion  iomem;

    uint8_t op_self_test;
    uint8_t op_seed;
    uint8_t mask;
    bool    auto_seed;

    QEMUBH *self_test_bh;
    QEMUBH *seed_bh;
    qemu_irq irq;
};

#endif /* IMX_RNGC_H */
