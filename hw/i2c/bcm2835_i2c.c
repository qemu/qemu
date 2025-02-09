/*
 * Broadcom Serial Controller (BSC)
 *
 * Copyright (c) 2024 Rayhan Faizel <rayhan.faizel@gmail.com>
 *
 * SPDX-License-Identifier: MIT
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
#include "qemu/log.h"
#include "hw/i2c/bcm2835_i2c.h"
#include "hw/irq.h"
#include "migration/vmstate.h"

static void bcm2835_i2c_update_interrupt(BCM2835I2CState *s)
{
    int do_interrupt = 0;
    /* Interrupt on RXR (Needs reading) */
    if (s->c & BCM2835_I2C_C_INTR && s->s & BCM2835_I2C_S_RXR) {
        do_interrupt = 1;
    }

    /* Interrupt on TXW (Needs writing) */
    if (s->c & BCM2835_I2C_C_INTT && s->s & BCM2835_I2C_S_TXW) {
        do_interrupt = 1;
    }

    /* Interrupt on DONE (Transfer complete) */
    if (s->c & BCM2835_I2C_C_INTD && s->s & BCM2835_I2C_S_DONE) {
        do_interrupt = 1;
    }
    qemu_set_irq(s->irq, do_interrupt);
}

static void bcm2835_i2c_begin_transfer(BCM2835I2CState *s)
{
    int direction = s->c & BCM2835_I2C_C_READ;
    if (i2c_start_transfer(s->bus, s->a, direction)) {
        s->s |= BCM2835_I2C_S_ERR;
    }
    s->s |= BCM2835_I2C_S_TA;

    if (direction) {
        s->s |= BCM2835_I2C_S_RXR | BCM2835_I2C_S_RXD;
    } else {
        s->s |= BCM2835_I2C_S_TXW;
    }
}

static void bcm2835_i2c_finish_transfer(BCM2835I2CState *s)
{
    /*
     * STOP is sent when DLEN counts down to zero.
     *
     * https://github.com/torvalds/linux/blob/v6.7/drivers/i2c/busses/i2c-bcm2835.c#L223-L261
     * It is possible to initiate repeated starts on real hardware.
     * However, this requires sending another ST request before the bytes in
     * TX FIFO are shifted out.
     *
     * This is not emulated currently.
     */
    i2c_end_transfer(s->bus);
    s->s |= BCM2835_I2C_S_DONE;

    /* Ensure RXD is cleared, otherwise the driver registers an error */
    s->s &= ~(BCM2835_I2C_S_TA | BCM2835_I2C_S_RXR |
              BCM2835_I2C_S_TXW | BCM2835_I2C_S_RXD);
}

static uint64_t bcm2835_i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    BCM2835I2CState *s = opaque;
    uint32_t readval = 0;

    switch (addr) {
    case BCM2835_I2C_C:
        readval = s->c;
        break;
    case BCM2835_I2C_S:
        readval = s->s;
        break;
    case BCM2835_I2C_DLEN:
        readval = s->dlen;
        break;
    case BCM2835_I2C_A:
        readval = s->a;
        break;
    case BCM2835_I2C_FIFO:
        /* We receive I2C messages directly instead of using FIFOs */
        if (s->s & BCM2835_I2C_S_TA) {
            readval = i2c_recv(s->bus);
            s->dlen -= 1;

            if (s->dlen == 0) {
                bcm2835_i2c_finish_transfer(s);
            }
        }
        bcm2835_i2c_update_interrupt(s);
        break;
    case BCM2835_I2C_DIV:
        readval = s->div;
        break;
    case BCM2835_I2C_DEL:
        readval = s->del;
        break;
    case BCM2835_I2C_CLKT:
        readval = s->clkt;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }

    return readval;
}

