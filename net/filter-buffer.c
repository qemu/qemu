/*
 * Copyright (c) 2015 FUJITSU LIMITED
 * Author: Yang Hongyang <yanghy@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "net/filter.h"
#include "net/queue.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "qemu/iov.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit.h"
#include "qom/object.h"

#define TYPE_FILTER_BUFFER "filter-buffer"

#define FILTER_BUFFER(obj) \
    OBJECT_CHECK(FilterBufferState, (obj), TYPE_FILTER_BUFFER)

typedef struct FilterBufferState {
    NetFilterState parent_obj;

    NetQueue *incoming_queue;
    uint32_t interval;
    QEMUTimer release_timer;
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

static void filter_buffer_setup_timer(NetFilterState *nf)
{
    FilterBufferState *s = FILTER_BUFFER(nf);

    if (s->interval) {
        timer_init_us(&s->release_timer, QEMU_CLOCK_VIRTUAL,
                      filter_buffer_release_timer, nf);
        /* Timer armed to fire in s->interval microseconds. */
        timer_mod(&s->release_timer,
                  qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + s->interval);
    }
}

static void filter_buffer_setup(NetFilterState *nf, Error **errp)
{
    FilterBufferState *s = FILTER_BUFFER(nf);

    /*
     * We may want to accept zero interval when VM FT solutions like MC
     * or COLO use this filter to release packets on demand.
     */
    if (!s->interval) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "interval",
                   "a non-zero interval");
        return;
    }

    s->incoming_queue = qemu_new_net_queue(qemu_netfilter_pass_to_next, nf);
    filter_buffer_setup_timer(nf);
}

static void filter_buffer_status_changed(NetFilterState *nf, Error **errp)
{
    FilterBufferState *s = FILTER_BUFFER(nf);

    if (!nf->on) {
        if (s->interval) {
            timer_del(&s->release_timer);
        }
        filter_buffer_flush(nf);
    } else {
        filter_buffer_setup_timer(nf);
    }
}

static void filter_buffer_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->setup = filter_buffer_setup;
    nfc->cleanup = filter_buffer_cleanup;
    nfc->receive_iov = filter_buffer_receive_iov;
    nfc->status_changed = filter_buffer_status_changed;
}

static void filter_buffer_get_interval(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    FilterBufferState *s = FILTER_BUFFER(obj);
    uint32_t value = s->interval;

    visit_type_uint32(v, name, &value, errp);
}

static void filter_buffer_set_interval(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    FilterBufferState *s = FILTER_BUFFER(obj);
    Error *local_err = NULL;
    uint32_t value;

    visit_type_uint32(v, name, &value, &local_err);
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
