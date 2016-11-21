/*
 * QEMU ISA IPMI KCS emulation
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
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/ipmi/ipmi.h"
#include "hw/isa/isa.h"
#include "hw/i386/pc.h"

#define IPMI_KCS_OBF_BIT        0
#define IPMI_KCS_IBF_BIT        1
#define IPMI_KCS_SMS_ATN_BIT    2
#define IPMI_KCS_CD_BIT         3

#define IPMI_KCS_OBF_MASK          (1 << IPMI_KCS_OBF_BIT)
#define IPMI_KCS_GET_OBF(d)        (((d) >> IPMI_KCS_OBF_BIT) & 0x1)
#define IPMI_KCS_SET_OBF(d, v)     (d) = (((d) & ~IPMI_KCS_OBF_MASK) | \
                                       (((v) & 1) << IPMI_KCS_OBF_BIT))
#define IPMI_KCS_IBF_MASK          (1 << IPMI_KCS_IBF_BIT)
#define IPMI_KCS_GET_IBF(d)        (((d) >> IPMI_KCS_IBF_BIT) & 0x1)
#define IPMI_KCS_SET_IBF(d, v)     (d) = (((d) & ~IPMI_KCS_IBF_MASK) | \
                                       (((v) & 1) << IPMI_KCS_IBF_BIT))
#define IPMI_KCS_SMS_ATN_MASK      (1 << IPMI_KCS_SMS_ATN_BIT)
#define IPMI_KCS_GET_SMS_ATN(d)    (((d) >> IPMI_KCS_SMS_ATN_BIT) & 0x1)
#define IPMI_KCS_SET_SMS_ATN(d, v) (d) = (((d) & ~IPMI_KCS_SMS_ATN_MASK) | \
                                       (((v) & 1) << IPMI_KCS_SMS_ATN_BIT))
#define IPMI_KCS_CD_MASK           (1 << IPMI_KCS_CD_BIT)
#define IPMI_KCS_GET_CD(d)         (((d) >> IPMI_KCS_CD_BIT) & 0x1)
#define IPMI_KCS_SET_CD(d, v)      (d) = (((d) & ~IPMI_KCS_CD_MASK) | \
                                       (((v) & 1) << IPMI_KCS_CD_BIT))

#define IPMI_KCS_IDLE_STATE        0
#define IPMI_KCS_READ_STATE        1
#define IPMI_KCS_WRITE_STATE       2
#define IPMI_KCS_ERROR_STATE       3

#define IPMI_KCS_GET_STATE(d)    (((d) >> 6) & 0x3)
#define IPMI_KCS_SET_STATE(d, v) ((d) = ((d) & ~0xc0) | (((v) & 0x3) << 6))

#define IPMI_KCS_ABORT_STATUS_CMD       0x60
#define IPMI_KCS_WRITE_START_CMD        0x61
#define IPMI_KCS_WRITE_END_CMD          0x62
#define IPMI_KCS_READ_CMD               0x68

#define IPMI_KCS_STATUS_NO_ERR          0x00
#define IPMI_KCS_STATUS_ABORTED_ERR     0x01
#define IPMI_KCS_STATUS_BAD_CC_ERR      0x02
#define IPMI_KCS_STATUS_LENGTH_ERR      0x06

typedef struct IPMIKCS {
    IPMIBmc *bmc;

    bool do_wake;

    qemu_irq irq;

    uint32_t io_base;
    unsigned long io_length;
    MemoryRegion io;

    bool obf_irq_set;
    bool atn_irq_set;
    bool use_irq;
    bool irqs_enabled;

    uint8_t outmsg[MAX_IPMI_MSG_SIZE];
    uint32_t outpos;
    uint32_t outlen;

    uint8_t inmsg[MAX_IPMI_MSG_SIZE];
    uint32_t inlen;
    bool write_end;

    uint8_t status_reg;
    uint8_t data_out_reg;

    int16_t data_in_reg; /* -1 means not written */
    int16_t cmd_reg;

    /*
     * This is a response number that we send with the command to make
     * sure that the response matches the command.
     */
    uint8_t waiting_rsp;
} IPMIKCS;

#define SET_OBF() \
    do {                                                                      \
        IPMI_KCS_SET_OBF(ik->status_reg, 1);                                  \
        if (ik->use_irq && ik->irqs_enabled && !ik->obf_irq_set) {            \
            ik->obf_irq_set = 1;                                              \
            if (!ik->atn_irq_set) {                                           \
                qemu_irq_raise(ik->irq);                                      \
            }                                                                 \
        }                                                                     \
    } while (0)

