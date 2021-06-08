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
#include "cpu.h"
#include "kvm_i386.h"

#ifndef __OPTIMIZE__
bool kvm_has_smm(void)
{
    return 1;
}

bool kvm_enable_x2apic(void)
{
    return false;
}

/* This function is only called inside conditionals which we
 * rely on the compiler to optimize out when CONFIG_KVM is not
 * defined.
 */
uint32_t kvm_arch_get_supported_cpuid(KVMState *env, uint32_t function,
                                      uint32_t index, int reg)
{
    abort();
}
#endif

bool kvm_hv_vpindex_settable(void)
{
    return false;
}

bool kvm_hyperv_expand_features(X86CPU *cpu, Error **errp)
{
    abort();
}
