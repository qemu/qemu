/*
 * SCLP
 *    Event Facility
 *       handles SCLP event types
 *          - Signal Quiesce - system power down
 *          - ASCII Console Data - VT220 read and write
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Heinz Graalfs <graalfs@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "monitor/monitor.h"
#include "sysemu/sysemu.h"

#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"

typedef struct EventTypesBus {
    BusState qbus;
} EventTypesBus;

struct SCLPEventFacility {
    EventTypesBus sbus;
    DeviceState *qdev;
    /* guest' receive mask */
    unsigned int receive_mask;
};

/* return true if any child has event pending set */
static bool event_pending(SCLPEventFacility *ef)
{
    BusChild *kid;
    SCLPEvent *event;
    SCLPEventClass *event_class;

    QTAILQ_FOREACH(kid, &ef->sbus.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        event = DO_UPCAST(SCLPEvent, qdev, qdev);
        event_class = SCLP_EVENT_GET_CLASS(event);
        if (event->event_pending &&
            event_class->get_send_mask() & ef->receive_mask) {
            return true;
        }
    }
    return false;
}

static unsigned int get_host_send_mask(SCLPEventFacility *ef)
{
    unsigned int mask;
    BusChild *kid;
    SCLPEventClass *child;

    mask = 0;

    QTAILQ_FOREACH(kid, &ef->sbus.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        child = SCLP_EVENT_GET_CLASS((SCLPEvent *) qdev);
        mask |= child->get_send_mask();
    }
    return mask;
}

static unsigned int get_host_receive_mask(SCLPEventFacility *ef)
{
    unsigned int mask;
    BusChild *kid;
    SCLPEventClass *child;

    mask = 0;

    QTAILQ_FOREACH(kid, &ef->sbus.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        child = SCLP_EVENT_GET_CLASS((SCLPEvent *) qdev);
        mask |= child->get_receive_mask();
    }
    return mask;
}

static uint16_t write_event_length_check(SCCB *sccb)
{
    int slen;
    unsigned elen = 0;
    EventBufferHeader *event;
    WriteEventData *wed = (WriteEventData *) sccb;

    event = (EventBufferHeader *) &wed->ebh;
    for (slen = sccb_data_len(sccb); slen > 0; slen -= elen) {
        elen = be16_to_cpu(event->length);
        if (elen < sizeof(*event) || elen > slen) {
            return SCLP_RC_EVENT_BUFFER_SYNTAX_ERROR;
        }
        event = (void *) event + elen;
    }
    if (slen) {
        return SCLP_RC_INCONSISTENT_LENGTHS;
    }
    return SCLP_RC_NORMAL_COMPLETION;
}

static uint16_t handle_write_event_buf(SCLPEventFacility *ef,
                                       EventBufferHeader *event_buf, SCCB *sccb)
{
    uint16_t rc;
    BusChild *kid;
    SCLPEvent *event;
    SCLPEventClass *ec;

    rc = SCLP_RC_INVALID_FUNCTION;

    QTAILQ_FOREACH(kid, &ef->sbus.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        event = (SCLPEvent *) qdev;
        ec = SCLP_EVENT_GET_CLASS(event);

        if (ec->write_event_data &&
            ec->event_type() == event_buf->type) {
            rc = ec->write_event_data(event, event_buf);
            break;
        }
    }
    return rc;
}

static uint16_t handle_sccb_write_events(SCLPEventFacility *ef, SCCB *sccb)
{
    uint16_t rc;
    int slen;
    unsigned elen = 0;
    EventBufferHeader *event_buf;
    WriteEventData *wed = (WriteEventData *) sccb;

    event_buf = &wed->ebh;
    rc = SCLP_RC_NORMAL_COMPLETION;

    /* loop over all contained event buffers */
    for (slen = sccb_data_len(sccb); slen > 0; slen -= elen) {
        elen = be16_to_cpu(event_buf->length);

        /* in case of a previous error mark all trailing buffers
         * as not accepted */
        if (rc != SCLP_RC_NORMAL_COMPLETION) {
            event_buf->flags &= ~(SCLP_EVENT_BUFFER_ACCEPTED);
        } else {
            rc = handle_write_event_buf(ef, event_buf, sccb);
        }
        event_buf = (void *) event_buf + elen;
    }
    return rc;
}

