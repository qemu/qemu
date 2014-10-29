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

typedef struct SCLPEventsBus {
    BusState qbus;
} SCLPEventsBus;

struct SCLPEventFacility {
    SysBusDevice parent_obj;
    SCLPEventsBus sbus;
    /* guest' receive mask */
    unsigned int receive_mask;
};

static SCLPEvent cpu_hotplug;

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
            ec->can_handle_event(event_buf->type)) {
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
    unsigned elen;
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
                elen = be16_to_cpu(event_buf->length);
                event_buf = (EventBufferHeader *) ((char *)event_buf + elen);
                rc = SCLP_RC_NORMAL_COMPLETION;
            }
        }
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

static const TypeInfo sclp_events_bus_info = {
    .name = TYPE_SCLP_EVENTS_BUS,
    .parent = TYPE_BUS,
    .class_init = sclp_events_bus_class_init,
};

static void command_handler(SCLPEventFacility *ef, SCCB *sccb, uint64_t code)
{
    switch (code & SCLP_CMD_CODE_MASK) {
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

static const VMStateDescription vmstate_event_facility = {
    .name = "vmstate-event-facility",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(receive_mask, SCLPEventFacility),
        VMSTATE_END_OF_LIST()
     }
};

static int init_event_facility(SCLPEventFacility *event_facility)
{
    DeviceState *sdev = DEVICE(event_facility);
    DeviceState *quiesce;

    /* Spawn a new bus for SCLP events */
    qbus_create_inplace(&event_facility->sbus, sizeof(event_facility->sbus),
                        TYPE_SCLP_EVENTS_BUS, sdev, NULL);

    quiesce = qdev_create(&event_facility->sbus.qbus, "sclpquiesce");
    if (!quiesce) {
        return -1;
    }
    qdev_init_nofail(quiesce);

    object_initialize(&cpu_hotplug, sizeof(cpu_hotplug), TYPE_SCLP_CPU_HOTPLUG);
    qdev_set_parent_bus(DEVICE(&cpu_hotplug), BUS(&event_facility->sbus));
    object_property_set_bool(OBJECT(&cpu_hotplug), true, "realized", NULL);

    return 0;
}

static void reset_event_facility(DeviceState *dev)
{
    SCLPEventFacility *sdev = EVENT_FACILITY(dev);

    sdev->receive_mask = 0;
}

static void init_event_facility_class(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbdc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(sbdc);
    SCLPEventFacilityClass *k = EVENT_FACILITY_CLASS(dc);

    dc->reset = reset_event_facility;
    dc->vmsd = &vmstate_event_facility;
    k->init = init_event_facility;
    k->command_handler = command_handler;
    k->event_pending = event_pending;
}

static const TypeInfo sclp_event_facility_info = {
    .name          = TYPE_SCLP_EVENT_FACILITY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SCLPEventFacility),
    .class_init    = init_event_facility_class,
    .class_size    = sizeof(SCLPEventFacilityClass),
};

static void event_realize(DeviceState *qdev, Error **errp)
{
    SCLPEvent *event = SCLP_EVENT(qdev);
    SCLPEventClass *child = SCLP_EVENT_GET_CLASS(event);

    if (child->init) {
        int rc = child->init(event);
        if (rc < 0) {
            error_setg(errp, "SCLP event initialization failed.");
            return;
        }
    }
}

static void event_unrealize(DeviceState *qdev, Error **errp)
{
    SCLPEvent *event = SCLP_EVENT(qdev);
    SCLPEventClass *child = SCLP_EVENT_GET_CLASS(event);
    if (child->exit) {
        int rc = child->exit(event);
        if (rc < 0) {
            error_setg(errp, "SCLP event exit failed.");
            return;
        }
    }
}

static void event_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_SCLP_EVENTS_BUS;
    dc->realize = event_realize;
    dc->unrealize = event_unrealize;
}

static const TypeInfo sclp_event_type_info = {
    .name = TYPE_SCLP_EVENT,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SCLPEvent),
    .class_init = event_class_init,
    .class_size = sizeof(SCLPEventClass),
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&sclp_events_bus_info);
    type_register_static(&sclp_event_facility_info);
    type_register_static(&sclp_event_type_info);
}

type_init(register_types)