static void bcm2835_i2c_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    BCM2835I2CState *s = opaque;
    uint32_t writeval = value;

    switch (addr) {
    case BCM2835_I2C_C:
        /* ST is a one-shot operation; it must read back as 0 */
        s->c = writeval & ~BCM2835_I2C_C_ST;

        /* Start transfer */
        if (writeval & (BCM2835_I2C_C_ST | BCM2835_I2C_C_I2CEN)) {
            bcm2835_i2c_begin_transfer(s);
            /*
             * Handle special case where transfer starts with zero data length.
             * Required for zero length i2c quick messages to work.
             */
            if (s->dlen == 0) {
                bcm2835_i2c_finish_transfer(s);
            }
        }

        bcm2835_i2c_update_interrupt(s);
        break;
    case BCM2835_I2C_S:
        if (writeval & BCM2835_I2C_S_DONE && s->s & BCM2835_I2C_S_DONE) {
            /* When DONE is cleared, DLEN should read last written value. */
            s->dlen = s->last_dlen;
        }

        /* Clear DONE, CLKT and ERR by writing 1 */
        s->s &= ~(writeval & (BCM2835_I2C_S_DONE |
                  BCM2835_I2C_S_ERR | BCM2835_I2C_S_CLKT));
        break;
    case BCM2835_I2C_DLEN:
        s->dlen = writeval;
        s->last_dlen = writeval;
        break;
    case BCM2835_I2C_A:
        s->a = writeval;
        break;
    case BCM2835_I2C_FIFO:
        /* We send I2C messages directly instead of using FIFOs */
        if (s->s & BCM2835_I2C_S_TA) {
            if (s->s & BCM2835_I2C_S_TXD) {
                if (!i2c_send(s->bus, writeval & 0xff)) {
                    s->dlen -= 1;
                } else {
                    s->s |= BCM2835_I2C_S_ERR;
                }
            }

            if (s->dlen == 0) {
                bcm2835_i2c_finish_transfer(s);
            }
        }
        bcm2835_i2c_update_interrupt(s);
        break;
    case BCM2835_I2C_DIV:
        s->div = writeval;
        break;
    case BCM2835_I2C_DEL:
        s->del = writeval;
        break;
    case BCM2835_I2C_CLKT:
        s->clkt = writeval;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps bcm2835_i2c_ops = {
    .read = bcm2835_i2c_read,
    .write = bcm2835_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bcm2835_i2c_realize(DeviceState *dev, Error **errp)
{
    BCM2835I2CState *s = BCM2835_I2C(dev);
    s->bus = i2c_init_bus(dev, NULL);

    memory_region_init_io(&s->iomem, OBJECT(dev), &bcm2835_i2c_ops, s,
                          TYPE_BCM2835_I2C, 0x24);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static void bcm2835_i2c_reset(DeviceState *dev)
{
    BCM2835I2CState *s = BCM2835_I2C(dev);

    /* Reset values according to BCM2835 Peripheral Documentation */
    s->c = 0x0;
    s->s = BCM2835_I2C_S_TXD | BCM2835_I2C_S_TXE;
    s->dlen = 0x0;
    s->a = 0x0;
    s->div = 0x5dc;
    s->del = 0x00300030;
    s->clkt = 0x40;
}

static const VMStateDescription vmstate_bcm2835_i2c = {
    .name = TYPE_BCM2835_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(c, BCM2835I2CState),
        VMSTATE_UINT32(s, BCM2835I2CState),
        VMSTATE_UINT32(dlen, BCM2835I2CState),
        VMSTATE_UINT32(a, BCM2835I2CState),
        VMSTATE_UINT32(div, BCM2835I2CState),
        VMSTATE_UINT32(del, BCM2835I2CState),
        VMSTATE_UINT32(clkt, BCM2835I2CState),
        VMSTATE_UINT32(last_dlen, BCM2835I2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, bcm2835_i2c_reset);
    dc->realize = bcm2835_i2c_realize;
    dc->vmsd = &vmstate_bcm2835_i2c;
}

static const TypeInfo bcm2835_i2c_info = {
    .name = TYPE_BCM2835_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835I2CState),
    .class_init = bcm2835_i2c_class_init,
};

static void bcm2835_i2c_register_types(void)
{
    type_register_static(&bcm2835_i2c_info);
}

type_init(bcm2835_i2c_register_types)