static void write_event_data(SCLPEventFacility *ef, SCCB *sccb)
{
    if (sccb->h.function_code != SCLP_FC_NORMAL_WRITE) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_FUNCTION);
        goto out;
    }
    if (be16_to_cpu(sccb->h.length) < 8) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INSUFFICIENT_SCCB_LENGTH);
        goto out;
    }
    /* first do a sanity check of the write events */
    sccb->h.response_code = cpu_to_be16(write_event_length_check(sccb));

    /* if no early error, then execute */
    if (sccb->h.response_code == be16_to_cpu(SCLP_RC_NORMAL_COMPLETION)) {
        sccb->h.response_code =
                cpu_to_be16(handle_sccb_write_events(ef, sccb));
    }

out:
    return;
}

static uint16_t handle_sccb_read_events(SCLPEventFacility *ef, SCCB *sccb,
                                        unsigned int mask)
{
    uint16_t rc;
    int slen;
    unsigned elen = 0;
    BusChild *kid;
    SCLPEvent *event;
    SCLPEventClass *ec;
    EventBufferHeader *event_buf;
    ReadEventData *red = (ReadEventData *) sccb;

    event_buf = &red->ebh;
    event_buf->length = 0;
    slen = sizeof(sccb->data);

    rc = SCLP_RC_NO_EVENT_BUFFERS_STORED;

    QTAILQ_FOREACH(kid, &ef->sbus.qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        event = (SCLPEvent *) qdev;
        ec = SCLP_EVENT_GET_CLASS(event);

        if (mask & ec->get_send_mask()) {
            if (ec->read_event_data(event, event_buf, &slen)) {
                rc = SCLP_RC_NORMAL_COMPLETION;
            }
        }
        elen = be16_to_cpu(event_buf->length);
        event_buf = (void *) event_buf + elen;
    }

    if (sccb->h.control_mask[2] & SCLP_VARIABLE_LENGTH_RESPONSE) {
        /* architecture suggests to reset variable-length-response bit */
        sccb->h.control_mask[2] &= ~SCLP_VARIABLE_LENGTH_RESPONSE;
        /* with a new length value */
        sccb->h.length = cpu_to_be16(SCCB_SIZE - slen);
    }
    return rc;
}

static void read_event_data(SCLPEventFacility *ef, SCCB *sccb)
{
    unsigned int sclp_active_selection_mask;
    unsigned int sclp_cp_receive_mask;

    ReadEventData *red = (ReadEventData *) sccb;

    if (be16_to_cpu(sccb->h.length) != SCCB_SIZE) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INSUFFICIENT_SCCB_LENGTH);
        goto out;
    }

    sclp_cp_receive_mask = ef->receive_mask;

    /* get active selection mask */
    switch (sccb->h.function_code) {
    case SCLP_UNCONDITIONAL_READ:
        sclp_active_selection_mask = sclp_cp_receive_mask;
        break;
    case SCLP_SELECTIVE_READ:
        if (!(sclp_cp_receive_mask & be32_to_cpu(red->mask))) {
            sccb->h.response_code =
                    cpu_to_be16(SCLP_RC_INVALID_SELECTION_MASK);
            goto out;
        }
        sclp_active_selection_mask = be32_to_cpu(red->mask);
        break;
    default:
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_FUNCTION);
        goto out;
    }
    sccb->h.response_code = cpu_to_be16(
            handle_sccb_read_events(ef, sccb, sclp_active_selection_mask));

out:
    return;
}

