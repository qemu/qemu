/*
 * Nitro Enclaves accelerator - public interface
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSTEM_NITRO_ACCEL_H
#define SYSTEM_NITRO_ACCEL_H

#include "qemu/accel.h"

extern bool nitro_allowed;

static inline bool nitro_enabled(void)
{
    return nitro_allowed;
}

#define TYPE_NITRO_ACCEL ACCEL_CLASS_NAME("nitro")

typedef struct NitroAccelState NitroAccelState;
DECLARE_INSTANCE_CHECKER(NitroAccelState, NITRO_ACCEL,
                         TYPE_NITRO_ACCEL)

#endif /* SYSTEM_NITRO_ACCEL_H */
