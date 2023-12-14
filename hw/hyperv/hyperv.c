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
#include "qemu/module.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "sysemu/kvm.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/lockable.h"
#include "qemu/queue.h"
#include "qemu/rcu.h"
#include "qemu/rcu_queue.h"
#include "hw/hyperv/hyperv.h"
#include "qom/object.h"

struct SynICState {
    DeviceState parent_obj;

    CPUState *cs;

    bool sctl_enabled;
    hwaddr msg_page_addr;
    hwaddr event_page_addr;
    MemoryRegion msg_page_mr;
    MemoryRegion event_page_mr;
    struct hyperv_message_page *msg_page;
    struct hyperv_event_flags_page *event_page;

    QemuMutex sint_routes_mutex;
    QLIST_HEAD(, HvSintRoute) sint_routes;
};

#define TYPE_SYNIC "hyperv-synic"
OBJECT_DECLARE_SIMPLE_TYPE(SynICState, SYNIC)

static bool synic_enabled;

bool hyperv_is_synic_enabled(void)
{
    return synic_enabled;
}

static SynICState *get_synic(CPUState *cs)
{
    return SYNIC(object_resolve_path_component(OBJECT(cs), "synic"));
}

static void synic_update(SynICState *synic, bool sctl_enable,
                         hwaddr msg_page_addr, hwaddr event_page_addr)
{

    synic->sctl_enabled = sctl_enable;
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

void hyperv_synic_update(CPUState *cs, bool sctl_enable,
                         hwaddr msg_page_addr, hwaddr event_page_addr)
{
    SynICState *synic = get_synic(cs);

    if (!synic) {
        return;
    }

    synic_update(synic, sctl_enable, msg_page_addr, event_page_addr);
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
    qemu_mutex_init(&synic->sint_routes_mutex);
    QLIST_INIT(&synic->sint_routes);

    g_free(msgp_name);
    g_free(eventp_name);
}

static void synic_reset(DeviceState *dev)
{
    SynICState *synic = SYNIC(dev);
    memset(synic->msg_page, 0, sizeof(*synic->msg_page));
    memset(synic->event_page, 0, sizeof(*synic->event_page));
    synic_update(synic, false, 0, 0);
    assert(QLIST_EMPTY(&synic->sint_routes));
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
    object_property_add_child(OBJECT(cs), "synic", obj);
    object_unref(obj);
    qdev_realize(DEVICE(obj), NULL, &error_abort);
    synic_enabled = true;
}

void hyperv_synic_reset(CPUState *cs)
{
    SynICState *synic = get_synic(cs);

    if (synic) {
        device_cold_reset(DEVICE(synic));
    }
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
    QLIST_ENTRY(HvSintRoute) link;
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

    if (qatomic_read(&staged_msg->state) != HV_STAGED_MSG_POSTED) {
        /* status nor ready yet (spurious ack from guest?), ignore */
        return;
    }

    staged_msg->cb(staged_msg->cb_data, staged_msg->status);
    staged_msg->status = 0;

    /* staged message processing finished, ready to start over */
    qatomic_set(&staged_msg->state, HV_STAGED_MSG_FREE);
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

    if (!synic->msg_page_addr) {
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
    qatomic_set(&staged_msg->state, HV_STAGED_MSG_POSTED);
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
    if (qatomic_cmpxchg(&staged_msg->state, HV_STAGED_MSG_FREE,
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

/*
 * Set given event flag for a given sint on a given vcpu, and signal the sint.
 */
int hyperv_set_event_flag(HvSintRoute *sint_route, unsigned eventno)
{
    int ret;
    SynICState *synic = sint_route->synic;
    unsigned long *flags, set_mask;
    unsigned set_idx;

    if (eventno > HV_EVENT_FLAGS_COUNT) {
        return -EINVAL;
    }
    if (!synic->sctl_enabled || !synic->event_page_addr) {
        return -ENXIO;
    }

    set_idx = BIT_WORD(eventno);
    set_mask = BIT_MASK(eventno);
    flags = synic->event_page->slot[sint_route->sint].flags;

    if ((qatomic_fetch_or(&flags[set_idx], set_mask) & set_mask) != set_mask) {
        memory_region_set_dirty(&synic->event_page_mr, 0,
                                sizeof(*synic->event_page));
        ret = hyperv_sint_route_set_sint(sint_route);
    } else {
        ret = 0;
    }
    return ret;
}

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintMsgCb cb, void *cb_data)
{
    HvSintRoute *sint_route = NULL;
    EventNotifier *ack_notifier = NULL;
    int r, gsi;
    CPUState *cs;
    SynICState *synic;
    bool ack_event_initialized = false;

    cs = hyperv_find_vcpu(vp_index);
    if (!cs) {
        return NULL;
    }

    synic = get_synic(cs);
    if (!synic) {
        return NULL;
    }

    sint_route = g_new0(HvSintRoute, 1);
    if (!sint_route) {
        return NULL;
    }

    sint_route->synic = synic;
    sint_route->sint = sint;
    sint_route->refcount = 1;

    ack_notifier = cb ? &sint_route->sint_ack_notifier : NULL;
    if (ack_notifier) {
        sint_route->staged_msg = g_new0(HvSintStagedMessage, 1);
        if (!sint_route->staged_msg) {
            goto cleanup_err_sint;
        }
        sint_route->staged_msg->cb = cb;
        sint_route->staged_msg->cb_data = cb_data;

        r = event_notifier_init(ack_notifier, false);
        if (r) {
            goto cleanup_err_sint;
        }
        event_notifier_set_handler(ack_notifier, sint_ack_handler);
        ack_event_initialized = true;
    }

    /* See if we are done or we need to setup a GSI for this SintRoute */
    if (!synic->sctl_enabled) {
        goto cleanup;
    }

    /* We need to setup a GSI for this SintRoute */
    r = event_notifier_init(&sint_route->sint_set_notifier, false);
    if (r) {
        goto cleanup_err_sint;
    }

    gsi = kvm_irqchip_add_hv_sint_route(kvm_state, vp_index, sint);
    if (gsi < 0) {
        goto cleanup_err_sint_notifier;
    }

    r = kvm_irqchip_add_irqfd_notifier_gsi(kvm_state,
                                           &sint_route->sint_set_notifier,
                                           ack_notifier, gsi);
    if (r) {
        goto cleanup_err_irqfd;
    }
    sint_route->gsi = gsi;
cleanup:
    qemu_mutex_lock(&synic->sint_routes_mutex);
    QLIST_INSERT_HEAD(&synic->sint_routes, sint_route, link);
    qemu_mutex_unlock(&synic->sint_routes_mutex);
    return sint_route;

cleanup_err_irqfd:
    kvm_irqchip_release_virq(kvm_state, gsi);

cleanup_err_sint_notifier:
    event_notifier_cleanup(&sint_route->sint_set_notifier);

cleanup_err_sint:
    if (ack_notifier) {
        if (ack_event_initialized) {
            event_notifier_set_handler(ack_notifier, NULL);
            event_notifier_cleanup(ack_notifier);
        }

        g_free(sint_route->staged_msg);
    }

    g_free(sint_route);
    return NULL;
}

void hyperv_sint_route_ref(HvSintRoute *sint_route)
{
    sint_route->refcount++;
}

void hyperv_sint_route_unref(HvSintRoute *sint_route)
{
    SynICState *synic;

    if (!sint_route) {
        return;
    }

    assert(sint_route->refcount > 0);

    if (--sint_route->refcount) {
        return;
    }

    synic = sint_route->synic;
    qemu_mutex_lock(&synic->sint_routes_mutex);
    QLIST_REMOVE(sint_route, link);
    qemu_mutex_unlock(&synic->sint_routes_mutex);

    if (sint_route->gsi) {
        kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state,
                                              &sint_route->sint_set_notifier,
                                              sint_route->gsi);
        kvm_irqchip_release_virq(kvm_state, sint_route->gsi);
        event_notifier_cleanup(&sint_route->sint_set_notifier);
    }

    if (sint_route->staged_msg) {
        event_notifier_set_handler(&sint_route->sint_ack_notifier, NULL);
        event_notifier_cleanup(&sint_route->sint_ack_notifier);
        g_free(sint_route->staged_msg);
    }
    g_free(sint_route);
}

int hyperv_sint_route_set_sint(HvSintRoute *sint_route)
{
    if (!sint_route->gsi) {
        return 0;
    }

    return event_notifier_set(&sint_route->sint_set_notifier);
}

typedef struct MsgHandler {
    struct rcu_head rcu;
    QLIST_ENTRY(MsgHandler) link;
    uint32_t conn_id;
    HvMsgHandler handler;
    void *data;
} MsgHandler;

typedef struct EventFlagHandler {
    struct rcu_head rcu;
    QLIST_ENTRY(EventFlagHandler) link;
    uint32_t conn_id;
    EventNotifier *notifier;
} EventFlagHandler;

static QLIST_HEAD(, MsgHandler) msg_handlers;
static QLIST_HEAD(, EventFlagHandler) event_flag_handlers;
static QemuMutex handlers_mutex;

static void __attribute__((constructor)) hv_init(void)
{
    QLIST_INIT(&msg_handlers);
    QLIST_INIT(&event_flag_handlers);
    qemu_mutex_init(&handlers_mutex);
}

int hyperv_set_msg_handler(uint32_t conn_id, HvMsgHandler handler, void *data)
{
    int ret;
    MsgHandler *mh;

    QEMU_LOCK_GUARD(&handlers_mutex);
    QLIST_FOREACH(mh, &msg_handlers, link) {
        if (mh->conn_id == conn_id) {
            if (handler) {
                ret = -EEXIST;
            } else {
                QLIST_REMOVE_RCU(mh, link);
                g_free_rcu(mh, rcu);
                ret = 0;
            }
            return ret;
        }
    }

    if (handler) {
        mh = g_new(MsgHandler, 1);
        mh->conn_id = conn_id;
        mh->handler = handler;
        mh->data = data;
        QLIST_INSERT_HEAD_RCU(&msg_handlers, mh, link);
        ret = 0;
    } else {
        ret = -ENOENT;
    }

    return ret;
}

uint16_t hyperv_hcall_post_message(uint64_t param, bool fast)
{
    uint16_t ret;
    hwaddr len;
    struct hyperv_post_message_input *msg;
    MsgHandler *mh;

    if (fast) {
        return HV_STATUS_INVALID_HYPERCALL_CODE;
    }
    if (param & (__alignof__(*msg) - 1)) {
        return HV_STATUS_INVALID_ALIGNMENT;
    }

    len = sizeof(*msg);
    msg = cpu_physical_memory_map(param, &len, 0);
    if (len < sizeof(*msg)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto unmap;
    }
    if (msg->payload_size > sizeof(msg->payload)) {
        ret = HV_STATUS_INVALID_HYPERCALL_INPUT;
        goto unmap;
    }

    ret = HV_STATUS_INVALID_CONNECTION_ID;
    WITH_RCU_READ_LOCK_GUARD() {
        QLIST_FOREACH_RCU(mh, &msg_handlers, link) {
            if (mh->conn_id == (msg->connection_id & HV_CONNECTION_ID_MASK)) {
                ret = mh->handler(msg, mh->data);
                break;
            }
        }
    }

unmap:
    cpu_physical_memory_unmap(msg, len, 0, 0);
    return ret;
}

static int set_event_flag_handler(uint32_t conn_id, EventNotifier *notifier)
{
    int ret;
    EventFlagHandler *handler;

    QEMU_LOCK_GUARD(&handlers_mutex);
    QLIST_FOREACH(handler, &event_flag_handlers, link) {
        if (handler->conn_id == conn_id) {
            if (notifier) {
                ret = -EEXIST;
            } else {
                QLIST_REMOVE_RCU(handler, link);
                g_free_rcu(handler, rcu);
                ret = 0;
            }
            return ret;
        }
    }

    if (notifier) {
        handler = g_new(EventFlagHandler, 1);
        handler->conn_id = conn_id;
        handler->notifier = notifier;
        QLIST_INSERT_HEAD_RCU(&event_flag_handlers, handler, link);
        ret = 0;
    } else {
        ret = -ENOENT;
    }

    return ret;
}

static bool process_event_flags_userspace;

int hyperv_set_event_flag_handler(uint32_t conn_id, EventNotifier *notifier)
{
    if (!process_event_flags_userspace &&
        !kvm_check_extension(kvm_state, KVM_CAP_HYPERV_EVENTFD)) {
        process_event_flags_userspace = true;

        warn_report("Hyper-V event signaling is not supported by this kernel; "
                    "using slower userspace hypercall processing");
    }

    if (!process_event_flags_userspace) {
        struct kvm_hyperv_eventfd hvevfd = {
            .conn_id = conn_id,
            .fd = notifier ? event_notifier_get_fd(notifier) : -1,
            .flags = notifier ? 0 : KVM_HYPERV_EVENTFD_DEASSIGN,
        };

        return kvm_vm_ioctl(kvm_state, KVM_HYPERV_EVENTFD, &hvevfd);
    }
    return set_event_flag_handler(conn_id, notifier);
}

uint16_t hyperv_hcall_signal_event(uint64_t param, bool fast)
{
    EventFlagHandler *handler;

    if (unlikely(!fast)) {
        hwaddr addr = param;

        if (addr & (__alignof__(addr) - 1)) {
            return HV_STATUS_INVALID_ALIGNMENT;
        }

        param = ldq_phys(&address_space_memory, addr);
    }

    /*
     * Per spec, bits 32-47 contain the extra "flag number".  However, we
     * have no use for it, and in all known usecases it is zero, so just
     * report lookup failure if it isn't.
     */
    if (param & 0xffff00000000ULL) {
        return HV_STATUS_INVALID_PORT_ID;
    }
    /* remaining bits are reserved-zero */
    if (param & ~HV_CONNECTION_ID_MASK) {
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }

    RCU_READ_LOCK_GUARD();
    QLIST_FOREACH_RCU(handler, &event_flag_handlers, link) {
        if (handler->conn_id == param) {
            event_notifier_set(handler->notifier);
            return 0;
        }
    }
    return HV_STATUS_INVALID_CONNECTION_ID;
}

static HvSynDbgHandler hv_syndbg_handler;
static void *hv_syndbg_context;

void hyperv_set_syndbg_handler(HvSynDbgHandler handler, void *context)
{
    assert(!hv_syndbg_handler);
    hv_syndbg_handler = handler;
    hv_syndbg_context = context;
}

uint16_t hyperv_hcall_reset_dbg_session(uint64_t outgpa)
{
    uint16_t ret;
    HvSynDbgMsg msg;
    struct hyperv_reset_debug_session_output *reset_dbg_session = NULL;
    hwaddr len;

    if (!hv_syndbg_handler) {
        ret = HV_STATUS_INVALID_HYPERCALL_CODE;
        goto cleanup;
    }

    len = sizeof(*reset_dbg_session);
    reset_dbg_session = cpu_physical_memory_map(outgpa, &len, 1);
    if (!reset_dbg_session || len < sizeof(*reset_dbg_session)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    msg.type = HV_SYNDBG_MSG_CONNECTION_INFO;
    ret = hv_syndbg_handler(hv_syndbg_context, &msg);
    if (ret) {
        goto cleanup;
    }

    reset_dbg_session->host_ip = msg.u.connection_info.host_ip;
    reset_dbg_session->host_port = msg.u.connection_info.host_port;
    /* The following fields are only used as validation for KDVM */
    memset(&reset_dbg_session->host_mac, 0,
           sizeof(reset_dbg_session->host_mac));
    reset_dbg_session->target_ip = msg.u.connection_info.host_ip;
    reset_dbg_session->target_port = msg.u.connection_info.host_port;
    memset(&reset_dbg_session->target_mac, 0,
           sizeof(reset_dbg_session->target_mac));
cleanup:
    if (reset_dbg_session) {
        cpu_physical_memory_unmap(reset_dbg_session,
                                  sizeof(*reset_dbg_session), 1, len);
    }

    return ret;
}

uint16_t hyperv_hcall_retreive_dbg_data(uint64_t ingpa, uint64_t outgpa,
                                        bool fast)
{
    uint16_t ret;
    struct hyperv_retrieve_debug_data_input *debug_data_in = NULL;
    struct hyperv_retrieve_debug_data_output *debug_data_out = NULL;
    hwaddr in_len, out_len;
    HvSynDbgMsg msg;

    if (fast || !hv_syndbg_handler) {
        ret = HV_STATUS_INVALID_HYPERCALL_CODE;
        goto cleanup;
    }

    in_len = sizeof(*debug_data_in);
    debug_data_in = cpu_physical_memory_map(ingpa, &in_len, 0);
    if (!debug_data_in || in_len < sizeof(*debug_data_in)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    out_len = sizeof(*debug_data_out);
    debug_data_out = cpu_physical_memory_map(outgpa, &out_len, 1);
    if (!debug_data_out || out_len < sizeof(*debug_data_out)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    msg.type = HV_SYNDBG_MSG_RECV;
    msg.u.recv.buf_gpa = outgpa + sizeof(*debug_data_out);
    msg.u.recv.count = TARGET_PAGE_SIZE - sizeof(*debug_data_out);
    msg.u.recv.options = debug_data_in->options;
    msg.u.recv.timeout = debug_data_in->timeout;
    msg.u.recv.is_raw = true;
    ret = hv_syndbg_handler(hv_syndbg_context, &msg);
    if (ret == HV_STATUS_NO_DATA) {
        debug_data_out->retrieved_count = 0;
        debug_data_out->remaining_count = debug_data_in->count;
        goto cleanup;
    } else if (ret != HV_STATUS_SUCCESS) {
        goto cleanup;
    }

    debug_data_out->retrieved_count = msg.u.recv.retrieved_count;
    debug_data_out->remaining_count =
        debug_data_in->count - msg.u.recv.retrieved_count;
cleanup:
    if (debug_data_out) {
        cpu_physical_memory_unmap(debug_data_out, sizeof(*debug_data_out), 1,
                                  out_len);
    }

    if (debug_data_in) {
        cpu_physical_memory_unmap(debug_data_in, sizeof(*debug_data_in), 0,
                                  in_len);
    }

    return ret;
}

uint16_t hyperv_hcall_post_dbg_data(uint64_t ingpa, uint64_t outgpa, bool fast)
{
    uint16_t ret;
    struct hyperv_post_debug_data_input *post_data_in = NULL;
    struct hyperv_post_debug_data_output *post_data_out = NULL;
    hwaddr in_len, out_len;
    HvSynDbgMsg msg;

    if (fast || !hv_syndbg_handler) {
        ret = HV_STATUS_INVALID_HYPERCALL_CODE;
        goto cleanup;
    }

    in_len = sizeof(*post_data_in);
    post_data_in = cpu_physical_memory_map(ingpa, &in_len, 0);
    if (!post_data_in || in_len < sizeof(*post_data_in)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    if (post_data_in->count > TARGET_PAGE_SIZE - sizeof(*post_data_in)) {
        ret = HV_STATUS_INVALID_PARAMETER;
        goto cleanup;
    }

    out_len = sizeof(*post_data_out);
    post_data_out = cpu_physical_memory_map(outgpa, &out_len, 1);
    if (!post_data_out || out_len < sizeof(*post_data_out)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    msg.type = HV_SYNDBG_MSG_SEND;
    msg.u.send.buf_gpa = ingpa + sizeof(*post_data_in);
    msg.u.send.count = post_data_in->count;
    msg.u.send.is_raw = true;
    ret = hv_syndbg_handler(hv_syndbg_context, &msg);
    if (ret != HV_STATUS_SUCCESS) {
        goto cleanup;
    }

    post_data_out->pending_count = msg.u.send.pending_count;
    ret = post_data_out->pending_count ? HV_STATUS_INSUFFICIENT_BUFFERS :
                                         HV_STATUS_SUCCESS;
cleanup:
    if (post_data_out) {
        cpu_physical_memory_unmap(post_data_out,
                                  sizeof(*post_data_out), 1, out_len);
    }

    if (post_data_in) {
        cpu_physical_memory_unmap(post_data_in,
                                  sizeof(*post_data_in), 0, in_len);
    }

    return ret;
}

uint32_t hyperv_syndbg_send(uint64_t ingpa, uint32_t count)
{
    HvSynDbgMsg msg;

    if (!hv_syndbg_handler) {
        return HV_SYNDBG_STATUS_INVALID;
    }

    msg.type = HV_SYNDBG_MSG_SEND;
    msg.u.send.buf_gpa = ingpa;
    msg.u.send.count = count;
    msg.u.send.is_raw = false;
    if (hv_syndbg_handler(hv_syndbg_context, &msg)) {
        return HV_SYNDBG_STATUS_INVALID;
    }

    return HV_SYNDBG_STATUS_SEND_SUCCESS;
}

uint32_t hyperv_syndbg_recv(uint64_t ingpa, uint32_t count)
{
    uint16_t ret;
    HvSynDbgMsg msg;

    if (!hv_syndbg_handler) {
        return HV_SYNDBG_STATUS_INVALID;
    }

    msg.type = HV_SYNDBG_MSG_RECV;
    msg.u.recv.buf_gpa = ingpa;
    msg.u.recv.count = count;
    msg.u.recv.options = 0;
    msg.u.recv.timeout = 0;
    msg.u.recv.is_raw = false;
    ret = hv_syndbg_handler(hv_syndbg_context, &msg);
    if (ret != HV_STATUS_SUCCESS) {
        return 0;
    }

    return HV_SYNDBG_STATUS_SET_SIZE(HV_SYNDBG_STATUS_RECV_SUCCESS,
                                     msg.u.recv.retrieved_count);
}

void hyperv_syndbg_set_pending_page(uint64_t ingpa)
{
    HvSynDbgMsg msg;

    if (!hv_syndbg_handler) {
        return;
    }

    msg.type = HV_SYNDBG_MSG_SET_PENDING_PAGE;
    msg.u.pending_page.buf_gpa = ingpa;
    hv_syndbg_handler(hv_syndbg_context, &msg);
}

uint64_t hyperv_syndbg_query_options(void)
{
    HvSynDbgMsg msg;

    if (!hv_syndbg_handler) {
        return 0;
    }

    msg.type = HV_SYNDBG_MSG_QUERY_OPTIONS;
    if (hv_syndbg_handler(hv_syndbg_context, &msg) != HV_STATUS_SUCCESS) {
        return 0;
    }

    return msg.u.query_options.options;
}
