/*
 * Hyper-V guest/hypervisor interaction
 *
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "sysemu/kvm.h"
#include "hw/hyperv/hyperv.h"

typedef struct SynICState {
    DeviceState parent_obj;

    CPUState *cs;

    bool enabled;
    hwaddr msg_page_addr;
    hwaddr event_page_addr;
    MemoryRegion msg_page_mr;
    MemoryRegion event_page_mr;
    struct hyperv_message_page *msg_page;
    struct hyperv_event_flags_page *event_page;
} SynICState;

#define TYPE_SYNIC "hyperv-synic"
#define SYNIC(obj) OBJECT_CHECK(SynICState, (obj), TYPE_SYNIC)

static SynICState *get_synic(CPUState *cs)
{
    return SYNIC(object_resolve_path_component(OBJECT(cs), "synic"));
}

static void synic_update(SynICState *synic, bool enable,
                         hwaddr msg_page_addr, hwaddr event_page_addr)
{

    synic->enabled = enable;
    if (synic->msg_page_addr != msg_page_addr) {
        if (synic->msg_page_addr) {
            memory_region_del_subregion(get_system_memory(),
                                        &synic->msg_page_mr);
        }
        if (msg_page_addr) {
            memory_region_add_subregion(get_system_memory(), msg_page_addr,
                                        &synic->msg_page_mr);
        }
        synic->msg_page_addr = msg_page_addr;
    }
    if (synic->event_page_addr != event_page_addr) {
        if (synic->event_page_addr) {
            memory_region_del_subregion(get_system_memory(),
                                        &synic->event_page_mr);
        }
        if (event_page_addr) {
            memory_region_add_subregion(get_system_memory(), event_page_addr,
                                        &synic->event_page_mr);
        }
        synic->event_page_addr = event_page_addr;
    }
}

void hyperv_synic_update(CPUState *cs, bool enable,
                         hwaddr msg_page_addr, hwaddr event_page_addr)
{
    SynICState *synic = get_synic(cs);

    if (!synic) {
        return;
    }

    synic_update(synic, enable, msg_page_addr, event_page_addr);
}

static void synic_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    SynICState *synic = SYNIC(dev);
    char *msgp_name, *eventp_name;
    uint32_t vp_index;

    /* memory region names have to be globally unique */
    vp_index = hyperv_vp_index(synic->cs);
    msgp_name = g_strdup_printf("synic-%u-msg-page", vp_index);
    eventp_name = g_strdup_printf("synic-%u-event-page", vp_index);

    memory_region_init_ram(&synic->msg_page_mr, obj, msgp_name,
                           sizeof(*synic->msg_page), &error_abort);
    memory_region_init_ram(&synic->event_page_mr, obj, eventp_name,
                           sizeof(*synic->event_page), &error_abort);
    synic->msg_page = memory_region_get_ram_ptr(&synic->msg_page_mr);
    synic->event_page = memory_region_get_ram_ptr(&synic->event_page_mr);

    g_free(msgp_name);
    g_free(eventp_name);
}
static void synic_reset(DeviceState *dev)
{
    SynICState *synic = SYNIC(dev);
    memset(synic->msg_page, 0, sizeof(*synic->msg_page));
    memset(synic->event_page, 0, sizeof(*synic->event_page));
    synic_update(synic, false, 0, 0);
}

static void synic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = synic_realize;
    dc->reset = synic_reset;
    dc->user_creatable = false;
}

void hyperv_synic_add(CPUState *cs)
{
    Object *obj;
    SynICState *synic;

    obj = object_new(TYPE_SYNIC);
    synic = SYNIC(obj);
    synic->cs = cs;
    object_property_add_child(OBJECT(cs), "synic", obj, &error_abort);
    object_unref(obj);
    object_property_set_bool(obj, true, "realized", &error_abort);
}

void hyperv_synic_reset(CPUState *cs)
{
    device_reset(DEVICE(get_synic(cs)));
}

