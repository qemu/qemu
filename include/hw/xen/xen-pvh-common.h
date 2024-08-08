/*
 * QEMU Xen PVH machine - common code.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef XEN_PVH_COMMON_H__
#define XEN_PVH_COMMON_H__

#include <assert.h>
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/xen/xen-hvm-common.h"
#include "hw/pci-host/gpex.h"

#define TYPE_XEN_PVH_MACHINE MACHINE_TYPE_NAME("xen-pvh-base")
OBJECT_DECLARE_TYPE(XenPVHMachineState, XenPVHMachineClass,
                    XEN_PVH_MACHINE)

struct XenPVHMachineClass {
    MachineClass parent;

    /* PVH implementation specific init.  */
    void (*init)(MachineState *state);

    /*
     * Each implementation can optionally enable features that it
     * supports and are known to work.
     */
    bool has_tpm;
    bool has_virtio_mmio;
};

struct XenPVHMachineState {
    /*< private >*/
    MachineState parent;

    XenIOState ioreq;

    struct {
        MemoryRegion low;
        MemoryRegion high;
    } ram;

    struct {
        MemMapEntry ram_low, ram_high;
        MemMapEntry tpm;

        /* Virtio-mmio */
        MemMapEntry virtio_mmio;
        uint32_t virtio_mmio_num;
        uint32_t virtio_mmio_irq_base;
    } cfg;
};

void xen_pvh_class_setup_common_props(XenPVHMachineClass *xpc);
#endif
