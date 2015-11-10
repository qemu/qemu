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

#ifndef HYPERV_I386_H
#define HYPERV_I386_H

#include "cpu.h"
#include "sysemu/kvm.h"
#include "qemu/event_notifier.h"

typedef struct HvSintRoute HvSintRoute;
typedef void (*HvSintAckClb)(HvSintRoute *sint_route);

struct HvSintRoute {
    uint32_t sint;
    uint32_t vcpu_id;
    int gsi;
    EventNotifier sint_set_notifier;
    EventNotifier sint_ack_notifier;
    HvSintAckClb sint_ack_clb;
};

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit);

HvSintRoute *kvm_hv_sint_route_create(uint32_t vcpu_id, uint32_t sint,
                                      HvSintAckClb sint_ack_clb);

void kvm_hv_sint_route_destroy(HvSintRoute *sint_route);

int kvm_hv_sint_route_set_sint(HvSintRoute *sint_route);

#endif
