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

/*
 * KVM has its own message producers (SynIC timers).  To guarantee
 * serialization with both KVM vcpu and the guest cpu, the messages are first
 * staged in an intermediate area and then posted to the SynIC message page in
 * the vcpu thread.
 */
typedef struct HvSintStagedMessage {
    /* message content staged by hyperv_post_msg */
    struct hyperv_message msg;
    /* callback + data (r/o) to complete the processing in a BH */
    HvSintMsgCb cb;
    void *cb_data;
    /* message posting status filled by cpu_post_msg */
    int status;
    /* passing the buck: */
    enum {
        /* initial state */
        HV_STAGED_MSG_FREE,
        /*
         * hyperv_post_msg (e.g. in main loop) grabs the staged area (FREE ->
         * BUSY), copies msg, and schedules cpu_post_msg on the assigned cpu
         */
        HV_STAGED_MSG_BUSY,
        /*
         * cpu_post_msg (vcpu thread) tries to copy staged msg to msg slot,
         * notify the guest, records the status, marks the posting done (BUSY
         * -> POSTED), and schedules sint_msg_bh BH
         */
        HV_STAGED_MSG_POSTED,
        /*
         * sint_msg_bh (BH) verifies that the posting is done, runs the
         * callback, and starts over (POSTED -> FREE)
         */
    } state;
} HvSintStagedMessage;

struct HvSintRoute {
    uint32_t sint;
    SynICState *synic;
    int gsi;
    EventNotifier sint_set_notifier;
    EventNotifier sint_ack_notifier;

    HvSintStagedMessage *staged_msg;

    unsigned refcount;
};

static CPUState *hyperv_find_vcpu(uint32_t vp_index)
{
    CPUState *cs = qemu_get_cpu(vp_index);
    assert(hyperv_vp_index(cs) == vp_index);
    return cs;
}

/*
 * BH to complete the processing of a staged message.
 */
static void sint_msg_bh(void *opaque)
{
    HvSintRoute *sint_route = opaque;
    HvSintStagedMessage *staged_msg = sint_route->staged_msg;

    if (atomic_read(&staged_msg->state) != HV_STAGED_MSG_POSTED) {
        /* status nor ready yet (spurious ack from guest?), ignore */
        return;
    }

    staged_msg->cb(staged_msg->cb_data, staged_msg->status);
    staged_msg->status = 0;

    /* staged message processing finished, ready to start over */
    atomic_set(&staged_msg->state, HV_STAGED_MSG_FREE);
    /* drop the reference taken in hyperv_post_msg */
    hyperv_sint_route_unref(sint_route);
}

/*
 * Worker to transfer the message from the staging area into the SynIC message
 * page in vcpu context.
 */
static void cpu_post_msg(CPUState *cs, run_on_cpu_data data)
{
    HvSintRoute *sint_route = data.host_ptr;
    HvSintStagedMessage *staged_msg = sint_route->staged_msg;
    SynICState *synic = sint_route->synic;
    struct hyperv_message *dst_msg;
    bool wait_for_sint_ack = false;

    assert(staged_msg->state == HV_STAGED_MSG_BUSY);

    if (!synic->enabled || !synic->msg_page_addr) {
        staged_msg->status = -ENXIO;
        goto posted;
    }

    dst_msg = &synic->msg_page->slot[sint_route->sint];

    if (dst_msg->header.message_type != HV_MESSAGE_NONE) {
        dst_msg->header.message_flags |= HV_MESSAGE_FLAG_PENDING;
        staged_msg->status = -EAGAIN;
        wait_for_sint_ack = true;
    } else {
        memcpy(dst_msg, &staged_msg->msg, sizeof(*dst_msg));
        staged_msg->status = hyperv_sint_route_set_sint(sint_route);
    }

    memory_region_set_dirty(&synic->msg_page_mr, 0, sizeof(*synic->msg_page));

posted:
    atomic_set(&staged_msg->state, HV_STAGED_MSG_POSTED);
    /*
     * Notify the msg originator of the progress made; if the slot was busy we
     * set msg_pending flag in it so it will be the guest who will do EOM and
     * trigger the notification from KVM via sint_ack_notifier
     */
    if (!wait_for_sint_ack) {
        aio_bh_schedule_oneshot(qemu_get_aio_context(), sint_msg_bh,
                                sint_route);
    }
}

/*
 * Post a Hyper-V message to the staging area, for delivery to guest in the
 * vcpu thread.
 */
int hyperv_post_msg(HvSintRoute *sint_route, struct hyperv_message *src_msg)
{
    HvSintStagedMessage *staged_msg = sint_route->staged_msg;

    assert(staged_msg);

    /* grab the staging area */
    if (atomic_cmpxchg(&staged_msg->state, HV_STAGED_MSG_FREE,
                       HV_STAGED_MSG_BUSY) != HV_STAGED_MSG_FREE) {
        return -EAGAIN;
    }

    memcpy(&staged_msg->msg, src_msg, sizeof(*src_msg));

    /* hold a reference on sint_route until the callback is finished */
    hyperv_sint_route_ref(sint_route);

    /* schedule message posting attempt in vcpu thread */
    async_run_on_cpu(sint_route->synic->cs, cpu_post_msg,
                     RUN_ON_CPU_HOST_PTR(sint_route));
    return 0;
}

static void sint_ack_handler(EventNotifier *notifier)
{
    HvSintRoute *sint_route = container_of(notifier, HvSintRoute,
                                           sint_ack_notifier);
    event_notifier_test_and_clear(notifier);

    /*
     * the guest consumed the previous message so complete the current one with
     * -EAGAIN and let the msg originator retry
     */
    aio_bh_schedule_oneshot(qemu_get_aio_context(), sint_msg_bh, sint_route);
}

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintMsgCb cb, void *cb_data)
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


    ack_notifier = cb ? &sint_route->sint_ack_notifier : NULL;
    if (ack_notifier) {
        sint_route->staged_msg = g_new0(HvSintStagedMessage, 1);
        sint_route->staged_msg->cb = cb;
        sint_route->staged_msg->cb_data = cb_data;

        r = event_notifier_init(ack_notifier, false);
        if (r) {
            goto err_sint_set_notifier;
        }

        event_notifier_set_handler(ack_notifier, sint_ack_handler);
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
        g_free(sint_route->staged_msg);
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
    if (sint_route->staged_msg) {
        event_notifier_set_handler(&sint_route->sint_ack_notifier, NULL);
        event_notifier_cleanup(&sint_route->sint_ack_notifier);
        g_free(sint_route->staged_msg);
    }
    event_notifier_cleanup(&sint_route->sint_set_notifier);
    g_free(sint_route);
}

int hyperv_sint_route_set_sint(HvSintRoute *sint_route)
{
    return event_notifier_set(&sint_route->sint_set_notifier);
}
