/*
 * Copyright (c) 2015 FUJITSU LIMITED
 * Author: Yang Hongyang <yanghy@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "net/filter.h"
#include "net/queue.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "qemu/iov.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit.h"
#include "qom/object.h"
#include "net/net.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-input-visitor.h"
#include "monitor/monitor.h"
#include "qmp-commands.h"

#define TYPE_FILTER_BUFFER "filter-buffer"

#define FILTER_BUFFER(obj) \
    OBJECT_CHECK(FilterBufferState, (obj), TYPE_FILTER_BUFFER)

typedef struct FilterBufferState {
    NetFilterState parent_obj;

    NetQueue *incoming_queue;
    uint32_t interval;
    QEMUTimer release_timer;
    bool is_default;
    bool enable_buffer;
} FilterBufferState;

static void filter_buffer_flush(NetFilterState *nf)
{
    FilterBufferState *s = FILTER_BUFFER(nf);

    if (!qemu_net_queue_flush(s->incoming_queue)) {
        /* Unable to empty the queue, purge remaining packets */
        qemu_net_queue_purge(s->incoming_queue, nf->netdev);
    }
}

static void filter_buffer_release_timer(void *opaque)
{
    NetFilterState *nf = opaque;
    FilterBufferState *s = FILTER_BUFFER(nf);

    /*
     * Note: filter_buffer_flush() drops packets that can't be sent
     * TODO: We should leave them queued.  But currently there's no way
     * for the next filter or receiver to notify us that it can receive
     * more packets.
     */
    filter_buffer_flush(nf);
    /* Timer rearmed to fire again in s->interval microseconds. */
    timer_mod(&s->release_timer,
              qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + s->interval);
}

/* filter APIs */
static ssize_t filter_buffer_receive_iov(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    FilterBufferState *s = FILTER_BUFFER(nf);

    /* Don't buffer any packets if the filter is not enabled */
    if (!s->enable_buffer) {
        return 0;
    }
    /*
     * We return size when buffer a packet, the sender will take it as
     * a already sent packet, so sent_cb should not be called later.
     *
     * FIXME: Even if the guest can't receive packets for some reasons,
     * the filter can still accept packets until its internal queue is full.
     * For example:
     *   For some reason, receiver could not receive more packets
     * (.can_receive() returns zero). Without a filter, at most one packet
     * will be queued in incoming queue and sender's poll will be disabled
     * unit its sent_cb() was called. With a filter, it will keep receiving
     * the packets without caring about the receiver. This is suboptimal.
     * May need more thoughts (e.g keeping sent_cb).
     */
    qemu_net_queue_append_iov(s->incoming_queue, sender, flags,
                              iov, iovcnt, NULL);
    return iov_size(iov, iovcnt);
}

static void filter_buffer_cleanup(NetFilterState *nf)
{
    FilterBufferState *s = FILTER_BUFFER(nf);

    if (s->interval) {
        timer_del(&s->release_timer);
    }

    /* flush packets */
    if (s->incoming_queue) {
        filter_buffer_flush(nf);
        g_free(s->incoming_queue);
    }
}

static void filter_buffer_setup(NetFilterState *nf, Error **errp)
{
    FilterBufferState *s = FILTER_BUFFER(nf);
    char *path = object_get_canonical_path_component(OBJECT(nf));

    s->incoming_queue = qemu_new_net_queue(qemu_netfilter_pass_to_next, nf);
    s->is_default = !strcmp(path, "nop");
    if (s->interval) {
        timer_init_us(&s->release_timer, QEMU_CLOCK_VIRTUAL,
                      filter_buffer_release_timer, nf);
        /* Timer armed to fire in s->interval microseconds. */
        timer_mod(&s->release_timer,
                  qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + s->interval);
    }
}

static void filter_buffer_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->setup = filter_buffer_setup;
    nfc->cleanup = filter_buffer_cleanup;
    nfc->receive_iov = filter_buffer_receive_iov;
}

