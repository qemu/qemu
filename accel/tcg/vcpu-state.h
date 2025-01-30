/*
 * TaskState helpers for QEMU
 *
 * Copyright (c) 2023 Linaro Ltd.
 *
 * Authors:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ACCEL_TCG_VCPU_STATE_H
#define ACCEL_TCG_VCPU_STATE_H

#include "hw/core/cpu.h"

#ifdef CONFIG_USER_ONLY
static inline TaskState *get_task_state(const CPUState *cs)
{
    return cs->opaque;
}
#endif

#endif