static void ipmi_kcs_signal(IPMIKCS *ik, IPMIInterface *ii)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

    ik->do_wake = 1;
    while (ik->do_wake) {
        ik->do_wake = 0;
        iic->handle_if_event(ii);
    }
}

static void ipmi_kcs_handle_event(IPMIInterface *ii)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIKCS *ik = iic->get_backend_data(ii);

    if (ik->cmd_reg == IPMI_KCS_ABORT_STATUS_CMD) {
        if (IPMI_KCS_GET_STATE(ik->status_reg) != IPMI_KCS_ERROR_STATE) {
            ik->waiting_rsp++; /* Invalidate the message */
            ik->outmsg[0] = IPMI_KCS_STATUS_ABORTED_ERR;
            ik->outlen = 1;
            ik->outpos = 0;
            IPMI_KCS_SET_STATE(ik->status_reg, IPMI_KCS_ERROR_STATE);
            SET_OBF();
        }
        goto out;
    }

    switch (IPMI_KCS_GET_STATE(ik->status_reg)) {
    case IPMI_KCS_IDLE_STATE:
        if (ik->cmd_reg == IPMI_KCS_WRITE_START_CMD) {
            IPMI_KCS_SET_STATE(ik->status_reg, IPMI_KCS_WRITE_STATE);
            ik->cmd_reg = -1;
            ik->write_end = 0;
            ik->inlen = 0;
            SET_OBF();
        }
        break;

    case IPMI_KCS_READ_STATE:
    handle_read:
        if (ik->outpos >= ik->outlen) {
            IPMI_KCS_SET_STATE(ik->status_reg, IPMI_KCS_IDLE_STATE);
            SET_OBF();
        } else if (ik->data_in_reg == IPMI_KCS_READ_CMD) {
            ik->data_out_reg = ik->outmsg[ik->outpos];
            ik->outpos++;
            SET_OBF();
        } else {
            ik->outmsg[0] = IPMI_KCS_STATUS_BAD_CC_ERR;
            ik->outlen = 1;
            ik->outpos = 0;
            IPMI_KCS_SET_STATE(ik->status_reg, IPMI_KCS_ERROR_STATE);
            SET_OBF();
            goto out;
        }
        break;

    case IPMI_KCS_WRITE_STATE:
        if (ik->data_in_reg != -1) {
            /*
             * Don't worry about input overrun here, that will be
             * handled in the BMC.
             */
            if (ik->inlen < sizeof(ik->inmsg)) {
                ik->inmsg[ik->inlen] = ik->data_in_reg;
            }
            ik->inlen++;
        }
        if (ik->write_end) {
            IPMIBmcClass *bk = IPMI_BMC_GET_CLASS(ik->bmc);
            ik->outlen = 0;
            ik->write_end = 0;
            ik->outpos = 0;
            bk->handle_command(ik->bmc, ik->inmsg, ik->inlen, sizeof(ik->inmsg),
                               ik->waiting_rsp);
            goto out_noibf;
        } else if (ik->cmd_reg == IPMI_KCS_WRITE_END_CMD) {
            ik->cmd_reg = -1;
            ik->write_end = 1;
        }
        SET_OBF();
        break;

    case IPMI_KCS_ERROR_STATE:
        if (ik->data_in_reg != -1) {
            IPMI_KCS_SET_STATE(ik->status_reg, IPMI_KCS_READ_STATE);
            ik->data_in_reg = IPMI_KCS_READ_CMD;
            goto handle_read;
        }
        break;
    }

    if (ik->cmd_reg != -1) {
        /* Got an invalid command */
        ik->outmsg[0] = IPMI_KCS_STATUS_BAD_CC_ERR;
        ik->outlen = 1;
        ik->outpos = 0;
        IPMI_KCS_SET_STATE(ik->status_reg, IPMI_KCS_ERROR_STATE);
    }

 out:
    ik->cmd_reg = -1;
    ik->data_in_reg = -1;
    IPMI_KCS_SET_IBF(ik->status_reg, 0);
 out_noibf:
    return;
}

