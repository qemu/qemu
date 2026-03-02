/*
 * kvm target arch specific stubs
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * Author:
 *   Ani Sinha <anisinha@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "system/kvm.h"

int kvm_arch_on_vmfd_change(MachineState *ms, KVMState *s)
{
    abort();
}

bool kvm_arch_supports_vmfd_change(void)
{
    return false;
}
