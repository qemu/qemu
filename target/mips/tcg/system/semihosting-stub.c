/*
 *  MIPS semihosting stub
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2024 Linaro Ltd.
 * Authors:
 *   Philippe Mathieu-Daudé
 */

#include "qemu/osdep.h"
#include "internal.h"

void mips_semihosting(CPUMIPSState *env)
{
    g_assert_not_reached();
}
