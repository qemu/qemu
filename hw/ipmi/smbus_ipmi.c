/*
 * QEMU IPMI SMBus (SSIF) emulation
 *
 * Copyright (c) 2015,2016 Corey Minyard, MontaVista Software, LLC
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
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/i2c/smbus_slave.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/ipmi/ipmi.h"

#define TYPE_SMBUS_IPMI "smbus-ipmi"
#define SMBUS_IPMI(obj) OBJECT_CHECK(SMBusIPMIDevice, (obj), TYPE_SMBUS_IPMI)

#define SSIF_IPMI_REQUEST                       2
#define SSIF_IPMI_MULTI_PART_REQUEST_START      6
#define SSIF_IPMI_MULTI_PART_REQUEST_MIDDLE     7
#define SSIF_IPMI_MULTI_PART_REQUEST_END        8
#define SSIF_IPMI_RESPONSE                      3
#define SSIF_IPMI_MULTI_PART_RESPONSE_MIDDLE    9
#define SSIF_IPMI_MULTI_PART_RETRY              0xa

#define MAX_SSIF_IPMI_MSG_SIZE 255
#define MAX_SSIF_IPMI_MSG_CHUNK 32

#define IPMI_GET_SYS_INTF_CAP_CMD 0x57

typedef struct SMBusIPMIDevice {
    SMBusDevice parent;

    IPMIBmc *bmc;

    uint8_t outmsg[MAX_SSIF_IPMI_MSG_SIZE];
    uint32_t outlen;
    uint32_t currblk;

    /* Holds the SMBUS message currently being sent to the host. */
    uint8_t outbuf[MAX_SSIF_IPMI_MSG_CHUNK + 1]; /* len + message. */
    uint32_t outpos;

    uint8_t inmsg[MAX_SSIF_IPMI_MSG_SIZE];
    uint32_t inlen;

    /*
     * This is a response number that we send with the command to make
     * sure that the response matches the command.
     */
    uint8_t waiting_rsp;

    uint32_t uuid;
} SMBusIPMIDevice;

static void smbus_ipmi_handle_event(IPMIInterface *ii)
{
    /* No interrupts, so nothing to do here. */
}

static void smbus_ipmi_handle_rsp(IPMIInterface *ii, uint8_t msg_id,
                                  unsigned char *rsp, unsigned int rsp_len)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(ii);

    if (sid->waiting_rsp == msg_id) {
        sid->waiting_rsp++;

        if (rsp_len > MAX_SSIF_IPMI_MSG_SIZE) {
            rsp[2] = IPMI_CC_REQUEST_DATA_TRUNCATED;
            rsp_len = MAX_SSIF_IPMI_MSG_SIZE;
        }
        memcpy(sid->outmsg, rsp, rsp_len);
        sid->outlen = rsp_len;
        sid->outpos = 0;
        sid->currblk = 0;
    }
}

static void smbus_ipmi_set_atn(IPMIInterface *ii, int val, int irq)
{
    /* This is where PEC would go. */
}

static void smbus_ipmi_set_irq_enable(IPMIInterface *ii, int val)
{
}

static void smbus_ipmi_send_msg(SMBusIPMIDevice *sid)
{
    uint8_t *msg = sid->inmsg;
    uint32_t len = sid->inlen;
    IPMIBmcClass *bk = IPMI_BMC_GET_CLASS(sid->bmc);

    sid->outlen = 0;
    sid->outpos = 0;
    sid->currblk = 0;

    if (msg[0] == (IPMI_NETFN_APP << 2) && msg[1] == IPMI_GET_SYS_INTF_CAP_CMD)
    {
        /* We handle this ourself. */
        sid->outmsg[0] = (IPMI_NETFN_APP + 1) << 2;
        sid->outmsg[1] = msg[1];
        if (len < 3) {
            sid->outmsg[2] = IPMI_CC_REQUEST_DATA_LENGTH_INVALID;
            sid->outlen = 3;
        } else if ((msg[2] & 0x0f) != 0) {
            sid->outmsg[2] = IPMI_CC_INVALID_DATA_FIELD;
            sid->outlen = 3;
        } else {
            sid->outmsg[2] = 0;
            sid->outmsg[3] = 0;
            sid->outmsg[4] = (2 << 6); /* Multi-part supported. */
            sid->outmsg[5] = MAX_SSIF_IPMI_MSG_SIZE;
            sid->outmsg[6] = MAX_SSIF_IPMI_MSG_SIZE;
            sid->outlen = 7;
        }
        return;
    }

    bk->handle_command(sid->bmc, sid->inmsg, sid->inlen, sizeof(sid->inmsg),
                       sid->waiting_rsp);
}

