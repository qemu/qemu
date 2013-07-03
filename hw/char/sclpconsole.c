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

#include <hw/qdev.h>
#include "qemu/thread.h"
#include "qemu/error-report.h"

#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"
#include "sysemu/char.h"

typedef struct ASCIIConsoleData {
    EventBufferHeader ebh;
    char data[0];
} QEMU_PACKED ASCIIConsoleData;

/* max size for ASCII data in 4K SCCB page */
#define SIZE_BUFFER_VT220 4080

typedef struct SCLPConsole {
    SCLPEvent event;
    CharDriverState *chr;
    /* io vector                                                       */
    uint8_t *iov;           /* iov buffer pointer                      */
    uint8_t *iov_sclp;      /* pointer to SCLP read offset             */
    uint8_t *iov_bs;        /* pointer byte stream read offset         */
    uint32_t iov_data_len;  /* length of byte stream in buffer         */
    uint32_t iov_sclp_rest; /* length of byte stream not read via SCLP */
    qemu_irq irq_read_vt220;
} SCLPConsole;

/* character layer call-back functions */

/* Return number of bytes that fit into iov buffer */
static int chr_can_read(void *opaque)
{
    SCLPConsole *scon = opaque;

    return scon->iov ? SIZE_BUFFER_VT220 - scon->iov_data_len : 0;
}

/* Receive n bytes from character layer, save in iov buffer,
 * and set event pending */
static void receive_from_chr_layer(SCLPConsole *scon, const uint8_t *buf,
                                   int size)
{
    assert(scon->iov);

    /* read data must fit into current buffer */
    assert(size <= SIZE_BUFFER_VT220 - scon->iov_data_len);

    /* put byte-stream from character layer into buffer */
    memcpy(scon->iov_bs, buf, size);
    scon->iov_data_len += size;
    scon->iov_sclp_rest += size;
    scon->iov_bs += size;
    scon->event.event_pending = true;
}

/* Send data from a char device over to the guest */
static void chr_read(void *opaque, const uint8_t *buf, int size)
{
    SCLPConsole *scon = opaque;

    assert(scon);

    receive_from_chr_layer(scon, buf, size);
    /* trigger SCLP read operation */
    qemu_irq_raise(scon->irq_read_vt220);
}

static void chr_event(void *opaque, int event)
{
    SCLPConsole *scon = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        if (!scon->iov) {
            scon->iov = g_malloc0(SIZE_BUFFER_VT220);
            scon->iov_sclp = scon->iov;
            scon->iov_bs = scon->iov;
            scon->iov_data_len = 0;
            scon->iov_sclp_rest = 0;
        }
        break;
    case CHR_EVENT_CLOSED:
        if (scon->iov) {
            g_free(scon->iov);
            scon->iov = NULL;
        }
        break;
    }
}

/* functions to be called by event facility */

static int event_type(void)
{
    return SCLP_EVENT_ASCII_CONSOLE_DATA;
}

static unsigned int send_mask(void)
{
    return SCLP_EVENT_MASK_MSG_ASCII;
}

static unsigned int receive_mask(void)
{
    return SCLP_EVENT_MASK_MSG_ASCII;
}

/* triggered by SCLP's read_event_data -
 * copy console data byte-stream into provided (SCLP) buffer
 */
static void get_console_data(SCLPEvent *event, uint8_t *buf, size_t *size,
                             int avail)
{
    SCLPConsole *cons = DO_UPCAST(SCLPConsole, event, event);

    /* first byte is hex 0 saying an ascii string follows */
    *buf++ = '\0';
    avail--;
    /* if all data fit into provided SCLP buffer */
    if (avail >= cons->iov_sclp_rest) {
        /* copy character byte-stream to SCLP buffer */
        memcpy(buf, cons->iov_sclp, cons->iov_sclp_rest);
        *size = cons->iov_sclp_rest + 1;
        cons->iov_sclp = cons->iov;
        cons->iov_bs = cons->iov;
        cons->iov_data_len = 0;
        cons->iov_sclp_rest = 0;
        event->event_pending = false;
        /* data provided and no more data pending */
    } else {
        /* if provided buffer is too small, just copy part */
        memcpy(buf, cons->iov_sclp, avail);
        *size = avail + 1;
        cons->iov_sclp_rest -= avail;
        cons->iov_sclp += avail;
        /* more data pending */
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
    ssize_t ret = 0;
    const uint8_t *iov_offset;
    SCLPConsole *scon = DO_UPCAST(SCLPConsole, event, event);

    if (!scon->chr) {
        /* If there's no backend, we can just say we consumed all data. */
        return len;
    }

    iov_offset = buf;
    while (len > 0) {
        ret = qemu_chr_fe_write(scon->chr, buf, len);
        if (ret == 0) {
            /* a pty doesn't seem to be connected - no error */
            len = 0;
        } else if (ret == -EAGAIN || (ret > 0 && ret < len)) {
            len -= ret;
            iov_offset += ret;
        } else {
            len = 0;
        }
    }

    return ret;
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

static void trigger_ascii_console_data(void *opaque, int n, int level)
{
    sclp_service_interrupt(0);
}

/* qemu object creation and initialization functions */

/* tell character layer our call-back functions */
static int console_init(SCLPEvent *event)
{
    static bool console_available;

    SCLPConsole *scon = DO_UPCAST(SCLPConsole, event, event);

    if (console_available) {
        error_report("Multiple VT220 operator consoles are not supported");
        return -1;
    }
    console_available = true;
    event->event_type = SCLP_EVENT_ASCII_CONSOLE_DATA;
    if (scon->chr) {
        qemu_chr_add_handlers(scon->chr, chr_can_read,
                              chr_read, chr_event, scon);
    }
    scon->irq_read_vt220 = *qemu_allocate_irqs(trigger_ascii_console_data,
                                               NULL, 1);

    return 0;
}

static int console_exit(SCLPEvent *event)
{
    return 0;
}

static Property console_properties[] = {
    DEFINE_PROP_CHR("chardev", SCLPConsole, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void console_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCLPEventClass *ec = SCLP_EVENT_CLASS(klass);

    dc->props = console_properties;
    ec->init = console_init;
    ec->exit = console_exit;
    ec->get_send_mask = send_mask;
    ec->get_receive_mask = receive_mask;
    ec->event_type = event_type;
    ec->read_event_data = read_event_data;
    ec->write_event_data = write_event_data;
}

static const TypeInfo sclp_console_info = {
    .name          = "sclpconsole",
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
