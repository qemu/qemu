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
#include "sysemu/sysemu.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"

typedef struct ConfigMgtData {
    EventBufferHeader ebh;
    uint8_t reserved;
    uint8_t event_qualifier;
} QEMU_PACKED ConfigMgtData;

static qemu_irq *irq_cpu_hotplug; /* Only used in this file */

#define EVENT_QUAL_CPU_CHANGE  1

void raise_irq_cpu_hotplug(void)
{
    qemu_irq_raise(*irq_cpu_hotplug);
}

static unsigned int send_mask(void)
{
    return SCLP_EVENT_MASK_CONFIG_MGT_DATA;
}

static unsigned int receive_mask(void)
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

static void trigger_signal(void *opaque, int n, int level)
{
    SCLPEvent *event = opaque;
    event->event_pending = true;

    /* Trigger SCLP read operation */
    sclp_service_interrupt(0);
}

static int irq_cpu_hotplug_init(SCLPEvent *event)
{
    irq_cpu_hotplug = qemu_allocate_irqs(trigger_signal, event, 1);
    return 0;
}

static void cpu_class_init(ObjectClass *oc, void *data)
{
    SCLPEventClass *k = SCLP_EVENT_CLASS(oc);

    k->init = irq_cpu_hotplug_init;
    k->get_send_mask = send_mask;
    k->get_receive_mask = receive_mask;
    k->read_event_data = read_event_data;
    k->write_event_data = NULL;
}

static const TypeInfo sclp_cpu_info = {
    .name          = "sclp-cpu-hotplug",
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPEvent),
    .class_init    = cpu_class_init,
    .class_size    = sizeof(SCLPEventClass),
};

static void sclp_cpu_register_types(void)
{
    type_register_static(&sclp_cpu_info);
}

type_init(sclp_cpu_register_types)
