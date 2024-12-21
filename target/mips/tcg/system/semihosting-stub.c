/*
 *  MIPS semihosting stub
 *
 * SPDX-FileContributor: Philippe Mathieu-Daudé <philmd@linaro.org>
 * SPDX-FileCopyrightText: 2024 Linaro Ltd.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "internal.h"

void mips_semihosting(CPUMIPSState *env)
{
    g_assert_not_reached();
}
