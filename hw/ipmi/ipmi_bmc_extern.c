/*
 * IPMI BMC external connection
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * This is designed to connect with OpenIPMI's lanserv serial interface
 * using the "VM" connection type.  See that for details.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "sysemu/char.h"
#include "sysemu/sysemu.h"
#include "hw/ipmi/ipmi.h"

#define VM_MSG_CHAR        0xA0 /* Marks end of message */
#define VM_CMD_CHAR        0xA1 /* Marks end of a command */
#define VM_ESCAPE_CHAR     0xAA /* Set bit 4 from the next byte to 0 */

#define VM_PROTOCOL_VERSION        1
#define VM_CMD_VERSION             0xff /* A version number byte follows */
#define VM_CMD_NOATTN              0x00
#define VM_CMD_ATTN                0x01
#define VM_CMD_ATTN_IRQ            0x02
#define VM_CMD_POWEROFF            0x03
#define VM_CMD_RESET               0x04
#define VM_CMD_ENABLE_IRQ          0x05 /* Enable/disable the messaging irq */
#define VM_CMD_DISABLE_IRQ         0x06
#define VM_CMD_SEND_NMI            0x07
#define VM_CMD_CAPABILITIES        0x08
#define   VM_CAPABILITIES_POWER    0x01
#define   VM_CAPABILITIES_RESET    0x02
#define   VM_CAPABILITIES_IRQ      0x04
#define   VM_CAPABILITIES_NMI      0x08
#define   VM_CAPABILITIES_ATTN     0x10
#define VM_CMD_FORCEOFF            0x09

#define TYPE_IPMI_BMC_EXTERN "ipmi-bmc-extern"
#define IPMI_BMC_EXTERN(obj) OBJECT_CHECK(IPMIBmcExtern, (obj), \
                                        TYPE_IPMI_BMC_EXTERN)
typedef struct IPMIBmcExtern {
    IPMIBmc parent;

    CharDriverState *chr;

    bool connected;

    unsigned char inbuf[MAX_IPMI_MSG_SIZE + 2];
    unsigned int inpos;
    bool in_escape;
    bool in_too_many;
    bool waiting_rsp;
    bool sending_cmd;

    unsigned char outbuf[(MAX_IPMI_MSG_SIZE + 2) * 2 + 1];
    unsigned int outpos;
    unsigned int outlen;

    struct QEMUTimer *extern_timer;

    /* A reset event is pending to be sent upstream. */
    bool send_reset;
} IPMIBmcExtern;

static int can_receive(void *opaque);
static void receive(void *opaque, const uint8_t *buf, int size);
static void chr_event(void *opaque, int event);

static unsigned char
ipmb_checksum(const unsigned char *data, int size, unsigned char start)
{
        unsigned char csum = start;

        for (; size > 0; size--, data++) {
                csum += *data;
        }
        return csum;
}

static void continue_send(IPMIBmcExtern *ibe)
{
    if (ibe->outlen == 0) {
        goto check_reset;
    }
 send:
    ibe->outpos += qemu_chr_fe_write(ibe->chr, ibe->outbuf + ibe->outpos,
                                     ibe->outlen - ibe->outpos);
    if (ibe->outpos < ibe->outlen) {
        /* Not fully transmitted, try again in a 10ms */
        timer_mod_ns(ibe->extern_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000000);
    } else {
        /* Sent */
        ibe->outlen = 0;
        ibe->outpos = 0;
        if (!ibe->sending_cmd) {
            ibe->waiting_rsp = true;
        } else {
            ibe->sending_cmd = false;
        }
    check_reset:
        if (ibe->connected && ibe->send_reset) {
            /* Send the reset */
            ibe->outbuf[0] = VM_CMD_RESET;
            ibe->outbuf[1] = VM_CMD_CHAR;
            ibe->outlen = 2;
            ibe->outpos = 0;
            ibe->send_reset = false;
            ibe->sending_cmd = true;
            goto send;
        }

        if (ibe->waiting_rsp) {
            /* Make sure we get a response within 4 seconds. */
            timer_mod_ns(ibe->extern_timer,
                         qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 4000000000ULL);
        }
    }
    return;
}

