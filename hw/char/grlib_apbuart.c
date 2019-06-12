/*
 * QEMU GRLIB APB UART Emulator
 *
 * Copyright (c) 2010-2019 AdaCore
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
#include "hw/sparc/grlib.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "chardev/char-fe.h"

#include "trace.h"

#define UART_REG_SIZE 20     /* Size of memory mapped registers */

/* UART status register fields */
#define UART_DATA_READY           (1 <<  0)
#define UART_TRANSMIT_SHIFT_EMPTY (1 <<  1)
#define UART_TRANSMIT_FIFO_EMPTY  (1 <<  2)
#define UART_BREAK_RECEIVED       (1 <<  3)
#define UART_OVERRUN              (1 <<  4)
#define UART_PARITY_ERROR         (1 <<  5)
#define UART_FRAMING_ERROR        (1 <<  6)
#define UART_TRANSMIT_FIFO_HALF   (1 <<  7)
#define UART_RECEIVE_FIFO_HALF    (1 <<  8)
#define UART_TRANSMIT_FIFO_FULL   (1 <<  9)
#define UART_RECEIVE_FIFO_FULL    (1 << 10)

/* UART control register fields */
#define UART_RECEIVE_ENABLE          (1 <<  0)
#define UART_TRANSMIT_ENABLE         (1 <<  1)
#define UART_RECEIVE_INTERRUPT       (1 <<  2)
#define UART_TRANSMIT_INTERRUPT      (1 <<  3)
#define UART_PARITY_SELECT           (1 <<  4)
#define UART_PARITY_ENABLE           (1 <<  5)
#define UART_FLOW_CONTROL            (1 <<  6)
#define UART_LOOPBACK                (1 <<  7)
#define UART_EXTERNAL_CLOCK          (1 <<  8)
#define UART_RECEIVE_FIFO_INTERRUPT  (1 <<  9)
#define UART_TRANSMIT_FIFO_INTERRUPT (1 << 10)
#define UART_FIFO_DEBUG_MODE         (1 << 11)
#define UART_OUTPUT_ENABLE           (1 << 12)
#define UART_FIFO_AVAILABLE          (1 << 31)

/* Memory mapped register offsets */
#define DATA_OFFSET       0x00
#define STATUS_OFFSET     0x04
#define CONTROL_OFFSET    0x08
#define SCALER_OFFSET     0x0C  /* not supported */
#define FIFO_DEBUG_OFFSET 0x10  /* not supported */

#define FIFO_LENGTH 1024

#define GRLIB_APB_UART(obj) \
    OBJECT_CHECK(UART, (obj), TYPE_GRLIB_APB_UART)

typedef struct UART {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    CharBackend chr;

    /* registers */
    uint32_t status;
    uint32_t control;

    /* FIFO */
    char buffer[FIFO_LENGTH];
    int  len;
    int  current;
} UART;

static int uart_data_to_read(UART *uart)
{
    return uart->current < uart->len;
}

static char uart_pop(UART *uart)
{
    char ret;

    if (uart->len == 0) {
        uart->status &= ~UART_DATA_READY;
        return 0;
    }

    ret = uart->buffer[uart->current++];

    if (uart->current >= uart->len) {
        /* Flush */
        uart->len     = 0;
        uart->current = 0;
    }

    if (!uart_data_to_read(uart)) {
        uart->status &= ~UART_DATA_READY;
    }

    return ret;
}

static void uart_add_to_fifo(UART          *uart,
                             const uint8_t *buffer,
                             int            length)
{
    if (uart->len + length > FIFO_LENGTH) {
        abort();
    }
    memcpy(uart->buffer + uart->len, buffer, length);
    uart->len += length;
}

static int grlib_apbuart_can_receive(void *opaque)
{
    UART *uart = opaque;

    return FIFO_LENGTH - uart->len;
}

