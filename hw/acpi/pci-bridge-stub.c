/*
 * QEMU ACPI PCI bridge stub
 *
 * Copyright (c) 2023 Red Hat, Inc.
 *
 * Author:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/acpi/pci.h"

void build_pci_bridge_aml(AcpiDevAmlIf *adev, Aml *scope)
{
}