static void extern_timeout(void *opaque)
{
    IPMIBmcExtern *ibe = opaque;
    IPMIInterface *s = ibe->parent.intf;

    if (ibe->connected) {
        if (ibe->waiting_rsp && (ibe->outlen == 0)) {
            IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
            /* The message response timed out, return an error. */
            ibe->waiting_rsp = false;
            ibe->inbuf[1] = ibe->outbuf[1] | 0x04;
            ibe->inbuf[2] = ibe->outbuf[2];
            ibe->inbuf[3] = IPMI_CC_TIMEOUT;
            k->handle_rsp(s, ibe->outbuf[0], ibe->inbuf + 1, 3);
        } else {
            continue_send(ibe);
        }
    }
}

static void addchar(IPMIBmcExtern *ibe, unsigned char ch)
{
    switch (ch) {
    case VM_MSG_CHAR:
    case VM_CMD_CHAR:
    case VM_ESCAPE_CHAR:
        ibe->outbuf[ibe->outlen] = VM_ESCAPE_CHAR;
        ibe->outlen++;
        ch |= 0x10;
        /* No break */

    default:
        ibe->outbuf[ibe->outlen] = ch;
        ibe->outlen++;
    }
}

static void ipmi_bmc_extern_handle_command(IPMIBmc *b,
                                       uint8_t *cmd, unsigned int cmd_len,
                                       unsigned int max_cmd_len,
                                       uint8_t msg_id)
{
    IPMIBmcExtern *ibe = IPMI_BMC_EXTERN(b);
    IPMIInterface *s = ibe->parent.intf;
    uint8_t err = 0, csum;
    unsigned int i;

    if (ibe->outlen) {
        /* We already have a command queued.  Shouldn't ever happen. */
        fprintf(stderr, "IPMI KCS: Got command when not finished with the"
                " previous commmand\n");
        abort();
    }

    /* If it's too short or it was truncated, return an error. */
    if (cmd_len < 2) {
        err = IPMI_CC_REQUEST_DATA_LENGTH_INVALID;
    } else if ((cmd_len > max_cmd_len) || (cmd_len > MAX_IPMI_MSG_SIZE)) {
        err = IPMI_CC_REQUEST_DATA_TRUNCATED;
    } else if (!ibe->connected) {
        err = IPMI_CC_BMC_INIT_IN_PROGRESS;
    }
    if (err) {
        IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
        unsigned char rsp[3];
        rsp[0] = cmd[0] | 0x04;
        rsp[1] = cmd[1];
        rsp[2] = err;
        ibe->waiting_rsp = false;
        k->handle_rsp(s, msg_id, rsp, 3);
        goto out;
    }

    addchar(ibe, msg_id);
    for (i = 0; i < cmd_len; i++) {
        addchar(ibe, cmd[i]);
    }
    csum = ipmb_checksum(&msg_id, 1, 0);
    addchar(ibe, -ipmb_checksum(cmd, cmd_len, csum));

    ibe->outbuf[ibe->outlen] = VM_MSG_CHAR;
    ibe->outlen++;

    /* Start the transmit */
    continue_send(ibe);

 out:
    return;
}

static void handle_hw_op(IPMIBmcExtern *ibe, unsigned char hw_op)
{
    IPMIInterface *s = ibe->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    switch (hw_op) {
    case VM_CMD_VERSION:
        /* We only support one version at this time. */
        break;

    case VM_CMD_NOATTN:
        k->set_atn(s, 0, 0);
        break;

    case VM_CMD_ATTN:
        k->set_atn(s, 1, 0);
        break;

    case VM_CMD_ATTN_IRQ:
        k->set_atn(s, 1, 1);
        break;

    case VM_CMD_POWEROFF:
        k->do_hw_op(s, IPMI_POWEROFF_CHASSIS, 0);
        break;

    case VM_CMD_RESET:
        k->do_hw_op(s, IPMI_RESET_CHASSIS, 0);
        break;

    case VM_CMD_ENABLE_IRQ:
        k->set_irq_enable(s, 1);
        break;

    case VM_CMD_DISABLE_IRQ:
        k->set_irq_enable(s, 0);
        break;

    case VM_CMD_SEND_NMI:
        k->do_hw_op(s, IPMI_SEND_NMI, 0);
        break;

    case VM_CMD_FORCEOFF:
        qemu_system_shutdown_request();
        break;
    }
}

