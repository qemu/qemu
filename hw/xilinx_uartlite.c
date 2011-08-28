/*
 * QEMU model of Xilinx uartlite.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
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

#define DUART(x)

#define R_RX            0
#define R_TX            1
#define R_STATUS        2
#define R_CTRL          3
#define R_MAX           4

#define STATUS_RXVALID    0x01
#define STATUS_RXFULL     0x02
#define STATUS_TXEMPTY    0x04
#define STATUS_TXFULL     0x08
#define STATUS_IE         0x10
#define STATUS_OVERRUN    0x20
#define STATUS_FRAME      0x40
#define STATUS_PARITY     0x80

#define CONTROL_RST_TX    0x01
#define CONTROL_RST_RX    0x02
#define CONTROL_IE        0x10

struct xlx_uartlite
{
    SysBusDevice busdev;
    MemoryRegion mmio;
    CharDriverState *chr;
    qemu_irq irq;

    uint8_t rx_fifo[8];
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;

    uint32_t regs[R_MAX];
};

static void uart_update_irq(struct xlx_uartlite *s)
{
    unsigned int irq;

    if (s->rx_fifo_len)
        s->regs[R_STATUS] |= STATUS_IE;

    irq = (s->regs[R_STATUS] & STATUS_IE) && (s->regs[R_CTRL] & CONTROL_IE);
    qemu_set_irq(s->irq, irq);
}

static void uart_update_status(struct xlx_uartlite *s)
{
    uint32_t r;

    r = s->regs[R_STATUS];
    r &= ~7;
    r |= 1 << 2; /* Tx fifo is always empty. We are fast :) */
    r |= (s->rx_fifo_len == sizeof (s->rx_fifo)) << 1;
    r |= (!!s->rx_fifo_len);
    s->regs[R_STATUS] = r;
}

static uint64_t
uart_read(void *opaque, target_phys_addr_t addr, unsigned int size)
{
    struct xlx_uartlite *s = opaque;
    uint32_t r = 0;
    addr >>= 2;
    switch (addr)
    {
        case R_RX:
            r = s->rx_fifo[(s->rx_fifo_pos - s->rx_fifo_len) & 7];
            if (s->rx_fifo_len)
                s->rx_fifo_len--;
            uart_update_status(s);
            uart_update_irq(s);
            break;

        default:
            if (addr < ARRAY_SIZE(s->regs))
                r = s->regs[addr];
            DUART(qemu_log("%s addr=%x v=%x\n", __func__, addr, r));
            break;
    }
    return r;
}

static void
uart_write(void *opaque, target_phys_addr_t addr,
           uint64_t val64, unsigned int size)
{
    struct xlx_uartlite *s = opaque;
    uint32_t value = val64;
    unsigned char ch = value;

    addr >>= 2;
    switch (addr)
    {
        case R_STATUS:
            hw_error("write to UART STATUS?\n");
            break;

        case R_CTRL:
            if (value & CONTROL_RST_RX) {
                s->rx_fifo_pos = 0;
                s->rx_fifo_len = 0;
            }
            s->regs[addr] = value;
            break;

        case R_TX:
            if (s->chr)
                qemu_chr_fe_write(s->chr, &ch, 1);

            s->regs[addr] = value;

            /* hax.  */
            s->regs[R_STATUS] |= STATUS_IE;
            break;

        default:
            DUART(printf("%s addr=%x v=%x\n", __func__, addr, value));
            if (addr < ARRAY_SIZE(s->regs))
                s->regs[addr] = value;
            break;
    }
    uart_update_status(s);
    uart_update_irq(s);
}

static const MemoryRegionOps uart_ops = {
    .read = uart_read,
    .write = uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    struct xlx_uartlite *s = opaque;

    /* Got a byte.  */
    if (s->rx_fifo_len >= 8) {
        printf("WARNING: UART dropped char.\n");
        return;
    }
    s->rx_fifo[s->rx_fifo_pos] = *buf;
    s->rx_fifo_pos++;
    s->rx_fifo_pos &= 0x7;
    s->rx_fifo_len++;

    uart_update_status(s);
    uart_update_irq(s);
}

static int uart_can_rx(void *opaque)
{
    struct xlx_uartlite *s = opaque;
    int r;

    r = s->rx_fifo_len < sizeof(s->rx_fifo);
    if (!r)
        printf("cannot receive!\n");
    return r;
}

static void uart_event(void *opaque, int event)
{

}

static int xilinx_uartlite_init(SysBusDevice *dev)
{
    struct xlx_uartlite *s = FROM_SYSBUS(typeof (*s), dev);

    sysbus_init_irq(dev, &s->irq);

    uart_update_status(s);
    memory_region_init_io(&s->mmio, &uart_ops, s, "xilinx-uartlite", R_MAX * 4);
    sysbus_init_mmio_region(dev, &s->mmio);

    s->chr = qdev_init_chardev(&dev->qdev);
    if (s->chr)
        qemu_chr_add_handlers(s->chr, uart_can_rx, uart_rx, uart_event, s);
    return 0;
}

static void xilinx_uart_register(void)
{
    sysbus_register_dev("xilinx,uartlite", sizeof (struct xlx_uartlite),
                        xilinx_uartlite_init);
}

device_init(xilinx_uart_register)