static uint8_t ipmi_receive_byte(SMBusDevice *dev)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(dev);

    if (sid->outpos >= sizeof(sid->outbuf)) {
        return 0xff;
    }

    return sid->outbuf[sid->outpos++];
}

static int ipmi_load_readbuf(SMBusIPMIDevice *sid)
{
    unsigned int block = sid->currblk, pos, len;

    if (sid->outlen == 0) {
        return -1;
    }

    if (sid->outlen <= 32) {
        if (block != 0) {
            return -1;
        }
        sid->outbuf[0] = sid->outlen;
        memcpy(sid->outbuf + 1, sid->outmsg, sid->outlen);
        sid->outpos = 0;
        return 0;
    }

    if (block == 0) {
        sid->outbuf[0] = 32;
        sid->outbuf[1] = 0;
        sid->outbuf[2] = 1;
        memcpy(sid->outbuf + 3, sid->outmsg, 30);
        sid->outpos = 0;
        return 0;
    }

    /*
     * Calculate the position in outmsg.  30 for the first block, 31
     * for the rest of the blocks.
     */
    pos = 30 + (block - 1) * 31;

    if (pos >= sid->outlen) {
        return -1;
    }

    len = sid->outlen - pos;
    if (len > 31) {
        /* More chunks after this. */
        len = 31;
        /* Blocks start at 0 for the first middle transaction. */
        sid->outbuf[1] = block - 1;
    } else {
        sid->outbuf[1] = 0xff; /* End of message marker. */
    }

    sid->outbuf[0] = len + 1;
    memcpy(sid->outbuf + 2, sid->outmsg + pos, len);
    sid->outpos = 0;
    return 0;
}

static int ipmi_write_data(SMBusDevice *dev, uint8_t *buf, uint8_t len)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(dev);
    bool send = false;
    uint8_t cmd;
    int ret = 0;

    /* length is guaranteed to be >= 1. */
    cmd = *buf++;
    len--;

    /* Handle read request, which don't have any data in the write part. */
    switch (cmd) {
    case SSIF_IPMI_RESPONSE:
        sid->currblk = 0;
        ret = ipmi_load_readbuf(sid);
        break;

    case SSIF_IPMI_MULTI_PART_RESPONSE_MIDDLE:
        sid->currblk++;
        ret = ipmi_load_readbuf(sid);
        break;

    case SSIF_IPMI_MULTI_PART_RETRY:
        if (len >= 1) {
            sid->currblk = buf[0];
            ret = ipmi_load_readbuf(sid);
        } else {
            ret = -1;
        }
        break;

    default:
        break;
    }

    /* This should be a message write, make the length is there and correct. */
    if (len >= 1) {
        if (*buf != len - 1 || *buf > MAX_SSIF_IPMI_MSG_CHUNK) {
            return -1; /* Bogus message */
        }
        buf++;
        len--;
    }

    switch (cmd) {
    case SSIF_IPMI_REQUEST:
        send = true;
        /* FALLTHRU */
    case SSIF_IPMI_MULTI_PART_REQUEST_START:
        if (len < 2) {
            return -1; /* Bogus. */
        }
        memcpy(sid->inmsg, buf, len);
        sid->inlen = len;
        break;

    case SSIF_IPMI_MULTI_PART_REQUEST_END:
        send = true;
        /* FALLTHRU */
    case SSIF_IPMI_MULTI_PART_REQUEST_MIDDLE:
        if (!sid->inlen) {
            return -1; /* Bogus. */
        }
        if (sid->inlen + len > MAX_SSIF_IPMI_MSG_SIZE) {
            sid->inlen = 0; /* Discard the message. */
            return -1; /* Bogus. */
        }
        if (len < 32) {
            /*
             * Special hack, a multi-part middle that is less than 32 bytes
             * marks the end of a message.  The specification is fairly
             * confusing, so some systems to this, even sending a zero
             * length end message to mark the end.
             */
            send = true;
        }
        memcpy(sid->inmsg + sid->inlen, buf, len);
        sid->inlen += len;
        break;
    }

    if (send && sid->inlen) {
        smbus_ipmi_send_msg(sid);
    }

    return ret;
}