static void handle_msg(IPMIBmcExtern *ibe)
{
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(ibe->parent.intf);

    if (ibe->in_escape) {
        ipmi_debug("msg escape not ended\n");
        return;
    }
    if (ibe->inpos < 5) {
        ipmi_debug("msg too short\n");
        return;
    }
    if (ibe->in_too_many) {
        ibe->inbuf[3] = IPMI_CC_REQUEST_DATA_TRUNCATED;
        ibe->inpos = 4;
    } else if (ipmb_checksum(ibe->inbuf, ibe->inpos, 0) != 0) {
        ipmi_debug("msg checksum failure\n");
        return;
    } else {
        ibe->inpos--; /* Remove checkum */
    }

    timer_del(ibe->extern_timer);
    ibe->waiting_rsp = false;
    k->handle_rsp(ibe->parent.intf, ibe->inbuf[0], ibe->inbuf + 1, ibe->inpos - 1);
}

static int can_receive(void *opaque)
{
    return 1;
}

static void receive(void *opaque, const uint8_t *buf, int size)
{
    IPMIBmcExtern *ibe = opaque;
    int i;
    unsigned char hw_op;

    for (i = 0; i < size; i++) {
        unsigned char ch = buf[i];

        switch (ch) {
        case VM_MSG_CHAR:
            handle_msg(ibe);
            ibe->in_too_many = false;
            ibe->inpos = 0;
            break;

        case VM_CMD_CHAR:
            if (ibe->in_too_many) {
                ipmi_debug("cmd in too many\n");
                ibe->in_too_many = false;
                ibe->inpos = 0;
                break;
            }
            if (ibe->in_escape) {
                ipmi_debug("cmd in escape\n");
                ibe->in_too_many = false;
                ibe->inpos = 0;
                ibe->in_escape = false;
                break;
            }
            ibe->in_too_many = false;
            if (ibe->inpos < 1) {
                break;
            }
            hw_op = ibe->inbuf[0];
            ibe->inpos = 0;
            goto out_hw_op;
            break;

        case VM_ESCAPE_CHAR:
            ibe->in_escape = true;
            break;

        default:
            if (ibe->in_escape) {
                ch &= ~0x10;
                ibe->in_escape = false;
            }
            if (ibe->in_too_many) {
                break;
            }
            if (ibe->inpos >= sizeof(ibe->inbuf)) {
                ibe->in_too_many = true;
                break;
            }
            ibe->inbuf[ibe->inpos] = ch;
            ibe->inpos++;
            break;
        }
    }
    return;

 out_hw_op:
    handle_hw_op(ibe, hw_op);
}

static void chr_event(void *opaque, int event)
{
    IPMIBmcExtern *ibe = opaque;
    IPMIInterface *s = ibe->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    unsigned char v;

    switch (event) {
    case CHR_EVENT_OPENED:
        ibe->connected = true;
        ibe->outpos = 0;
        ibe->outlen = 0;
        addchar(ibe, VM_CMD_VERSION);
        addchar(ibe, VM_PROTOCOL_VERSION);
        ibe->outbuf[ibe->outlen] = VM_CMD_CHAR;
        ibe->outlen++;
        addchar(ibe, VM_CMD_CAPABILITIES);
        v = VM_CAPABILITIES_IRQ | VM_CAPABILITIES_ATTN;
        if (k->do_hw_op(ibe->parent.intf, IPMI_POWEROFF_CHASSIS, 1) == 0) {
            v |= VM_CAPABILITIES_POWER;
        }
        if (k->do_hw_op(ibe->parent.intf, IPMI_RESET_CHASSIS, 1) == 0) {
            v |= VM_CAPABILITIES_RESET;
        }
        if (k->do_hw_op(ibe->parent.intf, IPMI_SEND_NMI, 1) == 0) {
            v |= VM_CAPABILITIES_NMI;
        }
        addchar(ibe, v);
        ibe->outbuf[ibe->outlen] = VM_CMD_CHAR;
        ibe->outlen++;
        ibe->sending_cmd = false;
        continue_send(ibe);
        break;

    case CHR_EVENT_CLOSED:
        if (!ibe->connected) {
            return;
        }
        ibe->connected = false;
        if (ibe->waiting_rsp) {
            ibe->waiting_rsp = false;
            ibe->inbuf[1] = ibe->outbuf[1] | 0x04;
            ibe->inbuf[2] = ibe->outbuf[2];
            ibe->inbuf[3] = IPMI_CC_BMC_INIT_IN_PROGRESS;
            k->handle_rsp(s, ibe->outbuf[0], ibe->inbuf + 1, 3);
        }
        break;
    }
}

