/*
 * SCLP event type
 *    Ascii Console Data (VT220 Console)
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
#include "qemu/thread.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

#include "hw/s390x/sclp.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/s390x/event-facility.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

typedef struct ASCIIConsoleData {
    EventBufferHeader ebh;
    char data[];
} QEMU_PACKED ASCIIConsoleData;

/* max size for ASCII data in 4K SCCB page */
#define SIZE_BUFFER_VT220 4080

struct SCLPConsole {
    SCLPEvent event;
    CharFrontend chr;
    uint8_t iov[SIZE_BUFFER_VT220];
    uint32_t iov_sclp;      /* offset in buf for SCLP read operation       */
    uint32_t iov_bs;        /* offset in buf for char layer read operation */
    uint32_t iov_data_len;  /* length of byte stream in buffer             */
    uint32_t iov_sclp_rest; /* length of byte stream not read via SCLP     */
    bool notify;            /* qemu_notify_event() req'd if true           */
};
typedef struct SCLPConsole SCLPConsole;

#define TYPE_SCLP_CONSOLE "sclpconsole"
DECLARE_INSTANCE_CHECKER(SCLPConsole, SCLP_CONSOLE,
                         TYPE_SCLP_CONSOLE)

/* character layer call-back functions */

/* Return number of bytes that fit into iov buffer */
static int chr_can_read(void *opaque)
{
    SCLPConsole *scon = opaque;
    int avail = SIZE_BUFFER_VT220 - scon->iov_data_len;

    if (avail == 0) {
        scon->notify = true;
    }
    return avail;
}

/* Send data from a char device over to the guest */
static void chr_read(void *opaque, const uint8_t *buf, int size)
{
    SCLPConsole *scon = opaque;

    assert(scon);
    /* read data must fit into current buffer */
    assert(size <= SIZE_BUFFER_VT220 - scon->iov_data_len);

    /* put byte-stream from character layer into buffer */
    memcpy(&scon->iov[scon->iov_bs], buf, size);
    scon->iov_data_len += size;
    scon->iov_sclp_rest += size;
    scon->iov_bs += size;
    scon->event.event_pending = true;
    sclp_service_interrupt(0);
}

/* functions to be called by event facility */

static bool can_handle_event(uint8_t type)
{
    return type == SCLP_EVENT_ASCII_CONSOLE_DATA;
}

static sccb_mask_t send_mask(void)
{
    return SCLP_EVENT_MASK_MSG_ASCII;
}

static sccb_mask_t receive_mask(void)
{
    return SCLP_EVENT_MASK_MSG_ASCII;
}

/* triggered by SCLP's read_event_data -
 * copy console data byte-stream into provided (SCLP) buffer
 */
static void get_console_data(SCLPEvent *event, uint8_t *buf, size_t *size,
                             int avail)
{
    SCLPConsole *cons = SCLP_CONSOLE(event);

    /* first byte is hex 0 saying an ascii string follows */
    *buf++ = '\0';
    avail--;
    /* if all data fit into provided SCLP buffer */
    if (avail >= cons->iov_sclp_rest) {
        /* copy character byte-stream to SCLP buffer */
        memcpy(buf, &cons->iov[cons->iov_sclp], cons->iov_sclp_rest);
        *size = cons->iov_sclp_rest + 1;
        cons->iov_sclp = 0;
        cons->iov_bs = 0;
        cons->iov_data_len = 0;
        cons->iov_sclp_rest = 0;
        event->event_pending = false;
        /* data provided and no more data pending */
    } else {
        /* if provided buffer is too small, just copy part */
        memcpy(buf, &cons->iov[cons->iov_sclp], avail);
        *size = avail + 1;
        cons->iov_sclp_rest -= avail;
        cons->iov_sclp += avail;
        /* more data pending */
    }
    if (cons->notify) {
        cons->notify = false;
        qemu_notify_event();
    }
}

