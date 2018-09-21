/*
 * QEMU KVM Hyper-V support
 *
 * Copyright (C) 2015 Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * Authors:
 *  Andrey Smetanin <asmetanin@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef TARGET_I386_HYPERV_H
#define TARGET_I386_HYPERV_H

#include "cpu.h"
#include "sysemu/kvm.h"

typedef struct HvSintRoute HvSintRoute;
typedef void (*HvSintAckClb)(void *data);

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit);

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintAckClb sint_ack_clb,
                                   void *sint_ack_clb_data);
void hyperv_sint_route_ref(HvSintRoute *sint_route);
void hyperv_sint_route_unref(HvSintRoute *sint_route);

int hyperv_sint_route_set_sint(HvSintRoute *sint_route);

static inline uint32_t hyperv_vp_index(X86CPU *cpu)
{
    return CPU(cpu)->cpu_index;
}

#endif
