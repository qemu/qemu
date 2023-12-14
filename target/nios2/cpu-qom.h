/*
 * QEMU Nios II CPU QOM header (target agnostic)
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef QEMU_NIOS2_CPU_QOM_H
#define QEMU_NIOS2_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_NIOS2_CPU "nios2-cpu"

OBJECT_DECLARE_CPU_TYPE(Nios2CPU, Nios2CPUClass, NIOS2_CPU)

#endif