static const TypeInfo synic_type_info = {
    .name = TYPE_SYNIC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SynICState),
    .class_init = synic_class_init,
};

static void synic_register_types(void)
{
    type_register_static(&synic_type_info);
}

type_init(synic_register_types)

struct HvSintRoute {
    uint32_t sint;
    SynICState *synic;
    int gsi;
    EventNotifier sint_set_notifier;
    EventNotifier sint_ack_notifier;
    HvSintAckClb sint_ack_clb;
    void *sint_ack_clb_data;
    unsigned refcount;
};

static CPUState *hyperv_find_vcpu(uint32_t vp_index)
{
    CPUState *cs = qemu_get_cpu(vp_index);
    assert(hyperv_vp_index(cs) == vp_index);
    return cs;
}

static void kvm_hv_sint_ack_handler(EventNotifier *notifier)
{
    HvSintRoute *sint_route = container_of(notifier, HvSintRoute,
                                           sint_ack_notifier);
    event_notifier_test_and_clear(notifier);
    sint_route->sint_ack_clb(sint_route->sint_ack_clb_data);
}

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintAckClb sint_ack_clb,
                                   void *sint_ack_clb_data)
{
    HvSintRoute *sint_route;
    EventNotifier *ack_notifier;
    int r, gsi;
    CPUState *cs;
    SynICState *synic;

    cs = hyperv_find_vcpu(vp_index);
    if (!cs) {
        return NULL;
    }

    synic = get_synic(cs);
    if (!synic) {
        return NULL;
    }

    sint_route = g_new0(HvSintRoute, 1);
    r = event_notifier_init(&sint_route->sint_set_notifier, false);
    if (r) {
        goto err;
    }

    ack_notifier = sint_ack_clb ? &sint_route->sint_ack_notifier : NULL;
    if (ack_notifier) {
        r = event_notifier_init(ack_notifier, false);
        if (r) {
            goto err_sint_set_notifier;
        }

        event_notifier_set_handler(ack_notifier, kvm_hv_sint_ack_handler);
    }

    gsi = kvm_irqchip_add_hv_sint_route(kvm_state, vp_index, sint);
    if (gsi < 0) {
        goto err_gsi;
    }

    r = kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &sint_route->sint_set_notifier,
                                           ack_notifier, gsi);
    if (r) {
        goto err_irqfd;
    }
    sint_route->gsi = gsi;
    sint_route->sint_ack_clb = sint_ack_clb;
    sint_route->sint_ack_clb_data = sint_ack_clb_data;
    sint_route->synic = synic;
    sint_route->sint = sint;
    sint_route->refcount = 1;

    return sint_route;

err_irqfd:
    kvm_irqchip_release_virq(kvm_state, gsi);
err_gsi:
    if (ack_notifier) {
        event_notifier_set_handler(ack_notifier, NULL);
        event_notifier_cleanup(ack_notifier);
    }
err_sint_set_notifier:
    event_notifier_cleanup(&sint_route->sint_set_notifier);
err:
    g_free(sint_route);

    return NULL;
}

void hyperv_sint_route_ref(HvSintRoute *sint_route)
{
    sint_route->refcount++;
}

void hyperv_sint_route_unref(HvSintRoute *sint_route)
{
    if (!sint_route) {
        return;
    }

    assert(sint_route->refcount > 0);

    if (--sint_route->refcount) {
        return;
    }

    kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state,
                                          &sint_route->sint_set_notifier,
                                          sint_route->gsi);
    kvm_irqchip_release_virq(kvm_state, sint_route->gsi);
    if (sint_route->sint_ack_clb) {
        event_notifier_set_handler(&sint_route->sint_ack_notifier, NULL);
        event_notifier_cleanup(&sint_route->sint_ack_notifier);
    }
    event_notifier_cleanup(&sint_route->sint_set_notifier);
    g_free(sint_route);
}

int hyperv_sint_route_set_sint(HvSintRoute *sint_route)
{
    return event_notifier_set(&sint_route->sint_set_notifier);
}
