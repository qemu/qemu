/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Stubs for calls made from machines to handle the case where CONFIG_PXB
 * is not enabled.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-bridge/pci_expander_bridge.h"
#include "hw/cxl/cxl.h"

void pxb_cxl_hook_up_registers(CXLState *state, PCIBus *bus, Error **errp) {};
