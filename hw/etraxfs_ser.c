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

#include "sysbus.h"
#include "qemu-char.h"
#include "qemu-log.h"

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

struct etrax_serial
{
    SysBusDevice busdev;
    CharDriverState *chr;
    qemu_irq irq;

    int pending_tx;

    uint8_t rx_fifo[16];
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;

    /* Control registers.  */
    uint32_t regs[R_MAX];
};

static void ser_update_irq(struct etrax_serial *s)
{

    if (s->rx_fifo_len) {
        s->regs[R_INTR] |= 8;
    } else {
        s->regs[R_INTR] &= ~8;
    }

    s->regs[R_MASKED_INTR] = s->regs[R_INTR] & s->regs[RW_INTR_MASK];
    qemu_set_irq(s->irq, !!s->regs[R_MASKED_INTR]);
}

static uint32_t ser_readl (void *opaque, target_phys_addr_t addr)
{
    struct etrax_serial *s = opaque;
    D(CPUState *env = s->env);
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
ser_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct etrax_serial *s = opaque;
    unsigned char ch = value;
    D(CPUState *env = s->env);

    D(qemu_log("%s " TARGET_FMT_plx "=%x\n",  __func__, addr, value));
    addr >>= 2;
    switch (addr)
    {
        case RW_DOUT:
            qemu_chr_fe_write(s->chr, &ch, 1);
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

static CPUReadMemoryFunc * const ser_read[] = {
    NULL, NULL,
    &ser_readl,
};

static CPUWriteMemoryFunc * const ser_write[] = {
    NULL, NULL,
    &ser_writel,
};

static void serial_receive(void *opaque, const uint8_t *buf, int size)
{
    struct etrax_serial *s = opaque;
    int i;

    /* Got a byte.  */
    if (s->rx_fifo_len >= 16) {
        qemu_log("WARNING: UART dropped char.\n");
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
    struct etrax_serial *s = opaque;
    int r;

    /* Is the receiver enabled?  */
    if (!(s->regs[RW_REC_CTRL] & (1 << 3))) {
        return 0;
    }

    r = sizeof(s->rx_fifo) - s->rx_fifo_len;
    return r;
}

static void serial_event(void *opaque, int event)
{

}

static void etraxfs_ser_reset(DeviceState *d)
{
    struct etrax_serial *s = container_of(d, typeof(*s), busdev.qdev);

    /* transmitter begins ready and idle.  */
    s->regs[RS_STAT_DIN] |= (1 << STAT_TR_RDY);
    s->regs[RS_STAT_DIN] |= (1 << STAT_TR_IDLE);

    s->regs[RW_REC_CTRL] = 0x10000;

}

static int etraxfs_ser_init(SysBusDevice *dev)
{
    struct etrax_serial *s = FROM_SYSBUS(typeof (*s), dev);
    int ser_regs;

    sysbus_init_irq(dev, &s->irq);
    ser_regs = cpu_register_io_memory(ser_read, ser_write, s,
                                      DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, ser_regs);
    s->chr = qdev_init_chardev(&dev->qdev);
    if (s->chr)
        qemu_chr_add_handlers(s->chr,
                      serial_can_receive, serial_receive,
                      serial_event, s);
    return 0;
}

static SysBusDeviceInfo etraxfs_ser_info = {
    .init = etraxfs_ser_init,
    .qdev.name  = "etraxfs,serial",
    .qdev.size  = sizeof(struct etrax_serial),
    .qdev.reset = etraxfs_ser_reset,
};

static void etraxfs_serial_register(void)
{
    sysbus_register_withprop(&etraxfs_ser_info);
}

device_init(etraxfs_serial_register)
