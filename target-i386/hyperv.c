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

#include "qemu/osdep.h"
#include "hyperv.h"
#include "standard-headers/asm-x86/hyperv.h"

int kvm_hv_handle_exit(X86CPU *cpu, struct kvm_hyperv_exit *exit)
{
    CPUX86State *env = &cpu->env;

    switch (exit->type) {
    case KVM_EXIT_HYPERV_SYNIC:
        if (!cpu->hyperv_synic) {
            return -1;
        }

        /*
         * For now just track changes in SynIC control and msg/evt pages msr's.
         * When SynIC messaging/events processing will be added in future
         * here we will do messages queues flushing and pages remapping.
         */
        switch (exit->u.synic.msr) {
        case HV_X64_MSR_SCONTROL:
            env->msr_hv_synic_control = exit->u.synic.control;
            break;
        case HV_X64_MSR_SIMP:
            env->msr_hv_synic_msg_page = exit->u.synic.msg_page;
            break;
        case HV_X64_MSR_SIEFP:
            env->msr_hv_synic_evt_page = exit->u.synic.evt_page;
            break;
        default:
            return -1;
        }
        return 0;
    default:
        return -1;
    }
}

static void kvm_hv_sint_ack_handler(EventNotifier *notifier)
{
    HvSintRoute *sint_route = container_of(notifier, HvSintRoute,
                                           sint_ack_notifier);
    event_notifier_test_and_clear(notifier);
    if (sint_route->sint_ack_clb) {
        sint_route->sint_ack_clb(sint_route);
    }
}

HvSintRoute *kvm_hv_sint_route_create(uint32_t vcpu_id, uint32_t sint,
                                      HvSintAckClb sint_ack_clb)
{
    HvSintRoute *sint_route;
    int r, gsi;

    sint_route = g_malloc0(sizeof(*sint_route));
    r = event_notifier_init(&sint_route->sint_set_notifier, false);
    if (r) {
        goto err;
    }

    r = event_notifier_init(&sint_route->sint_ack_notifier, false);
    if (r) {
        goto err_sint_set_notifier;
    }

    event_notifier_set_handler(&sint_route->sint_ack_notifier,
                               kvm_hv_sint_ack_handler);

    gsi = kvm_irqchip_add_hv_sint_route(kvm_state, vcpu_id, sint);
    if (gsi < 0) {
        goto err_gsi;
    }

    r = kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &sint_route->sint_set_notifier,
                                           &sint_route->sint_ack_notifier, gsi);
    if (r) {
        goto err_irqfd;
    }
    sint_route->gsi = gsi;
    sint_route->sint_ack_clb = sint_ack_clb;
    sint_route->vcpu_id = vcpu_id;
    sint_route->sint = sint;

    return sint_route;

err_irqfd:
    kvm_irqchip_release_virq(kvm_state, gsi);
err_gsi:
    event_notifier_set_handler(&sint_route->sint_ack_notifier, NULL);
    event_notifier_cleanup(&sint_route->sint_ack_notifier);
err_sint_set_notifier:
    event_notifier_cleanup(&sint_route->sint_set_notifier);
err:
    g_free(sint_route);

    return NULL;
}

void kvm_hv_sint_route_destroy(HvSintRoute *sint_route)
{
    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state,
                                          &sint_route->sint_set_notifier,
                                          sint_route->gsi);
    kvm_irqchip_release_virq(kvm_state, sint_route->gsi);
    event_notifier_set_handler(&sint_route->sint_ack_notifier, NULL);
    event_notifier_cleanup(&sint_route->sint_ack_notifier);
    event_notifier_cleanup(&sint_route->sint_set_notifier);
    g_free(sint_route);
}

int kvm_hv_sint_route_set_sint(HvSintRoute *sint_route)
{
    return event_notifier_set(&sint_route->sint_set_notifier);
}
