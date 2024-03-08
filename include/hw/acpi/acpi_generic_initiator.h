// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#ifndef ACPI_GENERIC_INITIATOR_H
#define ACPI_GENERIC_INITIATOR_H

#include "qom/object_interfaces.h"

#define TYPE_ACPI_GENERIC_INITIATOR "acpi-generic-initiator"

typedef struct AcpiGenericInitiator {
    /* private */
    Object parent;

    /* public */
    char *pci_dev;
    uint16_t node;
} AcpiGenericInitiator;

#endif
