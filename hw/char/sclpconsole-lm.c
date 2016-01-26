/*
 * SCLP event types
 *    Operations Command - Line Mode input
 *    Message            - Line Mode output
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Heinz Graalfs <graalfs@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "qemu/thread.h"
#include "qemu/error-report.h"
#include "sysemu/char.h"

#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"
#include "hw/s390x/ebcdic.h"

#define SIZE_BUFFER 4096
#define NEWLINE     "\n"

typedef struct OprtnsCommand {
    EventBufferHeader header;
    MDMSU message_unit;
    char data[0];
} QEMU_PACKED OprtnsCommand;

/* max size for line-mode data in 4K SCCB page */
#define SIZE_CONSOLE_BUFFER (SCCB_DATA_LEN - sizeof(OprtnsCommand))

typedef struct SCLPConsoleLM {
    SCLPEvent event;
    CharDriverState *chr;
    bool echo;                  /* immediate echo of input if true        */
    uint32_t write_errors;      /* errors writing to char layer           */
    uint32_t length;            /* length of byte stream in buffer        */
    uint8_t buf[SIZE_CONSOLE_BUFFER];
} SCLPConsoleLM;

/*
*  Character layer call-back functions
 *
 * Allow 1 character at a time
 *
 * Accumulate bytes from character layer in console buffer,
 * event_pending is set when a newline character is encountered
 *
 * The maximum command line length is limited by the maximum
 * space available in an SCCB. Line mode console input is sent
 * truncated to the guest in case it doesn't fit into the SCCB.
 */

static int chr_can_read(void *opaque)
{
    SCLPConsoleLM *scon = opaque;

    if (scon->event.event_pending) {
        return 0;
    }
    return 1;
}

static void chr_read(void *opaque, const uint8_t *buf, int size)
{
    SCLPConsoleLM *scon = opaque;

    assert(size == 1);

    if (*buf == '\r' || *buf == '\n') {
        scon->event.event_pending = true;
        sclp_service_interrupt(0);
        return;
    }
    if (scon->length == SIZE_CONSOLE_BUFFER) {
        /* Eat the character, but still process CR and LF.  */
        return;
    }
    scon->buf[scon->length] = *buf;
    scon->length += 1;
    if (scon->echo) {
        qemu_chr_fe_write(scon->chr, buf, size);
    }
}

/* functions to be called by event facility */

static bool can_handle_event(uint8_t type)
{
    return type == SCLP_EVENT_MESSAGE || type == SCLP_EVENT_PMSGCMD;
}

static unsigned int send_mask(void)
{
    return SCLP_EVENT_MASK_OP_CMD | SCLP_EVENT_MASK_PMSGCMD;
}

static unsigned int receive_mask(void)
{
    return SCLP_EVENT_MASK_MSG | SCLP_EVENT_MASK_PMSGCMD;
}

/*
 * Triggered by SCLP's read_event_data
 * - convert ASCII byte stream to EBCDIC and
 * - copy converted data into provided (SCLP) buffer
 */
static int get_console_data(SCLPEvent *event, uint8_t *buf, size_t *size,
                            int avail)
{
    int len;

    SCLPConsoleLM *cons = DO_UPCAST(SCLPConsoleLM, event, event);

    len = cons->length;
    /* data need to fit into provided SCLP buffer */
    if (len > avail) {
        return 1;
    }

    ebcdic_put(buf, (char *)&cons->buf, len);
    *size = len;
    cons->length = 0;
    /* data provided and no more data pending */
    event->event_pending = false;
    qemu_notify_event();
    return 0;
}

static int read_event_data(SCLPEvent *event, EventBufferHeader *evt_buf_hdr,
                           int *slen)
{
    int avail, rc;
    size_t src_len;
    uint8_t *to;
    OprtnsCommand *oc = (OprtnsCommand *) evt_buf_hdr;

    if (!event->event_pending) {
        /* no data pending */
        return 0;
    }

    to = (uint8_t *)&oc->data;
    avail = *slen - sizeof(OprtnsCommand);
    rc = get_console_data(event, to, &src_len, avail);
    if (rc) {
        /* data didn't fit, try next SCCB */
        return 1;
    }

    oc->message_unit.mdmsu.gds_id = GDS_ID_MDSMU;
    oc->message_unit.mdmsu.length = cpu_to_be16(sizeof(struct MDMSU));

    oc->message_unit.cpmsu.gds_id = GDS_ID_CPMSU;
    oc->message_unit.cpmsu.length =
        cpu_to_be16(sizeof(struct MDMSU) - sizeof(GdsVector));

    oc->message_unit.text_command.gds_id = GDS_ID_TEXTCMD;
    oc->message_unit.text_command.length =
        cpu_to_be16(sizeof(struct MDMSU) - (2 * sizeof(GdsVector)));

    oc->message_unit.self_def_text_message.key = GDS_KEY_SELFDEFTEXTMSG;
    oc->message_unit.self_def_text_message.length =
        cpu_to_be16(sizeof(struct MDMSU) - (3 * sizeof(GdsVector)));

    oc->message_unit.text_message.key = GDS_KEY_TEXTMSG;
    oc->message_unit.text_message.length =
        cpu_to_be16(sizeof(GdsSubvector) + src_len);

    oc->header.length = cpu_to_be16(sizeof(OprtnsCommand) + src_len);
    oc->header.type = SCLP_EVENT_OPRTNS_COMMAND;
    *slen = avail - src_len;

    return 1;
}

/*
 * Triggered by SCLP's write_event_data
 *  - write console data to character layer
 *  returns < 0 if an error occurred
 */
