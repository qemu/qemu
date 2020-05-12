/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Amit Tomar, <Amit.Tomar@freescale.com>
 *
 * Description:
 * This file is derived from IMX I2C controller,
 * by Jean-Christophe DUBOIS .
 *
 * Thanks to Scott Wood and Alexander Graf for their kind help on this.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 or later,
 * as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"

/* #define DEBUG_I2C */

#ifdef DEBUG_I2C
#define DPRINTF(fmt, ...)              \
    do { fprintf(stderr, "mpc_i2c[%s]: " fmt, __func__, ## __VA_ARGS__); \
    } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define TYPE_MPC_I2C "mpc-i2c"
#define MPC_I2C(obj) \
    OBJECT_CHECK(MPCI2CState, (obj), TYPE_MPC_I2C)

#define MPC_I2C_ADR   0x00
#define MPC_I2C_FDR   0x04
#define MPC_I2C_CR    0x08
#define MPC_I2C_SR    0x0c
#define MPC_I2C_DR    0x10
#define MPC_I2C_DFSRR 0x14

#define CCR_MEN  (1 << 7)
#define CCR_MIEN (1 << 6)
#define CCR_MSTA (1 << 5)
#define CCR_MTX  (1 << 4)
#define CCR_TXAK (1 << 3)
#define CCR_RSTA (1 << 2)
#define CCR_BCST (1 << 0)

#define CSR_MCF  (1 << 7)
#define CSR_MAAS (1 << 6)
#define CSR_MBB  (1 << 5)
#define CSR_MAL  (1 << 4)
#define CSR_SRW  (1 << 2)
#define CSR_MIF  (1 << 1)
#define CSR_RXAK (1 << 0)

#define CADR_MASK 0xFE
#define CFDR_MASK 0x3F
#define CCR_MASK  0xFC
#define CSR_MASK  0xED
#define CDR_MASK  0xFF

#define CYCLE_RESET 0xFF

typedef struct MPCI2CState {
    SysBusDevice parent_obj;

    I2CBus *bus;
    qemu_irq irq;
    MemoryRegion iomem;

    uint8_t address;
    uint8_t adr;
    uint8_t fdr;
    uint8_t cr;
    uint8_t sr;
    uint8_t dr;
    uint8_t dfssr;
} MPCI2CState;

static bool mpc_i2c_is_enabled(MPCI2CState *s)
{
    return s->cr & CCR_MEN;
}

static bool mpc_i2c_is_master(MPCI2CState *s)
{
    return s->cr & CCR_MSTA;
}

static bool mpc_i2c_direction_is_tx(MPCI2CState *s)
{
    return s->cr & CCR_MTX;
}

static bool mpc_i2c_irq_pending(MPCI2CState *s)
{
    return s->sr & CSR_MIF;
}

static bool mpc_i2c_irq_is_enabled(MPCI2CState *s)
{
    return s->cr & CCR_MIEN;
}

static void mpc_i2c_reset(DeviceState *dev)
{
    MPCI2CState *i2c = MPC_I2C(dev);

    i2c->address = 0xFF;
    i2c->adr = 0x00;
    i2c->fdr = 0x00;
    i2c->cr =  0x00;
    i2c->sr =  0x81;
    i2c->dr =  0x00;
}

static void mpc_i2c_irq(MPCI2CState *s)
{
    bool irq_active = false;

    if (mpc_i2c_is_enabled(s) && mpc_i2c_irq_is_enabled(s)
                              && mpc_i2c_irq_pending(s)) {
        irq_active = true;
    }

    if (irq_active) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void mpc_i2c_soft_reset(MPCI2CState *s)
{
    /* This is a soft reset. ADR is preserved during soft resets */
    uint8_t adr = s->adr;
    mpc_i2c_reset(DEVICE(s));
    s->adr = adr;
}

static void  mpc_i2c_address_send(MPCI2CState *s)
{
    /* if returns non zero slave address is not right */
    if (i2c_start_transfer(s->bus, s->dr >> 1, s->dr & (0x01))) {
        s->sr |= CSR_RXAK;
    } else {
        s->address = s->dr;
        s->sr &= ~CSR_RXAK;
        s->sr |=  CSR_MCF; /* Set after Byte Transfer is completed */
        s->sr |=  CSR_MIF; /* Set after Byte Transfer is completed */
        mpc_i2c_irq(s);
    }
}

static void  mpc_i2c_data_send(MPCI2CState *s)
{
    if (i2c_send(s->bus, s->dr)) {
        /* End of transfer */
        s->sr |= CSR_RXAK;
        i2c_end_transfer(s->bus);
    } else {
        s->sr &= ~CSR_RXAK;
        s->sr |=  CSR_MCF; /* Set after Byte Transfer is completed */
        s->sr |=  CSR_MIF; /* Set after Byte Transfer is completed */
        mpc_i2c_irq(s);
    }
}

static void  mpc_i2c_data_recive(MPCI2CState *s)
{
    int ret;
    /* get the next byte */
    ret = i2c_recv(s->bus);
    if (ret >= 0) {
        s->sr |= CSR_MCF; /* Set after Byte Transfer is completed */
        s->sr |= CSR_MIF; /* Set after Byte Transfer is completed */
        mpc_i2c_irq(s);
    } else {
        DPRINTF("read failed for device");
        ret = 0xff;
    }
    s->dr = ret;
}

static uint64_t mpc_i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    MPCI2CState *s = opaque;
    uint8_t value;

    switch (addr) {
    case MPC_I2C_ADR:
        value = s->adr;
        break;
    case MPC_I2C_FDR:
        value = s->fdr;
        break;
    case MPC_I2C_CR:
        value = s->cr;
        break;
    case MPC_I2C_SR:
        value = s->sr;
        break;
    case MPC_I2C_DR:
        value = s->dr;
        if (mpc_i2c_is_master(s)) { /* master mode */
            if (mpc_i2c_direction_is_tx(s)) {
                DPRINTF("MTX is set not in recv mode\n");
            } else {
                mpc_i2c_data_recive(s);
            }
        }
        break;
    default:
        value = 0;
        DPRINTF("ERROR: Bad read addr 0x%x\n", (unsigned int)addr);
        break;
    }

    DPRINTF("%s: addr " TARGET_FMT_plx " %02" PRIx32 "\n", __func__,
                                         addr, value);
    return (uint64_t)value;
}

static void mpc_i2c_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    MPCI2CState *s = opaque;

    DPRINTF("%s: addr " TARGET_FMT_plx " val %08" PRIx64 "\n", __func__,
                                             addr, value);
    switch (addr) {
    case MPC_I2C_ADR:
        s->adr = value & CADR_MASK;
        break;
    case MPC_I2C_FDR:
        s->fdr = value & CFDR_MASK;
        break;
    case MPC_I2C_CR:
        if (mpc_i2c_is_enabled(s) && ((value & CCR_MEN) == 0)) {
            mpc_i2c_soft_reset(s);
            break;
        }
        /* normal write */
        s->cr = value & CCR_MASK;
        if (mpc_i2c_is_master(s)) { /* master mode */
            /* set the bus to busy after master is set as per RM */
            s->sr |= CSR_MBB;
        } else {
            /* bus is not busy anymore */
            s->sr &= ~CSR_MBB;
            /* Reset the address for fresh write/read cycle */
        if (s->address != CYCLE_RESET) {
            i2c_end_transfer(s->bus);
            s->address = CYCLE_RESET;
            }
        }
        /* For restart end the onging transfer */
        if (s->cr & CCR_RSTA) {
            if (s->address != CYCLE_RESET) {
                s->address = CYCLE_RESET;
                i2c_end_transfer(s->bus);
                s->cr &= ~CCR_RSTA;
            }
        }
        break;
    case MPC_I2C_SR:
        s->sr = value & CSR_MASK;
        /* Lower the interrupt */
        if (!(s->sr & CSR_MIF) || !(s->sr & CSR_MAL)) {
            mpc_i2c_irq(s);
        }
        break;
    case MPC_I2C_DR:
        /* if the device is not enabled, nothing to do */
        if (!mpc_i2c_is_enabled(s)) {
            break;
        }
        s->dr = value & CDR_MASK;
        if (mpc_i2c_is_master(s)) { /* master mode */
            if (s->address == CYCLE_RESET) {
                mpc_i2c_address_send(s);
            } else {
                mpc_i2c_data_send(s);
            }
        }
        break;
    case MPC_I2C_DFSRR:
        s->dfssr = value;
        break;
    default:
        DPRINTF("ERROR: Bad write addr 0x%x\n", (unsigned int)addr);
        break;
    }
}