static void filter_buffer_get_interval(Object *obj, Visitor *v, void *opaque,
                                       const char *name, Error **errp)
{
    FilterBufferState *s = FILTER_BUFFER(obj);
    uint32_t value = s->interval;

    visit_type_uint32(v, &value, name, errp);
}

static void filter_buffer_set_interval(Object *obj, Visitor *v, void *opaque,
                                       const char *name, Error **errp)
{
    FilterBufferState *s = FILTER_BUFFER(obj);
    Error *local_err = NULL;
    uint32_t value;

    visit_type_uint32(v, &value, name, &local_err);
    if (local_err) {
        goto out;
    }
    if (!value) {
        error_setg(&local_err, "Property '%s.%s' requires a positive value",
                   object_get_typename(obj), name);
        goto out;
    }
    s->interval = value;

out:
    error_propagate(errp, local_err);
}

static void default_filter_set_buffer_flag(NetFilterState *nf,
                                           void *opaque,
                                           Error **errp)
{
    if (!strcmp(object_get_typename(OBJECT(nf)), TYPE_FILTER_BUFFER)) {
        FilterBufferState *s = FILTER_BUFFER(nf);
        bool *flag = opaque;

        if (s->is_default) {
            s->enable_buffer = *flag;
        }
    }
}

void qemu_set_default_filter_buffers(bool enable_buffer)
{
    qemu_foreach_netfilter(default_filter_set_buffer_flag,
                           &enable_buffer, NULL);
}

/*
* This will be used by COLO or MC FT, for which they will need
* to buffer the packets of VM's net devices, Here we add a default
* buffer filter for each netdev. The name of default buffer filter is
* 'nop'
*/
void netdev_add_default_filter_buffer(const char *netdev_id,
                                      NetFilterDirection direction,
                                      Error **errp)
{
    QmpOutputVisitor *qov;
    QmpInputVisitor *qiv;
    Visitor *ov, *iv;
    QObject *obj = NULL;
    QDict *qdict;
    void *dummy = NULL;
    const char *id = "nop";
    char *queue = g_strdup(NetFilterDirection_lookup[direction]);
    NetClientState *nc = qemu_find_netdev(netdev_id);
    Error *err = NULL;

    /* FIXME: Not support multiple queues */
    if (!nc || nc->queue_index > 1) {
        return;
    }
    qov = qmp_output_visitor_new();
    ov = qmp_output_get_visitor(qov);
    visit_start_struct(ov,  &dummy, NULL, NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_str(ov, &nc->name, "netdev", &err);
    if (err) {
        goto out;
    }
    visit_type_str(ov, &queue, "queue", &err);
    if (err) {
        goto out;
    }
    visit_end_struct(ov, &err);
    if (err) {
        goto out;
    }
    obj = qmp_output_get_qobject(qov);
    g_assert(obj != NULL);
    qdict = qobject_to_qdict(obj);
    qmp_output_visitor_cleanup(qov);

    qiv = qmp_input_visitor_new(obj);
    iv = qmp_input_get_visitor(qiv);
    object_add(TYPE_FILTER_BUFFER, id, qdict, iv, &err);
    qmp_input_visitor_cleanup(qiv);
    qobject_decref(obj);
out:
    g_free(queue);
    if (err) {
        error_propagate(errp, err);
    }
}

static void filter_buffer_init(Object *obj)
{
    object_property_add(obj, "interval", "int",
                        filter_buffer_get_interval,
                        filter_buffer_set_interval, NULL, NULL, NULL);
}

static const TypeInfo filter_buffer_info = {
    .name = TYPE_FILTER_BUFFER,
    .parent = TYPE_NETFILTER,
    .class_init = filter_buffer_class_init,
    .instance_init = filter_buffer_init,
    .instance_size = sizeof(FilterBufferState),
};

static void register_types(void)
{
    type_register_static(&filter_buffer_info);
}

type_init(register_types);