static void ipmi_kcs_handle_rsp(IPMIInterface *ii, uint8_t msg_id,
                                unsigned char *rsp, unsigned int rsp_len)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIKCS *ik = iic->get_backend_data(ii);

    if (ik->waiting_rsp == msg_id) {
        ik->waiting_rsp++;
        if (rsp_len > sizeof(ik->outmsg)) {
            ik->outmsg[0] = rsp[0];
            ik->outmsg[1] = rsp[1];
            ik->outmsg[2] = IPMI_CC_CANNOT_RETURN_REQ_NUM_BYTES;
            ik->outlen = 3;
        } else {
            memcpy(ik->outmsg, rsp, rsp_len);
            ik->outlen = rsp_len;
        }
        IPMI_KCS_SET_STATE(ik->status_reg, IPMI_KCS_READ_STATE);
        ik->data_in_reg = IPMI_KCS_READ_CMD;
        ipmi_kcs_signal(ik, ii);
    }
}


static uint64_t ipmi_kcs_ioport_read(void *opaque, hwaddr addr, unsigned size)
{
    IPMIInterface *ii = opaque;
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIKCS *ik = iic->get_backend_data(ii);
    uint32_t ret;

    switch (addr & 1) {
    case 0:
        ret = ik->data_out_reg;
        IPMI_KCS_SET_OBF(ik->status_reg, 0);
        if (ik->obf_irq_set) {
            ik->obf_irq_set = 0;
            if (!ik->atn_irq_set) {
                qemu_irq_lower(ik->irq);
            }
        }
        break;
    case 1:
        ret = ik->status_reg;
        if (ik->atn_irq_set) {
            ik->atn_irq_set = 0;
            if (!ik->obf_irq_set) {
                qemu_irq_lower(ik->irq);
            }
        }
        break;
    }
    return ret;
}

static void ipmi_kcs_ioport_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    IPMIInterface *ii = opaque;
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIKCS *ik = iic->get_backend_data(ii);

    if (IPMI_KCS_GET_IBF(ik->status_reg)) {
        return;
    }

    switch (addr & 1) {
    case 0:
        ik->data_in_reg = val;
        break;

    case 1:
        ik->cmd_reg = val;
        break;
    }
    IPMI_KCS_SET_IBF(ik->status_reg, 1);
    ipmi_kcs_signal(ik, ii);
}

const MemoryRegionOps ipmi_kcs_io_ops = {
    .read = ipmi_kcs_ioport_read,
    .write = ipmi_kcs_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ipmi_kcs_set_atn(IPMIInterface *ii, int val, int irq)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIKCS *ik = iic->get_backend_data(ii);

    IPMI_KCS_SET_SMS_ATN(ik->status_reg, val);
    if (val) {
        if (irq && !ik->atn_irq_set && ik->use_irq && ik->irqs_enabled) {
            ik->atn_irq_set = 1;
            if (!ik->obf_irq_set) {
                qemu_irq_raise(ik->irq);
            }
        }
    } else {
        if (ik->atn_irq_set) {
            ik->atn_irq_set = 0;
            if (!ik->obf_irq_set) {
                qemu_irq_lower(ik->irq);
            }
        }
    }
}

static void ipmi_kcs_set_irq_enable(IPMIInterface *ii, int val)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIKCS *ik = iic->get_backend_data(ii);

    ik->irqs_enabled = val;
}

static void ipmi_kcs_init(IPMIInterface *ii, Error **errp)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    IPMIKCS *ik = iic->get_backend_data(ii);

    ik->io_length = 2;
    memory_region_init_io(&ik->io, NULL, &ipmi_kcs_io_ops, ii, "ipmi-kcs", 2);
}

#define TYPE_ISA_IPMI_KCS "isa-ipmi-kcs"
#define ISA_IPMI_KCS(obj) OBJECT_CHECK(ISAIPMIKCSDevice, (obj), \
                                       TYPE_ISA_IPMI_KCS)

typedef struct ISAIPMIKCSDevice {
    ISADevice dev;
    int32_t isairq;
    IPMIKCS kcs;
    uint32_t uuid;
} ISAIPMIKCSDevice;

static void ipmi_kcs_get_fwinfo(IPMIInterface *ii, IPMIFwInfo *info)
{
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(ii);

    info->interface_name = "kcs";
    info->interface_type = IPMI_SMBIOS_KCS;
    info->ipmi_spec_major_revision = 2;
    info->ipmi_spec_minor_revision = 0;
    info->base_address = iik->kcs.io_base;
    info->i2c_slave_address = iik->kcs.bmc->slave_addr;
    info->register_length = iik->kcs.io_length;
    info->register_spacing = 1;
    info->memspace = IPMI_MEMSPACE_IO;
    info->irq_type = IPMI_LEVEL_IRQ;
    info->interrupt_number = iik->isairq;
    info->uuid = iik->uuid;
}

