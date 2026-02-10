/*
 * QEMU OpenRISC CPU QOM header (target agnostic)
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef QEMU_OPENRISC_CPU_QOM_H
#define QEMU_OPENRISC_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_OPENRISC_CPU "or1k-cpu"

OBJECT_DECLARE_CPU_TYPE(OpenRISCCPU, OpenRISCCPUClass, OPENRISC_CPU)

#define OPENRISC_CPU_TYPE_SUFFIX "-" TYPE_OPENRISC_CPU
#define OPENRISC_CPU_TYPE_NAME(model) model OPENRISC_CPU_TYPE_SUFFIX

#endif