static const MemoryRegionOps i2c_ops = {
    .read =  mpc_i2c_read,
    .write =  mpc_i2c_write,
    .valid.max_access_size = 1,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription mpc_i2c_vmstate = {
    .name = TYPE_MPC_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(address, MPCI2CState),
        VMSTATE_UINT8(adr, MPCI2CState),
        VMSTATE_UINT8(fdr, MPCI2CState),
        VMSTATE_UINT8(cr, MPCI2CState),
        VMSTATE_UINT8(sr, MPCI2CState),
        VMSTATE_UINT8(dr, MPCI2CState),
        VMSTATE_UINT8(dfssr, MPCI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void mpc_i2c_realize(DeviceState *dev, Error **errp)
{
    MPCI2CState  *i2c = MPC_I2C(dev);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &i2c->irq);
    memory_region_init_io(&i2c->iomem, OBJECT(i2c), &i2c_ops, i2c,
                          "mpc-i2c", 0x14);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &i2c->iomem);
    i2c->bus = i2c_init_bus(dev, "i2c");
}

static void mpc_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd  = &mpc_i2c_vmstate ;
    dc->reset = mpc_i2c_reset;
    dc->realize = mpc_i2c_realize;
    dc->desc = "MPC I2C Controller";
}

static const TypeInfo mpc_i2c_type_info = {
    .name          = TYPE_MPC_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MPCI2CState),
    .class_init    = mpc_i2c_class_init,
};

static void mpc_i2c_register_types(void)
{
    type_register_static(&mpc_i2c_type_info);
}

type_init(mpc_i2c_register_types)
