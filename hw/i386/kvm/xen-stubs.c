/*
 * QEMU Xen emulation: QMP stubs
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "xen_evtchn.h"
#include "xen_primary_console.h"

void xen_evtchn_snoop_msi(PCIDevice *dev, bool is_msix, unsigned int vector,
                          uint64_t addr, uint32_t data, bool is_masked)
{
}

void xen_evtchn_remove_pci_device(PCIDevice *dev)
{
}

bool xen_evtchn_deliver_pirq_msi(uint64_t address, uint32_t data)
{
    return false;
}

void xen_primary_console_create(void)
{
}

void xen_primary_console_set_be_port(uint16_t port)
{
}
