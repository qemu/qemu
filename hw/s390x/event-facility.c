/*
 * SCLP
 *    Event Facility
 *       handles SCLP event types
 *          - Signal Quiesce - system power down
 *          - ASCII Console Data - VT220 read and write
 *          - Control-Program Identification - Send OS data from guest to host
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"

#include "hw/s390x/sclp.h"
#include "migration/vmstate.h"
#include "hw/s390x/event-facility.h"

typedef struct SCLPEventsBus {
    BusState qbus;
} SCLPEventsBus;

/* we need to save 32 bit chunks for compatibility */
#if HOST_BIG_ENDIAN
#define RECV_MASK_LOWER 1
#define RECV_MASK_UPPER 0
#else /* little endian host */
#define RECV_MASK_LOWER 0
#define RECV_MASK_UPPER 1
#endif

struct SCLPEventFacility {
    SysBusDevice parent_obj;
    SCLPEventsBus sbus;
    SCLPEvent quiesce, cpu_hotplug;
    SCLPEventCPI cpi;
    /* guest's receive mask */
    union {
        uint32_t receive_mask_pieces[2];
        sccb_mask_t receive_mask;
    };
    /* length of the receive mask */
    uint16_t mask_length;
};

/* return true if any child has event pending set */
static bool event_pending(SCLPEventFacility *ef)
{
    BusChild *kid;
    SCLPEvent *event;
    SCLPEventClass *event_class;

    QTAILQ_FOREACH(kid, &ef->sbus.qbus.children, sibling) {
        event = SCLP_EVENT(kid->child);
        event_class = SCLP_EVENT_GET_CLASS(event);
        if (event->event_pending &&
            event_class->get_send_mask() & ef->receive_mask) {
            return true;
        }
    }
    return false;
}

static sccb_mask_t get_host_send_mask(SCLPEventFacility *ef)
{
    sccb_mask_t mask;
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

static sccb_mask_t get_host_receive_mask(SCLPEventFacility *ef)
{
    sccb_mask_t mask;
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
        return;
    }
    if (be16_to_cpu(sccb->h.length) < 8) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INSUFFICIENT_SCCB_LENGTH);
        return;
    }
    /* first do a sanity check of the write events */
    sccb->h.response_code = cpu_to_be16(write_event_length_check(sccb));

    /* if no early error, then execute */
    if (sccb->h.response_code == be16_to_cpu(SCLP_RC_NORMAL_COMPLETION)) {
        sccb->h.response_code =
                cpu_to_be16(handle_sccb_write_events(ef, sccb));
    }
}

static uint16_t handle_sccb_read_events(SCLPEventFacility *ef, SCCB *sccb,
                                        sccb_mask_t mask)
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
    slen = sccb_data_len(sccb);

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

/* copy up to src_len bytes and fill the rest of dst with zeroes */
static void copy_mask(uint8_t *dst, uint8_t *src, uint16_t dst_len,
                      uint16_t src_len)
{
    int i;

    for (i = 0; i < dst_len; i++) {
        dst[i] = i < src_len ? src[i] : 0;
    }
}

static void read_event_data(SCLPEventFacility *ef, SCCB *sccb)
{
    sccb_mask_t sclp_active_selection_mask;
    sccb_mask_t sclp_cp_receive_mask;

    ReadEventData *red = (ReadEventData *) sccb;

    if (be16_to_cpu(sccb->h.length) != SCCB_SIZE) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INSUFFICIENT_SCCB_LENGTH);
        return;
    }

    switch (sccb->h.function_code) {
    case SCLP_UNCONDITIONAL_READ:
        sccb->h.response_code = cpu_to_be16(
            handle_sccb_read_events(ef, sccb, ef->receive_mask));
        break;
    case SCLP_SELECTIVE_READ:
        /* get active selection mask */
        sclp_cp_receive_mask = ef->receive_mask;

        copy_mask((uint8_t *)&sclp_active_selection_mask, (uint8_t *)&red->mask,
                  sizeof(sclp_active_selection_mask), ef->mask_length);
        sclp_active_selection_mask = be64_to_cpu(sclp_active_selection_mask);
        if (!sclp_cp_receive_mask ||
            (sclp_active_selection_mask & ~sclp_cp_receive_mask)) {
            sccb->h.response_code =
                    cpu_to_be16(SCLP_RC_INVALID_SELECTION_MASK);
        } else {
            sccb->h.response_code = cpu_to_be16(
                handle_sccb_read_events(ef, sccb, sclp_active_selection_mask));
        }
        break;
    default:
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_FUNCTION);
    }
}

