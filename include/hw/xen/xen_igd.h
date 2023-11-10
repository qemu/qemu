/*
 * Copyright (c) 2007, Neocleus Corporation.
 * Copyright (c) 2007, Intel Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Alex Novik <alex@neocleus.com>
 * Allen Kay <allen.m.kay@intel.com>
 * Guy Zana <guy@neocleus.com>
 */
#ifndef XEN_IGD_H
#define XEN_IGD_H

#include "hw/xen/xen-host-pci-device.h"

typedef struct XenPCIPassthroughState XenPCIPassthroughState;

bool xen_igd_gfx_pt_enabled(void);
void xen_igd_gfx_pt_set(bool value, Error **errp);

uint32_t igd_read_opregion(XenPCIPassthroughState *s);
void xen_igd_reserve_slot(PCIBus *pci_bus);
void igd_write_opregion(XenPCIPassthroughState *s, uint32_t val);
void xen_igd_passthrough_isa_bridge_create(XenPCIPassthroughState *s,
                                           XenHostPCIDevice *dev);

static inline bool is_igd_vga_passthrough(XenHostPCIDevice *dev)
{
    return (xen_igd_gfx_pt_enabled()
            && ((dev->class_code >> 0x8) == PCI_CLASS_DISPLAY_VGA));
}

#endif