static void ipmi_bmc_extern_handle_reset(IPMIBmc *b)
{
    IPMIBmcExtern *ibe = IPMI_BMC_EXTERN(b);

    ibe->send_reset = true;
    continue_send(ibe);
}

static void ipmi_bmc_extern_realize(DeviceState *dev, Error **errp)
{
    IPMIBmcExtern *ibe = IPMI_BMC_EXTERN(dev);

    if (!ibe->chr) {
        error_setg(errp, "IPMI external bmc requires chardev attribute");
        return;
    }

    qemu_chr_add_handlers(ibe->chr, can_receive, receive, chr_event, ibe);
}

static int ipmi_bmc_extern_post_migrate(void *opaque, int version_id)
{
    IPMIBmcExtern *ibe = opaque;

    /*
     * We don't directly restore waiting_rsp, Instead, we return an
     * error on the interface if a response was being waited for.
     */
    if (ibe->waiting_rsp) {
        IPMIInterface *ii = ibe->parent.intf;
        IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

        ibe->waiting_rsp = false;
        ibe->inbuf[1] = ibe->outbuf[1] | 0x04;
        ibe->inbuf[2] = ibe->outbuf[2];
        ibe->inbuf[3] = IPMI_CC_BMC_INIT_IN_PROGRESS;
        iic->handle_rsp(ii, ibe->outbuf[0], ibe->inbuf + 1, 3);
    }
    return 0;
}

static const VMStateDescription vmstate_ipmi_bmc_extern = {
    .name = TYPE_IPMI_BMC_EXTERN,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ipmi_bmc_extern_post_migrate,
    .fields      = (VMStateField[]) {
        VMSTATE_BOOL(send_reset, IPMIBmcExtern),
        VMSTATE_BOOL(waiting_rsp, IPMIBmcExtern),
        VMSTATE_END_OF_LIST()
    }
};

static void ipmi_bmc_extern_init(Object *obj)
{
    IPMIBmcExtern *ibe = IPMI_BMC_EXTERN(obj);

    ibe->extern_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, extern_timeout, ibe);
    vmstate_register(NULL, 0, &vmstate_ipmi_bmc_extern, ibe);
}

static Property ipmi_bmc_extern_properties[] = {
    DEFINE_PROP_CHR("chardev", IPMIBmcExtern, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void ipmi_bmc_extern_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIBmcClass *bk = IPMI_BMC_CLASS(oc);

    bk->handle_command = ipmi_bmc_extern_handle_command;
    bk->handle_reset = ipmi_bmc_extern_handle_reset;
    dc->realize = ipmi_bmc_extern_realize;
    dc->props = ipmi_bmc_extern_properties;
}

static const TypeInfo ipmi_bmc_extern_type = {
    .name          = TYPE_IPMI_BMC_EXTERN,
    .parent        = TYPE_IPMI_BMC,
    .instance_size = sizeof(IPMIBmcExtern),
    .instance_init = ipmi_bmc_extern_init,
    .class_init    = ipmi_bmc_extern_class_init,
 };

static void ipmi_bmc_extern_register_types(void)
{
    type_register_static(&ipmi_bmc_extern_type);
}

type_init(ipmi_bmc_extern_register_types)
