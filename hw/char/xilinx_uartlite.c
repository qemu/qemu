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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

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

#define TYPE_XILINX_UARTLITE "xlnx.xps-uartlite"
OBJECT_DECLARE_SIMPLE_TYPE(XilinxUARTLite, XILINX_UARTLITE)

struct XilinxUARTLite {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharBackend chr;
    qemu_irq irq;

    uint8_t rx_fifo[8];
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;

    uint32_t regs[R_MAX];
};

static void uart_update_irq(XilinxUARTLite *s)
{
    unsigned int irq;

    if (s->rx_fifo_len)
        s->regs[R_STATUS] |= STATUS_IE;

    irq = (s->regs[R_STATUS] & STATUS_IE) && (s->regs[R_CTRL] & CONTROL_IE);
    qemu_set_irq(s->irq, irq);
}

static void uart_update_status(XilinxUARTLite *s)
{
    uint32_t r;

    r = s->regs[R_STATUS];
    r &= ~7;
    r |= 1 << 2; /* Tx fifo is always empty. We are fast :) */
    r |= (s->rx_fifo_len == sizeof (s->rx_fifo)) << 1;
    r |= (!!s->rx_fifo_len);
    s->regs[R_STATUS] = r;
}

static void xilinx_uartlite_reset(DeviceState *dev)
{
    uart_update_status(XILINX_UARTLITE(dev));
}

static uint64_t
uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    XilinxUARTLite *s = opaque;
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
            qemu_chr_fe_accept_input(&s->chr);
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
uart_write(void *opaque, hwaddr addr,
           uint64_t val64, unsigned int size)
{
    XilinxUARTLite *s = opaque;
    uint32_t value = val64;
    unsigned char ch = value;

    addr >>= 2;
    switch (addr)
    {
        case R_STATUS:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: write to UART STATUS\n",
                          __func__);
            break;

        case R_CTRL:
            if (value & CONTROL_RST_RX) {
                s->rx_fifo_pos = 0;
                s->rx_fifo_len = 0;
            }
            s->regs[addr] = value;
            break;

        case R_TX:
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
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

static Property xilinx_uartlite_properties[] = {
    DEFINE_PROP_CHR("chardev", XilinxUARTLite, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    XilinxUARTLite *s = opaque;

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
    XilinxUARTLite *s = opaque;

    return s->rx_fifo_len < sizeof(s->rx_fifo);
}

static void uart_event(void *opaque, QEMUChrEvent event)
{

}

static void xilinx_uartlite_realize(DeviceState *dev, Error **errp)
{
    XilinxUARTLite *s = XILINX_UARTLITE(dev);

    qemu_chr_fe_set_handlers(&s->chr, uart_can_rx, uart_rx,
                             uart_event, NULL, s, NULL, true);
}

static void xilinx_uartlite_init(Object *obj)
{
    XilinxUARTLite *s = XILINX_UARTLITE(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &uart_ops, s,
                          "xlnx.xps-uartlite", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void xilinx_uartlite_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xilinx_uartlite_reset;
    dc->realize = xilinx_uartlite_realize;
    device_class_set_props(dc, xilinx_uartlite_properties);
}

static const TypeInfo xilinx_uartlite_info = {
    .name          = TYPE_XILINX_UARTLITE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxUARTLite),
    .instance_init = xilinx_uartlite_init,
    .class_init    = xilinx_uartlite_class_init,
};

static void xilinx_uart_register_types(void)
{
    type_register_static(&xilinx_uartlite_info);
}

type_init(xilinx_uart_register_types)
