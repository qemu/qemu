/*
 * QEMU Hypervisor.framework (HVF) stubs for ARM
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hvf_arm.h"

uint32_t hvf_arm_get_default_ipa_bit_size(void)
{
    g_assert_not_reached();
}

uint32_t hvf_arm_get_max_ipa_bit_size(void)
{
    g_assert_not_reached();
}