static void write_event_mask(SCLPEventFacility *ef, SCCB *sccb)
{
    WriteEventMask *we_mask = (WriteEventMask *) sccb;

    /* Attention: We assume that Linux uses 4-byte masks, what it actually
       does. Architecture allows for masks of variable size, though */
    if (be16_to_cpu(we_mask->mask_length) != 4) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_MASK_LENGTH);
        goto out;
    }

    /* keep track of the guest's capability masks */
    ef->receive_mask = be32_to_cpu(we_mask->cp_receive_mask);

    /* return the SCLP's capability masks to the guest */
    we_mask->send_mask = cpu_to_be32(get_host_send_mask(ef));
    we_mask->receive_mask = cpu_to_be32(get_host_receive_mask(ef));

    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_COMPLETION);

out:
    return;
}

/* qemu object creation and initialization functions */

#define TYPE_SCLP_EVENTS_BUS "s390-sclp-events-bus"

static void sclp_events_bus_class_init(ObjectClass *klass, void *data)
{
}

static const TypeInfo s390_sclp_events_bus_info = {
    .name = TYPE_SCLP_EVENTS_BUS,
    .parent = TYPE_BUS,
    .class_init = sclp_events_bus_class_init,
};

static void command_handler(SCLPEventFacility *ef, SCCB *sccb, uint64_t code)
{
    switch (code) {
    case SCLP_CMD_READ_EVENT_DATA:
        read_event_data(ef, sccb);
        break;
    case SCLP_CMD_WRITE_EVENT_DATA:
        write_event_data(ef, sccb);
        break;
    case SCLP_CMD_WRITE_EVENT_MASK:
        write_event_mask(ef, sccb);
        break;
    default:
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        break;
    }
}

static int init_event_facility(S390SCLPDevice *sdev)
{
    SCLPEventFacility *event_facility;
    DeviceState *quiesce;

    event_facility = g_malloc0(sizeof(SCLPEventFacility));
    sdev->ef = event_facility;
    sdev->sclp_command_handler = command_handler;
    sdev->event_pending = event_pending;

    /* Spawn a new sclp-events facility */
    qbus_create_inplace(&event_facility->sbus.qbus,
                        TYPE_SCLP_EVENTS_BUS, (DeviceState *)sdev, NULL);
    event_facility->sbus.qbus.allow_hotplug = 0;
    event_facility->qdev = (DeviceState *) sdev;

    quiesce = qdev_create(&event_facility->sbus.qbus, "sclpquiesce");
    if (!quiesce) {
        return -1;
    }
    qdev_init_nofail(quiesce);

    return 0;
}

static void init_event_facility_class(ObjectClass *klass, void *data)
{
    S390SCLPDeviceClass *k = SCLP_S390_DEVICE_CLASS(klass);

    k->init = init_event_facility;
}

static const TypeInfo s390_sclp_event_facility_info = {
    .name          = "s390-sclp-event-facility",
    .parent        = TYPE_DEVICE_S390_SCLP,
    .instance_size = sizeof(S390SCLPDevice),
    .class_init    = init_event_facility_class,
};

static int event_qdev_init(DeviceState *qdev)
{
    SCLPEvent *event = DO_UPCAST(SCLPEvent, qdev, qdev);
    SCLPEventClass *child = SCLP_EVENT_GET_CLASS(event);

    return child->init(event);
}

static int event_qdev_exit(DeviceState *qdev)
{
    SCLPEvent *event = DO_UPCAST(SCLPEvent, qdev, qdev);
    SCLPEventClass *child = SCLP_EVENT_GET_CLASS(event);
    if (child->exit) {
        child->exit(event);
    }
    return 0;
}

static void event_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_SCLP_EVENTS_BUS;
    dc->unplug = qdev_simple_unplug_cb;
    dc->init = event_qdev_init;
    dc->exit = event_qdev_exit;
}

static const TypeInfo s390_sclp_event_type_info = {
    .name = TYPE_SCLP_EVENT,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SCLPEvent),
    .class_init = event_class_init,
    .class_size = sizeof(SCLPEventClass),
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&s390_sclp_events_bus_info);
    type_register_static(&s390_sclp_event_facility_info);
    type_register_static(&s390_sclp_event_type_info);
}

type_init(register_types)
