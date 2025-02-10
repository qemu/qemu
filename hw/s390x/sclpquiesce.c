/*
 * SCLP event type
 *    Signal Quiesce - trigger system powerdown request
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
#include "hw/s390x/sclp.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "system/runstate.h"
#include "hw/s390x/event-facility.h"

typedef struct SignalQuiesce {
    EventBufferHeader ebh;
    uint16_t timeout;
    uint8_t unit;
} QEMU_PACKED SignalQuiesce;

static bool can_handle_event(uint8_t type)
{
    return type == SCLP_EVENT_SIGNAL_QUIESCE;
}

static sccb_mask_t send_mask(void)
{
    return SCLP_EVENT_MASK_SIGNAL_QUIESCE;
}

static sccb_mask_t receive_mask(void)
{
    return 0;
}

static int read_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr,
                           int *slen)
{
    SignalQuiesce *sq = (SignalQuiesce *) evt_buf_hdr;

    if (*slen < sizeof(SignalQuiesce)) {
        return 0;
    }

    if (!event->event_pending) {
        return 0;
    }
    event->event_pending = false;

    sq->ebh.length = cpu_to_be16(sizeof(SignalQuiesce));
    sq->ebh.type = SCLP_EVENT_SIGNAL_QUIESCE;
    sq->ebh.flags |= SCLP_EVENT_BUFFER_ACCEPTED;
    /*
     * system_powerdown does not have a timeout. Fortunately the
     * timeout value is currently ignored by Linux, anyway
     */
    sq->timeout = cpu_to_be16(0);
    sq->unit = cpu_to_be16(0);
    *slen -= sizeof(SignalQuiesce);

    return 1;
}

static const VMStateDescription vmstate_sclpquiesce = {
    .name = TYPE_SCLP_QUIESCE,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(event_pending, SCLPEvent),
        VMSTATE_END_OF_LIST()
     }
};

typedef struct QuiesceNotifier {
    Notifier notifier;
    SCLPEvent *event;
} QuiesceNotifier;

static void quiesce_powerdown_req(Notifier *n, void *opaque)
{
    QuiesceNotifier *qn = container_of(n, QuiesceNotifier, notifier);
    SCLPEvent *event = qn->event;

    event->event_pending = true;
    /* trigger SCLP read operation */
    sclp_service_interrupt(0);
}

static int quiesce_init(SCLPEvent *event)
{
    static QuiesceNotifier qn;

    qn.notifier.notify = quiesce_powerdown_req;
    qn.event = event;

    qemu_register_powerdown_notifier(&qn.notifier);

    return 0;
}

static void quiesce_reset(DeviceState *dev)
{
   SCLPEvent *event = SCLP_EVENT(dev);

   event->event_pending = false;
}

static void quiesce_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCLPEventClass *k = SCLP_EVENT_CLASS(klass);

    device_class_set_legacy_reset(dc, quiesce_reset);
    dc->vmsd = &vmstate_sclpquiesce;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    /*
     * Reason: This is just an internal device - the notifier should
     * not be registered multiple times in quiesce_init()
     */
    dc->user_creatable = false;

    k->init = quiesce_init;
    k->get_send_mask = send_mask;
    k->get_receive_mask = receive_mask;
    k->can_handle_event = can_handle_event;
    k->read_event_data = read_event_data;
    k->write_event_data = NULL;
}

static const TypeInfo sclp_quiesce_info = {
    .name          = TYPE_SCLP_QUIESCE,
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPEvent),
    .class_init    = quiesce_class_init,
    .class_size    = sizeof(SCLPEventClass),
};

static void register_types(void)
{
    type_register_static(&sclp_quiesce_info);
}

type_init(register_types)
