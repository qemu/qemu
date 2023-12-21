/*
 * QEMU IPMI BT emulation
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
#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/ipmi/ipmi_bt.h"

/* Control register */
#define IPMI_BT_CLR_WR_BIT         0
#define IPMI_BT_CLR_RD_BIT         1
#define IPMI_BT_H2B_ATN_BIT        2
#define IPMI_BT_B2H_ATN_BIT        3
#define IPMI_BT_SMS_ATN_BIT        4
#define IPMI_BT_HBUSY_BIT          6
#define IPMI_BT_BBUSY_BIT          7

#define IPMI_BT_GET_CLR_WR(d)      (((d) >> IPMI_BT_CLR_WR_BIT) & 0x1)

#define IPMI_BT_GET_CLR_RD(d)      (((d) >> IPMI_BT_CLR_RD_BIT) & 0x1)

#define IPMI_BT_GET_H2B_ATN(d)     (((d) >> IPMI_BT_H2B_ATN_BIT) & 0x1)

#define IPMI_BT_B2H_ATN_MASK       (1 << IPMI_BT_B2H_ATN_BIT)
#define IPMI_BT_GET_B2H_ATN(d)     (((d) >> IPMI_BT_B2H_ATN_BIT) & 0x1)
#define IPMI_BT_SET_B2H_ATN(d, v)  ((d) = (((d) & ~IPMI_BT_B2H_ATN_MASK) | \
                                        (!!(v) << IPMI_BT_B2H_ATN_BIT)))

#define IPMI_BT_SMS_ATN_MASK       (1 << IPMI_BT_SMS_ATN_BIT)
#define IPMI_BT_GET_SMS_ATN(d)     (((d) >> IPMI_BT_SMS_ATN_BIT) & 0x1)
#define IPMI_BT_SET_SMS_ATN(d, v)  ((d) = (((d) & ~IPMI_BT_SMS_ATN_MASK) | \
                                        (!!(v) << IPMI_BT_SMS_ATN_BIT)))

#define IPMI_BT_HBUSY_MASK         (1 << IPMI_BT_HBUSY_BIT)
#define IPMI_BT_GET_HBUSY(d)       (((d) >> IPMI_BT_HBUSY_BIT) & 0x1)
#define IPMI_BT_SET_HBUSY(d, v)    ((d) = (((d) & ~IPMI_BT_HBUSY_MASK) | \
                                       (!!(v) << IPMI_BT_HBUSY_BIT)))

#define IPMI_BT_BBUSY_MASK         (1 << IPMI_BT_BBUSY_BIT)
#define IPMI_BT_SET_BBUSY(d, v)    ((d) = (((d) & ~IPMI_BT_BBUSY_MASK) | \
                                       (!!(v) << IPMI_BT_BBUSY_BIT)))


/* Mask register */
#define IPMI_BT_B2H_IRQ_EN_BIT     0
#define IPMI_BT_B2H_IRQ_BIT        1

#define IPMI_BT_B2H_IRQ_EN_MASK      (1 << IPMI_BT_B2H_IRQ_EN_BIT)
#define IPMI_BT_GET_B2H_IRQ_EN(d)    (((d) >> IPMI_BT_B2H_IRQ_EN_BIT) & 0x1)
#define IPMI_BT_SET_B2H_IRQ_EN(d, v) ((d) = (((d) & ~IPMI_BT_B2H_IRQ_EN_MASK) |\
                                        (!!(v) << IPMI_BT_B2H_IRQ_EN_BIT)))

#define IPMI_BT_B2H_IRQ_MASK         (1 << IPMI_BT_B2H_IRQ_BIT)
#define IPMI_BT_GET_B2H_IRQ(d)       (((d) >> IPMI_BT_B2H_IRQ_BIT) & 0x1)
#define IPMI_BT_SET_B2H_IRQ(d, v)    ((d) = (((d) & ~IPMI_BT_B2H_IRQ_MASK) | \
                                        (!!(v) << IPMI_BT_B2H_IRQ_BIT)))

#define IPMI_CMD_GET_BT_INTF_CAP        0x36