static int read_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr,
                           int *slen)
{
    int avail;
    size_t src_len;
    uint8_t *to;
    ASCIIConsoleData *acd = (ASCIIConsoleData *) evt_buf_hdr;

    if (!event->event_pending) {
        /* no data pending */
        return 0;
    }

    to = (uint8_t *)&acd->data;
    avail = *slen - sizeof(ASCIIConsoleData);
    get_console_data(event, to, &src_len, avail);

    acd->ebh.length = cpu_to_be16(sizeof(ASCIIConsoleData) + src_len);
    acd->ebh.type = SCLP_EVENT_ASCII_CONSOLE_DATA;
    acd->ebh.flags |= SCLP_EVENT_BUFFER_ACCEPTED;
    *slen = avail - src_len;

    return 1;
}

/* triggered by SCLP's write_event_data
 *  - write console data to character layer
 *  returns < 0 if an error occurred
 */
static ssize_t write_console_data(SCLPEvent *event, const uint8_t *buf,
                                  size_t len)
{
    SCLPConsole *scon = SCLP_CONSOLE(event);

    if (!qemu_chr_fe_backend_connected(&scon->chr)) {
        /* If there's no backend, we can just say we consumed all data. */
        return len;
    }

    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    return qemu_chr_fe_write_all(&scon->chr, buf, len);
}

static int write_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr)
{
    int rc;
    int length;
    ssize_t written;
    ASCIIConsoleData *acd = (ASCIIConsoleData *) evt_buf_hdr;

    length = be16_to_cpu(evt_buf_hdr->length) - sizeof(EventBufferHeader);
    written = write_console_data(event, (uint8_t *)acd->data, length);

    rc = SCLP_RC_NORMAL_COMPLETION;
    /* set event buffer accepted flag */
    evt_buf_hdr->flags |= SCLP_EVENT_BUFFER_ACCEPTED;

    /* written will be zero if a pty is not connected - don't treat as error */
    if (written < 0) {
        /* event buffer not accepted due to error in character layer */
        evt_buf_hdr->flags &= ~(SCLP_EVENT_BUFFER_ACCEPTED);
        rc = SCLP_RC_CONTAINED_EQUIPMENT_CHECK;
    }

    return rc;
}

static const VMStateDescription vmstate_sclpconsole = {
    .name = "sclpconsole",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(event.event_pending, SCLPConsole),
        VMSTATE_UINT8_ARRAY(iov, SCLPConsole, SIZE_BUFFER_VT220),
        VMSTATE_UINT32(iov_sclp, SCLPConsole),
        VMSTATE_UINT32(iov_bs, SCLPConsole),
        VMSTATE_UINT32(iov_data_len, SCLPConsole),
        VMSTATE_UINT32(iov_sclp_rest, SCLPConsole),
        VMSTATE_END_OF_LIST()
     }
};

/* qemu object creation and initialization functions */

/* tell character layer our call-back functions */

static int console_init(SCLPEvent *event)
{
    static bool console_available;

    SCLPConsole *scon = SCLP_CONSOLE(event);

    if (console_available) {
        error_report("Multiple VT220 operator consoles are not supported");
        return -1;
    }
    console_available = true;
    qemu_chr_fe_set_handlers(&scon->chr, chr_can_read,
                             chr_read, NULL, NULL, scon, NULL, true);

    return 0;
}

static void console_reset(DeviceState *dev)
{
   SCLPEvent *event = SCLP_EVENT(dev);
   SCLPConsole *scon = SCLP_CONSOLE(event);

   event->event_pending = false;
   scon->iov_sclp = 0;
   scon->iov_bs = 0;
   scon->iov_data_len = 0;
   scon->iov_sclp_rest = 0;
   scon->notify = false;
}

static const Property console_properties[] = {
    DEFINE_PROP_CHR("chardev", SCLPConsole, chr),
};

static void console_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCLPEventClass *ec = SCLP_EVENT_CLASS(klass);

    device_class_set_props(dc, console_properties);
    device_class_set_legacy_reset(dc, console_reset);
    dc->vmsd = &vmstate_sclpconsole;
    ec->init = console_init;
    ec->get_send_mask = send_mask;
    ec->get_receive_mask = receive_mask;
    ec->can_handle_event = can_handle_event;
    ec->read_event_data = read_event_data;
    ec->write_event_data = write_event_data;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo sclp_console_info = {
    .name          = TYPE_SCLP_CONSOLE,
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPConsole),
    .class_init    = console_class_init,
    .class_size    = sizeof(SCLPEventClass),
};

static void register_types(void)
{
    type_register_static(&sclp_console_info);
}

type_init(register_types)
