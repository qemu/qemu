/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#ifndef FSI_FSI_H
#define FSI_FSI_H

#include "hw/qdev-core.h"

#define TYPE_FSI_BUS "fsi.bus"
OBJECT_DECLARE_SIMPLE_TYPE(FSIBus, FSI_BUS)

typedef struct FSIBus {
    BusState bus;
} FSIBus;

#endif /* FSI_FSI_H */