static int write_console_data(SCLPEvent *event, const uint8_t *buf, int len)
{
    int ret = 0;
    const uint8_t *buf_offset;

    SCLPConsoleLM *scon = DO_UPCAST(SCLPConsoleLM, event, event);

    if (!scon->chr) {
        /* If there's no backend, we can just say we consumed all data. */
        return len;
    }

    buf_offset = buf;
    while (len > 0) {
        ret = qemu_chr_fe_write(scon->chr, buf, len);
        if (ret == 0) {
            /* a pty doesn't seem to be connected - no error */
            len = 0;
        } else if (ret == -EAGAIN || (ret > 0 && ret < len)) {
            len -= ret;
            buf_offset += ret;
        } else {
            len = 0;
        }
    }

    return ret;
}

static int process_mdb(SCLPEvent *event, MDBO *mdbo)
{
    int rc;
    int len;
    uint8_t buffer[SIZE_BUFFER];

    len = be16_to_cpu(mdbo->length);
    len -= sizeof(mdbo->length) + sizeof(mdbo->type)
            + sizeof(mdbo->mto.line_type_flags)
            + sizeof(mdbo->mto.alarm_control)
            + sizeof(mdbo->mto._reserved);

    assert(len <= SIZE_BUFFER);

    /* convert EBCDIC SCLP contents to ASCII console message */
    ascii_put(buffer, mdbo->mto.message, len);
    rc = write_console_data(event, (uint8_t *)NEWLINE, 1);
    if (rc < 0) {
        return rc;
    }
    return write_console_data(event, buffer, len);
}

static int write_event_data(SCLPEvent *event, EventBufferHeader *ebh)
{
    int len;
    int written;
    int errors = 0;
    MDBO *mdbo;
    SclpMsg *data = (SclpMsg *) ebh;
    SCLPConsoleLM *scon = DO_UPCAST(SCLPConsoleLM, event, event);

    len = be16_to_cpu(data->mdb.header.length);
    if (len < sizeof(data->mdb.header)) {
        return SCLP_RC_INCONSISTENT_LENGTHS;
    }
    len -= sizeof(data->mdb.header);

    /* first check message buffers */
    mdbo = data->mdb.mdbo;
    while (len > 0) {
        if (be16_to_cpu(mdbo->length) > len
                || be16_to_cpu(mdbo->length) == 0) {
            return SCLP_RC_INCONSISTENT_LENGTHS;
        }
        len -= be16_to_cpu(mdbo->length);
        mdbo = (void *) mdbo + be16_to_cpu(mdbo->length);
    }

    /* then execute */
    len = be16_to_cpu(data->mdb.header.length) - sizeof(data->mdb.header);
    mdbo = data->mdb.mdbo;
    while (len > 0) {
        switch (be16_to_cpu(mdbo->type)) {
        case MESSAGE_TEXT:
            /* message text object */
            written = process_mdb(event, mdbo);
            if (written < 0) {
                /* character layer error */
                errors++;
            }
            break;
        default: /* ignore */
            break;
        }
        len -= be16_to_cpu(mdbo->length);
        mdbo = (void *) mdbo + be16_to_cpu(mdbo->length);
    }
    if (errors) {
        scon->write_errors += errors;
    }
    data->header.flags = SCLP_EVENT_BUFFER_ACCEPTED;

    return SCLP_RC_NORMAL_COMPLETION;
}

/* functions for live migration */

static const VMStateDescription vmstate_sclplmconsole = {
    .name = "sclplmconsole",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(event.event_pending, SCLPConsoleLM),
        VMSTATE_UINT32(write_errors, SCLPConsoleLM),
        VMSTATE_UINT32(length, SCLPConsoleLM),
        VMSTATE_UINT8_ARRAY(buf, SCLPConsoleLM, SIZE_CONSOLE_BUFFER),
        VMSTATE_END_OF_LIST()
     }
};

/* qemu object creation and initialization functions */

/* tell character layer our call-back functions */

static int console_init(SCLPEvent *event)
{
    static bool console_available;

    SCLPConsoleLM *scon = DO_UPCAST(SCLPConsoleLM, event, event);

    if (console_available) {
        error_report("Multiple line-mode operator consoles are not supported");
        return -1;
    }
    console_available = true;

    if (scon->chr) {
        qemu_chr_add_handlers(scon->chr, chr_can_read, chr_read, NULL, scon);
    }

    return 0;
}

static int console_exit(SCLPEvent *event)
{
    return 0;
}

static void console_reset(DeviceState *dev)
{
   SCLPEvent *event = SCLP_EVENT(dev);
   SCLPConsoleLM *scon = DO_UPCAST(SCLPConsoleLM, event, event);

   event->event_pending = false;
   scon->length = 0;
   scon->write_errors = 0;
}

static Property console_properties[] = {
    DEFINE_PROP_CHR("chardev", SCLPConsoleLM, chr),
    DEFINE_PROP_UINT32("write_errors", SCLPConsoleLM, write_errors, 0),
    DEFINE_PROP_BOOL("echo", SCLPConsoleLM, echo, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void console_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SCLPEventClass *ec = SCLP_EVENT_CLASS(klass);

    dc->props = console_properties;
    dc->reset = console_reset;
    dc->vmsd = &vmstate_sclplmconsole;
    ec->init = console_init;
    ec->exit = console_exit;
    ec->get_send_mask = send_mask;
    ec->get_receive_mask = receive_mask;
    ec->can_handle_event = can_handle_event;
    ec->read_event_data = read_event_data;
    ec->write_event_data = write_event_data;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo sclp_console_info = {
    .name          = "sclplmconsole",
    .parent        = TYPE_SCLP_EVENT,
    .instance_size = sizeof(SCLPConsoleLM),
    .class_init    = console_class_init,
    .class_size    = sizeof(SCLPEventClass),
};

static void register_types(void)
{
    type_register_static(&sclp_console_info);
}

type_init(register_types)
