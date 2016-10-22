/*
 * QEMU ETRAX System Emulator
 *
 * Copyright (c) 2007 Edgar E. Iglesias, Axis Communications AB.
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
#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "qemu/log.h"

#define D(x)

#define RW_TR_CTRL     (0x00 / 4)
#define RW_TR_DMA_EN   (0x04 / 4)
#define RW_REC_CTRL    (0x08 / 4)
#define RW_DOUT        (0x1c / 4)
#define RS_STAT_DIN    (0x20 / 4)
#define R_STAT_DIN     (0x24 / 4)
#define RW_INTR_MASK   (0x2c / 4)
#define RW_ACK_INTR    (0x30 / 4)
#define R_INTR         (0x34 / 4)
#define R_MASKED_INTR  (0x38 / 4)
#define R_MAX          (0x3c / 4)

#define STAT_DAV     16
#define STAT_TR_IDLE 22
#define STAT_TR_RDY  24

#define TYPE_ETRAX_FS_SERIAL "etraxfs,serial"
#define ETRAX_SERIAL(obj) \
    OBJECT_CHECK(ETRAXSerial, (obj), TYPE_ETRAX_FS_SERIAL)

typedef struct ETRAXSerial {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharBackend chr;
    qemu_irq irq;

    int pending_tx;

    uint8_t rx_fifo[16];
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;

    /* Control registers.  */
    uint32_t regs[R_MAX];
} ETRAXSerial;

static void ser_update_irq(ETRAXSerial *s)
{

    if (s->rx_fifo_len) {
        s->regs[R_INTR] |= 8;
    } else {
        s->regs[R_INTR] &= ~8;
    }

    s->regs[R_MASKED_INTR] = s->regs[R_INTR] & s->regs[RW_INTR_MASK];
    qemu_set_irq(s->irq, !!s->regs[R_MASKED_INTR]);
}

static uint64_t
ser_read(void *opaque, hwaddr addr, unsigned int size)
{
    ETRAXSerial *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr)
    {
        case R_STAT_DIN:
            r = s->rx_fifo[(s->rx_fifo_pos - s->rx_fifo_len) & 15];
            if (s->rx_fifo_len) {
                r |= 1 << STAT_DAV;
            }
            r |= 1 << STAT_TR_RDY;
            r |= 1 << STAT_TR_IDLE;
            break;
        case RS_STAT_DIN:
            r = s->rx_fifo[(s->rx_fifo_pos - s->rx_fifo_len) & 15];
            if (s->rx_fifo_len) {
                r |= 1 << STAT_DAV;
                s->rx_fifo_len--;
            }
            r |= 1 << STAT_TR_RDY;
            r |= 1 << STAT_TR_IDLE;
            break;
        default:
            r = s->regs[addr];
            D(qemu_log("%s " TARGET_FMT_plx "=%x\n", __func__, addr, r));
            break;
    }
    return r;
}

static void
ser_write(void *opaque, hwaddr addr,
          uint64_t val64, unsigned int size)
{
    ETRAXSerial *s = opaque;
    uint32_t value = val64;
    unsigned char ch = val64;

    D(qemu_log("%s " TARGET_FMT_plx "=%x\n",  __func__, addr, value));
    addr >>= 2;
    switch (addr)
    {
        case RW_DOUT:
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            s->regs[R_INTR] |= 3;
            s->pending_tx = 1;
            s->regs[addr] = value;
            break;
        case RW_ACK_INTR:
            if (s->pending_tx) {
                value &= ~1;
                s->pending_tx = 0;
                D(qemu_log("fixedup value=%x r_intr=%x\n",
                           value, s->regs[R_INTR]));
            }
            s->regs[addr] = value;
            s->regs[R_INTR] &= ~value;
            D(printf("r_intr=%x\n", s->regs[R_INTR]));
            break;
        default:
            s->regs[addr] = value;
            break;
    }
    ser_update_irq(s);
}

static const MemoryRegionOps ser_ops = {
    .read = ser_read,
    .write = ser_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static Property etraxfs_ser_properties[] = {
    DEFINE_PROP_CHR("chardev", ETRAXSerial, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void serial_receive(void *opaque, const uint8_t *buf, int size)
{
    ETRAXSerial *s = opaque;
    int i;

    /* Got a byte.  */
    if (s->rx_fifo_len >= 16) {
        D(qemu_log("WARNING: UART dropped char.\n"));
        return;
    }

    for (i = 0; i < size; i++) { 
        s->rx_fifo[s->rx_fifo_pos] = buf[i];
        s->rx_fifo_pos++;
        s->rx_fifo_pos &= 15;
        s->rx_fifo_len++;
    }

    ser_update_irq(s);
}

static int serial_can_receive(void *opaque)
{
    ETRAXSerial *s = opaque;

    /* Is the receiver enabled?  */
    if (!(s->regs[RW_REC_CTRL] & (1 << 3))) {
        return 0;
    }

    return sizeof(s->rx_fifo) - s->rx_fifo_len;
}

static void serial_event(void *opaque, int event)
{

}

static void etraxfs_ser_reset(DeviceState *d)
{
    ETRAXSerial *s = ETRAX_SERIAL(d);

    /* transmitter begins ready and idle.  */
    s->regs[RS_STAT_DIN] |= (1 << STAT_TR_RDY);
    s->regs[RS_STAT_DIN] |= (1 << STAT_TR_IDLE);

    s->regs[RW_REC_CTRL] = 0x10000;

}

static void etraxfs_ser_init(Object *obj)
{
    ETRAXSerial *s = ETRAX_SERIAL(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(dev, &s->irq);
    memory_region_init_io(&s->mmio, obj, &ser_ops, s,
                          "etraxfs-serial", R_MAX * 4);
    sysbus_init_mmio(dev, &s->mmio);
}

static void etraxfs_ser_realize(DeviceState *dev, Error **errp)
{
    ETRAXSerial *s = ETRAX_SERIAL(dev);

    qemu_chr_fe_set_handlers(&s->chr,
                             serial_can_receive, serial_receive,
                             serial_event, s, NULL, true);
}

static void etraxfs_ser_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = etraxfs_ser_reset;
    dc->props = etraxfs_ser_properties;
    dc->realize = etraxfs_ser_realize;
}

static const TypeInfo etraxfs_ser_info = {
    .name          = TYPE_ETRAX_FS_SERIAL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ETRAXSerial),
    .instance_init = etraxfs_ser_init,
    .class_init    = etraxfs_ser_class_init,
};

static void etraxfs_serial_register_types(void)
{
    type_register_static(&etraxfs_ser_info);
}

type_init(etraxfs_serial_register_types)
