/*
 * Copyright (C) 2020       Citrix Systems UK Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/xen/xen_igd.h"
#include "qapi/error.h"

bool xen_igd_gfx_pt_enabled(void)
{
    return false;
}

void xen_igd_gfx_pt_set(bool value, Error **errp)
{
    if (value) {
        error_setg(errp, "Xen PCI passthrough support not built in");
    }
}

void xen_igd_reserve_slot(PCIBus *pci_bus)
{
}
