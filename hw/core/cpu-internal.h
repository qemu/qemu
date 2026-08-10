/*
 * QEMU private CPU interface between user / system modes)
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_CORE_CPU_INTERNAL_H
#define HW_CORE_CPU_INTERNAL_H

#include "hw/core/qdev.h"
#include "hw/core/cpu.h"

void cpu_class_init_props(DeviceClass *dc);
void cpu_exec_class_post_init(CPUClass *cc);

void cpu_exec_init(CPUState *cpu);
void cpu_exec_realize(CPUState *cpu, Error **errp);

void cpu_vmstate_register(CPUState *cpu);
void cpu_vmstate_unregister(CPUState *cpu);

#endif