static void ipmi_bt_raise_irq(IPMIBT *ib)
{
    if (ib->use_irq && ib->irqs_enabled && ib->raise_irq) {
        ib->raise_irq(ib);
    }
}

static void ipmi_bt_lower_irq(IPMIBT *ib)
{
    if (ib->lower_irq) {
        ib->lower_irq(ib);
    }
}

static void ipmi_bt_handle_event(IPMIInterface *ii)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);

    if (ib->inlen < 4) {
        goto out;
    }
    /* Note that overruns are handled by handle_command */
    if (ib->inmsg[0] != (ib->inlen - 1)) {
        /* Length mismatch, just ignore. */
        IPMI_BT_SET_BBUSY(ib->control_reg, 1);
        ib->inlen = 0;
        goto out;
    }
    if ((ib->inmsg[1] == (IPMI_NETFN_APP << 2)) &&
                        (ib->inmsg[3] == IPMI_CMD_GET_BT_INTF_CAP)) {
        /* We handle this one ourselves. */
        ib->outmsg[0] = 9;
        ib->outmsg[1] = ib->inmsg[1] | 0x04;
        ib->outmsg[2] = ib->inmsg[2];
        ib->outmsg[3] = ib->inmsg[3];
        ib->outmsg[4] = 0;
        ib->outmsg[5] = 1; /* Only support 1 outstanding request. */
        if (sizeof(ib->inmsg) > 0xff) { /* Input buffer size */
            ib->outmsg[6] = 0xff;
        } else {
            ib->outmsg[6] = (unsigned char) sizeof(ib->inmsg);
        }
        if (sizeof(ib->outmsg) > 0xff) { /* Output buffer size */
            ib->outmsg[7] = 0xff;
        } else {
            ib->outmsg[7] = (unsigned char) sizeof(ib->outmsg);
        }
        ib->outmsg[8] = 10; /* Max request to response time */
        ib->outmsg[9] = 0; /* Don't recommend retries */
        ib->outlen = 10;
        IPMI_BT_SET_BBUSY(ib->control_reg, 0);
        IPMI_BT_SET_B2H_ATN(ib->control_reg, 1);
        if (!IPMI_BT_GET_B2H_IRQ(ib->mask_reg) &&
                IPMI_BT_GET_B2H_IRQ_EN(ib->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 1);
            ipmi_bt_raise_irq(ib);
        }
        goto out;
    }
    ib->waiting_seq = ib->inmsg[2];
    ib->inmsg[2] = ib->inmsg[1];
    {
        IPMIBmcClass *bk = IPMI_BMC_GET_CLASS(ib->bmc);
        bk->handle_command(ib->bmc, ib->inmsg + 2, ib->inlen - 2,
                           sizeof(ib->inmsg), ib->waiting_rsp);
    }
 out:
    return;
}

static void ipmi_bt_handle_rsp(IPMIInterface *ii, uint8_t msg_id,
                                unsigned char *rsp, unsigned int rsp_len)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);

    if (ib->waiting_rsp == msg_id) {
        ib->waiting_rsp++;
        if (rsp_len > (sizeof(ib->outmsg) - 2)) {
            ib->outmsg[0] = 4;
            ib->outmsg[1] = rsp[0];
            ib->outmsg[2] = ib->waiting_seq;
            ib->outmsg[3] = rsp[1];
            ib->outmsg[4] = IPMI_CC_CANNOT_RETURN_REQ_NUM_BYTES;
            ib->outlen = 5;
        } else {
            ib->outmsg[0] = rsp_len + 1;
            ib->outmsg[1] = rsp[0];
            ib->outmsg[2] = ib->waiting_seq;
            memcpy(ib->outmsg + 3, rsp + 1, rsp_len - 1);
            ib->outlen = rsp_len + 2;
        }
        IPMI_BT_SET_BBUSY(ib->control_reg, 0);
        IPMI_BT_SET_B2H_ATN(ib->control_reg, 1);
        if (!IPMI_BT_GET_B2H_IRQ(ib->mask_reg) &&
                IPMI_BT_GET_B2H_IRQ_EN(ib->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 1);
            ipmi_bt_raise_irq(ib);
        }
    }
}


