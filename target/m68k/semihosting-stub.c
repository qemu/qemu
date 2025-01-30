/*
 *  m68k/ColdFire semihosting stub
 *
 * Copyright (c) 2024 Linaro Ltd.
 *
 * Authors:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"

void do_m68k_semihosting(CPUM68KState *env, int nr)
{
    g_assert_not_reached();
}
