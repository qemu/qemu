/*
 * Semihosting Stubs for all targets
 *
 * Copyright (c) 2023 Linaro Ltd
 *
 * Stubs for all targets that don't actually do semihosting.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "semihosting/semihost.h"

SemihostingTarget semihosting_get_target(void)
{
    return SEMIHOSTING_TARGET_AUTO;
}
