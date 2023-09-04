/*
 * QEMU KVM x86 specific function stubs
 *
 * Copyright Linaro Limited 2012
 *
 * Author: Peter Maydell <peter.maydell@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "kvm_i386.h"

#ifndef __OPTIMIZE__
bool kvm_enable_x2apic(void)
{
    return false;
}
#endif

bool kvm_hv_vpindex_settable(void)
{
    return false;
}
