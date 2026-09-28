/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PCI_EXPANDER_BRIDGE_H
#define PCI_EXPANDER_BRIDGE_H

#include "hw/cxl/cxl.h"

void pxb_cxl_hook_up_registers(CXLState *state, PCIBus *bus, Error **errp);

#endif /* PCI_EXPANDER_BRIDGE_H */
