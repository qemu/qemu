/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Virt system Controller
 */

#ifndef VIRT_CTRL_H
#define VIRT_CTRL_H

#include "hw/sysbus.h"

#define TYPE_VIRT_CTRL "virt-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(VirtCtrlState, VIRT_CTRL)

struct VirtCtrlState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t irq_enabled;
};

#endif