static uint64_t ipmi_bt_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    IPMIInterface *ii = opaque;
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);
    uint32_t ret = 0xff;

    switch (addr & ib->size_mask) {
    case 0:
        ret = ib->control_reg;
        break;
    case 1:
        if (ib->outpos < ib->outlen) {
            ret = ib->outmsg[ib->outpos];
            ib->outpos++;
            if (ib->outpos == ib->outlen) {
                ib->outpos = 0;
                ib->outlen = 0;
            }
        } else {
            ret = 0xff;
        }
        break;
    case 2:
        ret = ib->mask_reg;
        break;
    default:
        ret = 0xff;
        break;
    }
    return ret;
}

static void ipmi_bt_signal(IPMIBT *ib, IPMIInterface *ii)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

    ib->do_wake = 1;
    while (ib->do_wake) {
        ib->do_wake = 0;
        iic->handle_if_event(ii);
    }
}

static void ipmi_bt_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    IPMIInterface *ii = opaque;
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);

    switch (addr & ib->size_mask) {
    case 0:
        if (IPMI_BT_GET_CLR_WR(val)) {
            ib->inlen = 0;
        }
        if (IPMI_BT_GET_CLR_RD(val)) {
            ib->outpos = 0;
        }
        if (IPMI_BT_GET_B2H_ATN(val)) {
            IPMI_BT_SET_B2H_ATN(ib->control_reg, 0);
        }
        if (IPMI_BT_GET_SMS_ATN(val)) {
            IPMI_BT_SET_SMS_ATN(ib->control_reg, 0);
        }
        if (IPMI_BT_GET_HBUSY(val)) {
            /* Toggle */
            IPMI_BT_SET_HBUSY(ib->control_reg,
                              !IPMI_BT_GET_HBUSY(ib->control_reg));
        }
        if (IPMI_BT_GET_H2B_ATN(val)) {
            IPMI_BT_SET_BBUSY(ib->control_reg, 1);
            ipmi_bt_signal(ib, ii);
        }
        break;

    case 1:
        if (ib->inlen < sizeof(ib->inmsg)) {
            ib->inmsg[ib->inlen] = val;
        }
        ib->inlen++;
        break;

    case 2:
        if (IPMI_BT_GET_B2H_IRQ_EN(val) !=
                        IPMI_BT_GET_B2H_IRQ_EN(ib->mask_reg)) {
            if (IPMI_BT_GET_B2H_IRQ_EN(val)) {
                if (IPMI_BT_GET_B2H_ATN(ib->control_reg) ||
                        IPMI_BT_GET_SMS_ATN(ib->control_reg)) {
                    IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 1);
                    ipmi_bt_raise_irq(ib);
                }
                IPMI_BT_SET_B2H_IRQ_EN(ib->mask_reg, 1);
            } else {
                if (IPMI_BT_GET_B2H_IRQ(ib->mask_reg)) {
                    IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 0);
                    ipmi_bt_lower_irq(ib);
                }
                IPMI_BT_SET_B2H_IRQ_EN(ib->mask_reg, 0);
            }
        }
        if (IPMI_BT_GET_B2H_IRQ(val) && IPMI_BT_GET_B2H_IRQ(ib->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 0);
            ipmi_bt_lower_irq(ib);
        }
        break;
    default:
        /* Ignore. */
        break;
    }
}

static const MemoryRegionOps ipmi_bt_io_ops = {
    .read = ipmi_bt_ioport_read,
    .write = ipmi_bt_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ipmi_bt_set_atn(IPMIInterface *ii, int val, int irq)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);

    if (!!val == IPMI_BT_GET_SMS_ATN(ib->control_reg)) {
        return;
    }

    IPMI_BT_SET_SMS_ATN(ib->control_reg, val);
    if (val) {
        if (irq && !IPMI_BT_GET_B2H_ATN(ib->control_reg) &&
                IPMI_BT_GET_B2H_IRQ_EN(ib->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 1);
            ipmi_bt_raise_irq(ib);
        }
    } else {
        if (!IPMI_BT_GET_B2H_ATN(ib->control_reg) &&
                IPMI_BT_GET_B2H_IRQ(ib->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 0);
            ipmi_bt_lower_irq(ib);
        }
    }
}