static void write_event_mask(SCLPEventFacility *ef, SCCB *sccb)
{
    WriteEventMask *we_mask = (WriteEventMask *) sccb;
    uint16_t mask_length = be16_to_cpu(we_mask->mask_length);
    sccb_mask_t tmp_mask;

    if (!mask_length || mask_length > SCLP_EVENT_MASK_LEN_MAX) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_MASK_LENGTH);
        return;
    }

    /*
     * Note: We currently only support masks up to 8 byte length;
     *       the remainder is filled up with zeroes. Older Linux
     *       kernels use a 4 byte mask length, newer ones can use both
     *       8 or 4 depending on what is available on the host.
     */

    /* keep track of the guest's capability masks */
    copy_mask((uint8_t *)&tmp_mask, WEM_CP_RECEIVE_MASK(we_mask, mask_length),
              sizeof(tmp_mask), mask_length);
    ef->receive_mask = be64_to_cpu(tmp_mask);

    /* return the SCLP's capability masks to the guest */
    tmp_mask = cpu_to_be64(get_host_receive_mask(ef));
    copy_mask(WEM_RECEIVE_MASK(we_mask, mask_length), (uint8_t *)&tmp_mask,
              mask_length, sizeof(tmp_mask));
    tmp_mask = cpu_to_be64(get_host_send_mask(ef));
    copy_mask(WEM_SEND_MASK(we_mask, mask_length), (uint8_t *)&tmp_mask,
              mask_length, sizeof(tmp_mask));

    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_COMPLETION);
    ef->mask_length = mask_length;
}

/* qemu object creation and initialization functions */

#define TYPE_SCLP_EVENTS_BUS "s390-sclp-events-bus"

static const TypeInfo sclp_events_bus_info = {
    .name = TYPE_SCLP_EVENTS_BUS,
    .parent = TYPE_BUS,
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
    }
}

static bool vmstate_event_facility_mask64_needed(void *opaque)
{
    SCLPEventFacility *ef = opaque;

    return (ef->receive_mask & 0xFFFFFFFF) != 0;
}

static const VMStateDescription vmstate_event_facility_mask64 = {
    .name = "vmstate-event-facility/mask64",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = vmstate_event_facility_mask64_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(receive_mask_pieces[RECV_MASK_LOWER], SCLPEventFacility),
        VMSTATE_END_OF_LIST()
     }
};

static const VMStateDescription vmstate_event_facility_mask_length = {
    .name = "vmstate-event-facility/mask_length",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(mask_length, SCLPEventFacility),
        VMSTATE_END_OF_LIST()
     }
};

static const VMStateDescription vmstate_event_facility = {
    .name = "vmstate-event-facility",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(receive_mask_pieces[RECV_MASK_UPPER], SCLPEventFacility),
        VMSTATE_END_OF_LIST()
     },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_event_facility_mask64,
        &vmstate_event_facility_mask_length,
        NULL
     }
};

static void init_event_facility(Object *obj)
{
    SCLPEventFacility *event_facility = EVENT_FACILITY(obj);
    DeviceState *sdev = DEVICE(obj);

    event_facility->mask_length = 4;

    /* Spawn a new bus for SCLP events */
    qbus_init(&event_facility->sbus, sizeof(event_facility->sbus),
              TYPE_SCLP_EVENTS_BUS, sdev, NULL);

    object_initialize_child(obj, TYPE_SCLP_QUIESCE,
                            &event_facility->quiesce,
                            TYPE_SCLP_QUIESCE);

    object_initialize_child(obj, TYPE_SCLP_CPU_HOTPLUG,
                            &event_facility->cpu_hotplug,
                            TYPE_SCLP_CPU_HOTPLUG);
}

static void realize_event_facility(DeviceState *dev, Error **errp)
{
    SCLPEventFacility *event_facility = EVENT_FACILITY(dev);

    if (!qdev_realize(DEVICE(&event_facility->quiesce),
                      BUS(&event_facility->sbus), errp)) {
        return;
    }
    if (!qdev_realize(DEVICE(&event_facility->cpu_hotplug),
                      BUS(&event_facility->sbus), errp)) {
        qdev_unrealize(DEVICE(&event_facility->quiesce));
        return;
    }
}

static void reset_event_facility(DeviceState *dev)
{
    SCLPEventFacility *sdev = EVENT_FACILITY(dev);

    sdev->receive_mask = 0;
}

static void init_event_facility_class(ObjectClass *klass, const void *data)
{
    SysBusDeviceClass *sbdc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(sbdc);
    SCLPEventFacilityClass *k = EVENT_FACILITY_CLASS(dc);

    dc->realize = realize_event_facility;
    device_class_set_legacy_reset(dc, reset_event_facility);
    dc->vmsd = &vmstate_event_facility;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    k->command_handler = command_handler;
    k->event_pending = event_pending;
}

static const TypeInfo sclp_event_facility_info = {
    .name          = TYPE_SCLP_EVENT_FACILITY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = init_event_facility,
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

static void event_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_SCLP_EVENTS_BUS;
    dc->realize = event_realize;
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

BusState *sclp_get_event_facility_bus(SCLPEventFacility *ef)
{
    return BUS(&ef->sbus);
}