static void grlib_apbuart_receive(void *opaque, const uint8_t *buf, int size)
{
    UART *uart = opaque;

    if (uart->control & UART_RECEIVE_ENABLE) {
        uart_add_to_fifo(uart, buf, size);

        uart->status |= UART_DATA_READY;

        if (uart->control & UART_RECEIVE_INTERRUPT) {
            qemu_irq_pulse(uart->irq);
        }
    }
}

static void grlib_apbuart_event(void *opaque, int event)
{
    trace_grlib_apbuart_event(event);
}


static uint64_t grlib_apbuart_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    UART     *uart = opaque;

    addr &= 0xff;

    /* Unit registers */
    switch (addr) {
    case DATA_OFFSET:
    case DATA_OFFSET + 3:       /* when only one byte read */
        return uart_pop(uart);

    case STATUS_OFFSET:
        /* Read Only */
        return uart->status;

    case CONTROL_OFFSET:
        return uart->control;

    case SCALER_OFFSET:
        /* Not supported */
        return 0;

    default:
        trace_grlib_apbuart_readl_unknown(addr);
        return 0;
    }
}

static void grlib_apbuart_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    UART          *uart = opaque;
    unsigned char  c    = 0;

    addr &= 0xff;

    /* Unit registers */
    switch (addr) {
    case DATA_OFFSET:
    case DATA_OFFSET + 3:       /* When only one byte write */
        /* Transmit when character device available and transmitter enabled */
        if (qemu_chr_fe_backend_connected(&uart->chr) &&
            (uart->control & UART_TRANSMIT_ENABLE)) {
            c = value & 0xFF;
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&uart->chr, &c, 1);
            /* Generate interrupt */
            if (uart->control & UART_TRANSMIT_INTERRUPT) {
                qemu_irq_pulse(uart->irq);
            }
        }
        return;

    case STATUS_OFFSET:
        /* Read Only */
        return;

    case CONTROL_OFFSET:
        uart->control = value;
        return;

    case SCALER_OFFSET:
        /* Not supported */
        return;

    default:
        break;
    }

    trace_grlib_apbuart_writel_unknown(addr, value);
}

static const MemoryRegionOps grlib_apbuart_ops = {
    .write      = grlib_apbuart_write,
    .read       = grlib_apbuart_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void grlib_apbuart_realize(DeviceState *dev, Error **errp)
{
    UART *uart = GRLIB_APB_UART(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qemu_chr_fe_set_handlers(&uart->chr,
                             grlib_apbuart_can_receive,
                             grlib_apbuart_receive,
                             grlib_apbuart_event,
                             NULL, uart, NULL, true);

    sysbus_init_irq(sbd, &uart->irq);

    memory_region_init_io(&uart->iomem, OBJECT(uart), &grlib_apbuart_ops, uart,
                          "uart", UART_REG_SIZE);

    sysbus_init_mmio(sbd, &uart->iomem);
}

static void grlib_apbuart_reset(DeviceState *d)
{
    UART *uart = GRLIB_APB_UART(d);

    /* Transmitter FIFO and shift registers are always empty in QEMU */
    uart->status =  UART_TRANSMIT_FIFO_EMPTY | UART_TRANSMIT_SHIFT_EMPTY;
    /* Everything is off */
    uart->control = 0;
    /* Flush receive FIFO */
    uart->len = 0;
    uart->current = 0;
}

static Property grlib_apbuart_properties[] = {
    DEFINE_PROP_CHR("chrdev", UART, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void grlib_apbuart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = grlib_apbuart_realize;
    dc->reset = grlib_apbuart_reset;
    dc->props = grlib_apbuart_properties;
}

static const TypeInfo grlib_apbuart_info = {
    .name          = TYPE_GRLIB_APB_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(UART),
    .class_init    = grlib_apbuart_class_init,
};

static void grlib_apbuart_register_types(void)
{
    type_register_static(&grlib_apbuart_info);
}

type_init(grlib_apbuart_register_types)
