/*
 * SCLP event type
 *    Signal CPU - Trigger SCLP interrupt for system CPU configure or
 *    de-configure
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Thang Pham <thang.pham@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/s390x/sclp.h"
#include "qemu/module.h"
#include "hw/s390x/event-facility.h"
#include "sysemu/cpus.h"

typedef struct ConfigMgtData {
    EventBufferHeader ebh;
    uint8_t reserved;
    uint8_t event_qualifier;
} QEMU_PACKED ConfigMgtData;

#define EVENT_QUAL_CPU_CHANGE  1

void raise_irq_cpu_hotplug(void)
{
    Object *obj = object_resolve_path_type("", TYPE_SCLP_CPU_HOTPLUG, NULL);

    SCLP_EVENT(obj)->event_pending = true;

    /* Trigger SCLP read operation */
    sclp_service_interrupt(0);
}

static sccb_mask_t send_mask(void)
{
    return SCLP_EVENT_MASK_CONFIG_MGT_DATA;
}

static sccb_mask_t receive_mask(void)
{
    return 0;
}

static int read_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr,
                           int *slen)
{
    ConfigMgtData *cdata = (ConfigMgtData *) evt_buf_hdr;
    if (*slen < sizeof(ConfigMgtData)) {
        return 0;
    }

    /* Event is no longer pending */
    if (!event->event_pending) {
        return 0;
    }
    event->event_pending = false;

    /* Event header data */
    cdata->ebh.length = cpu_to_be16(sizeof(ConfigMgtData));
    cdata->ebh.type = SCLP_EVENT_CONFIG_MGT_DATA;
    cdata->ebh.flags |= SCLP_EVENT_BUFFER_ACCEPTED;

    /* Trigger a rescan of CPUs by setting event qualifier */
    cdata->event_qualifier = EVENT_QUAL_CPU_CHANGE;
    *slen -= sizeof(ConfigMgtData);

    return 1;
}

static void sclp_cpu_class_init(ObjectClass *oc, void *data)
{
    SCLPEventClass *k = SCLP_EVENT_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    k->get_send_mask = send_mask;
    k->get_receive_mask = receive_mask;
    k->read_event_data = read_event_data;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    /*
     * Reason: raise_irq_cpu_hotplug() depends on an unique
     * TYPE_SCLP_CPU_HOTPLUG device, which is already created
     * by the sclp event facility
     */
    dc->user_creatable = false;
}

static const TypeInfo sclp_cpu_info = {
    .name          = TYPE_SCLP_CPU_HOTPLUG,
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPEvent),
    .class_init    = sclp_cpu_class_init,
    .class_size    = sizeof(SCLPEventClass),
};

static void sclp_cpu_register_types(void)
{
    type_register_static(&sclp_cpu_info);
}

type_init(sclp_cpu_register_types)