static const VMStateDescription vmstate_smbus_ipmi = {
    .name = TYPE_SMBUS_IPMI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_SMBUS_DEVICE(parent, SMBusIPMIDevice),
        VMSTATE_UINT8(waiting_rsp, SMBusIPMIDevice),
        VMSTATE_UINT32(outlen, SMBusIPMIDevice),
        VMSTATE_UINT32(currblk, SMBusIPMIDevice),
        VMSTATE_UINT8_ARRAY(outmsg, SMBusIPMIDevice, MAX_SSIF_IPMI_MSG_SIZE),
        VMSTATE_UINT32(outpos, SMBusIPMIDevice),
        VMSTATE_UINT8_ARRAY(outbuf, SMBusIPMIDevice,
                            MAX_SSIF_IPMI_MSG_CHUNK + 1),
        VMSTATE_UINT32(inlen, SMBusIPMIDevice),
        VMSTATE_UINT8_ARRAY(inmsg, SMBusIPMIDevice, MAX_SSIF_IPMI_MSG_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void smbus_ipmi_realize(DeviceState *dev, Error **errp)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(dev);
    IPMIInterface *ii = IPMI_INTERFACE(dev);

    if (!sid->bmc) {
        error_setg(errp, "IPMI device requires a bmc attribute to be set");
        return;
    }

    sid->uuid = ipmi_next_uuid();

    sid->bmc->intf = ii;
}

static void smbus_ipmi_init(Object *obj)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(obj);

    ipmi_bmc_find_and_link(obj, (Object **) &sid->bmc);
}

static void smbus_ipmi_get_fwinfo(struct IPMIInterface *ii, IPMIFwInfo *info)
{
    SMBusIPMIDevice *sid = SMBUS_IPMI(ii);

    info->interface_name = "smbus";
    info->interface_type = IPMI_SMBIOS_SSIF;
    info->ipmi_spec_major_revision = 2;
    info->ipmi_spec_minor_revision = 0;
    info->i2c_slave_address = sid->bmc->slave_addr;
    info->base_address = sid->parent.i2c.address;
    info->memspace = IPMI_MEMSPACE_SMBUS;
    info->register_spacing = 1;
    info->uuid = sid->uuid;
}

static void smbus_ipmi_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(oc);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(oc);

    sc->receive_byte = ipmi_receive_byte;
    sc->write_data = ipmi_write_data;
    dc->vmsd = &vmstate_smbus_ipmi;
    dc->realize = smbus_ipmi_realize;
    iic->set_atn = smbus_ipmi_set_atn;
    iic->handle_rsp = smbus_ipmi_handle_rsp;
    iic->handle_if_event = smbus_ipmi_handle_event;
    iic->set_irq_enable = smbus_ipmi_set_irq_enable;
    iic->get_fwinfo = smbus_ipmi_get_fwinfo;
}

static const TypeInfo smbus_ipmi_info = {
    .name          = TYPE_SMBUS_IPMI,
    .parent        = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusIPMIDevice),
    .instance_init = smbus_ipmi_init,
    .class_init    = smbus_ipmi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IPMI_INTERFACE },
        { }
    }
};

static void smbus_ipmi_register_types(void)
{
    type_register_static(&smbus_ipmi_info);
}

type_init(smbus_ipmi_register_types)