static void ipmi_kcs_class_init(IPMIInterfaceClass *iic)
{
    iic->init = ipmi_kcs_init;
    iic->set_atn = ipmi_kcs_set_atn;
    iic->handle_rsp = ipmi_kcs_handle_rsp;
    iic->handle_if_event = ipmi_kcs_handle_event;
    iic->set_irq_enable = ipmi_kcs_set_irq_enable;
    iic->get_fwinfo = ipmi_kcs_get_fwinfo;
}

static void ipmi_isa_realize(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(dev);
    IPMIInterface *ii = IPMI_INTERFACE(dev);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);

    if (!iik->kcs.bmc) {
        error_setg(errp, "IPMI device requires a bmc attribute to be set");
        return;
    }

    iik->uuid = ipmi_next_uuid();

    iik->kcs.bmc->intf = ii;

    iic->init(ii, errp);
    if (*errp)
        return;

    if (iik->isairq > 0) {
        isa_init_irq(isadev, &iik->kcs.irq, iik->isairq);
        iik->kcs.use_irq = 1;
    }

    qdev_set_legacy_instance_id(dev, iik->kcs.io_base, iik->kcs.io_length);

    isa_register_ioport(isadev, &iik->kcs.io, iik->kcs.io_base);
}

const VMStateDescription vmstate_ISAIPMIKCSDevice = {
    .name = TYPE_IPMI_INTERFACE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_BOOL(kcs.obf_irq_set, ISAIPMIKCSDevice),
        VMSTATE_BOOL(kcs.atn_irq_set, ISAIPMIKCSDevice),
        VMSTATE_BOOL(kcs.use_irq, ISAIPMIKCSDevice),
        VMSTATE_BOOL(kcs.irqs_enabled, ISAIPMIKCSDevice),
        VMSTATE_UINT32(kcs.outpos, ISAIPMIKCSDevice),
        VMSTATE_UINT8_ARRAY(kcs.outmsg, ISAIPMIKCSDevice, MAX_IPMI_MSG_SIZE),
        VMSTATE_UINT8_ARRAY(kcs.inmsg, ISAIPMIKCSDevice, MAX_IPMI_MSG_SIZE),
        VMSTATE_BOOL(kcs.write_end, ISAIPMIKCSDevice),
        VMSTATE_UINT8(kcs.status_reg, ISAIPMIKCSDevice),
        VMSTATE_UINT8(kcs.data_out_reg, ISAIPMIKCSDevice),
        VMSTATE_INT16(kcs.data_in_reg, ISAIPMIKCSDevice),
        VMSTATE_INT16(kcs.cmd_reg, ISAIPMIKCSDevice),
        VMSTATE_UINT8(kcs.waiting_rsp, ISAIPMIKCSDevice),
        VMSTATE_END_OF_LIST()
    }
};

static void isa_ipmi_kcs_init(Object *obj)
{
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(obj);

    ipmi_bmc_find_and_link(obj, (Object **) &iik->kcs.bmc);

    vmstate_register(NULL, 0, &vmstate_ISAIPMIKCSDevice, iik);
}

static void *isa_ipmi_kcs_get_backend_data(IPMIInterface *ii)
{
    ISAIPMIKCSDevice *iik = ISA_IPMI_KCS(ii);

    return &iik->kcs;
}

static Property ipmi_isa_properties[] = {
    DEFINE_PROP_UINT32("ioport", ISAIPMIKCSDevice, kcs.io_base,  0xca2),
    DEFINE_PROP_INT32("irq",   ISAIPMIKCSDevice, isairq,  5),
    DEFINE_PROP_END_OF_LIST(),
};

static void isa_ipmi_kcs_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(oc);

    dc->realize = ipmi_isa_realize;
    dc->props = ipmi_isa_properties;

    iic->get_backend_data = isa_ipmi_kcs_get_backend_data;
    ipmi_kcs_class_init(iic);
}

static const TypeInfo isa_ipmi_kcs_info = {
    .name          = TYPE_ISA_IPMI_KCS,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAIPMIKCSDevice),
    .instance_init = isa_ipmi_kcs_init,
    .class_init    = isa_ipmi_kcs_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IPMI_INTERFACE },
        { }
    }
};

static void ipmi_register_types(void)
{
    type_register_static(&isa_ipmi_kcs_info);
}

type_init(ipmi_register_types)