static void ipmi_bt_handle_reset(IPMIInterface *ii, bool is_cold)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);

    if (is_cold) {
        /* Disable the BT interrupt on reset */
        if (IPMI_BT_GET_B2H_IRQ(ib->mask_reg)) {
            IPMI_BT_SET_B2H_IRQ(ib->mask_reg, 0);
            ipmi_bt_lower_irq(ib);
        }
        IPMI_BT_SET_B2H_IRQ_EN(ib->mask_reg, 0);
    }
}

static void ipmi_bt_set_irq_enable(IPMIInterface *ii, int val)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);

    ib->irqs_enabled = val;
}

static void ipmi_bt_init(IPMIInterface *ii, unsigned int min_size, Error **errp)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIBT *ib = iic->get_backend_data(ii);

    if (min_size == 0) {
        min_size = 4;
    }
    ib->size_mask = min_size - 1;
    ib->io_length = 3;

    memory_region_init_io(&ib->io, NULL, &ipmi_bt_io_ops, ii, "ipmi-bt",
                          min_size);
}

int ipmi_bt_vmstate_post_load(void *opaque, int version)
{
    IPMIBT *ib = opaque;

    /* Make sure all the values are sane. */
    if (ib->outpos >= MAX_IPMI_MSG_SIZE || ib->outlen >= MAX_IPMI_MSG_SIZE ||
        ib->outpos >= ib->outlen) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ipmi:bt: vmstate transfer received bad out values: %d %d\n",
                      ib->outpos, ib->outlen);
        ib->outpos = 0;
        ib->outlen = 0;
    }

    if (ib->inlen >= MAX_IPMI_MSG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ipmi:bt: vmstate transfer received bad in value: %d\n",
                      ib->inlen);
        ib->inlen = 0;
    }

    return 0;
}

const VMStateDescription vmstate_IPMIBT = {
    .name = TYPE_IPMI_INTERFACE_PREFIX "bt",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ipmi_bt_vmstate_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(obf_irq_set, IPMIBT),
        VMSTATE_BOOL(atn_irq_set, IPMIBT),
        VMSTATE_BOOL(irqs_enabled, IPMIBT),
        VMSTATE_UINT32(outpos, IPMIBT),
        VMSTATE_UINT32(outlen, IPMIBT),
        VMSTATE_UINT8_ARRAY(outmsg, IPMIBT, MAX_IPMI_MSG_SIZE),
        VMSTATE_UINT32(inlen, IPMIBT),
        VMSTATE_UINT8_ARRAY(inmsg, IPMIBT, MAX_IPMI_MSG_SIZE),
        VMSTATE_UINT8(control_reg, IPMIBT),
        VMSTATE_UINT8(mask_reg, IPMIBT),
        VMSTATE_UINT8(waiting_rsp, IPMIBT),
        VMSTATE_UINT8(waiting_seq, IPMIBT),
        VMSTATE_END_OF_LIST()
    }
};

void ipmi_bt_get_fwinfo(struct IPMIBT *ib, IPMIFwInfo *info)
{
    info->interface_name = "bt";
    info->interface_type = IPMI_SMBIOS_BT;
    info->ipmi_spec_major_revision = 2;
    info->ipmi_spec_minor_revision = 0;
    info->base_address = ib->io_base;
    info->register_length = ib->io_length;
    info->register_spacing = 1;
    info->memspace = IPMI_MEMSPACE_IO;
    info->irq_type = IPMI_LEVEL_IRQ;
}

void ipmi_bt_class_init(IPMIInterfaceClass *iic)
{
    iic->init = ipmi_bt_init;
    iic->set_atn = ipmi_bt_set_atn;
    iic->handle_rsp = ipmi_bt_handle_rsp;
    iic->handle_if_event = ipmi_bt_handle_event;
    iic->set_irq_enable = ipmi_bt_set_irq_enable;
    iic->reset = ipmi_bt_handle_reset;
}
