/*
 * Stubs for platforms different from ARM
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "semihosting/semihost.h"
#include <glib.h>

bool semihosting_arm_compatible(void)
{
    return false;
}

void semihosting_arm_compatible_init(void)
{
    g_assert_not_reached();
}
