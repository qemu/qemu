/*
 * QEMU GRLIB APB UART Emulator
 *
 * Copyright (c) 2010-2011 AdaCore
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

typedef struct UART {
    SysBusDevice busdev;

    qemu_irq irq;

    CharDriverState *chr;

    /* registers */
    uint32_t receive;
    uint32_t status;
    uint32_t control;
} UART;

static int grlib_apbuart_can_receive(void *opaque)
{
    UART *uart = opaque;

    return !!(uart->status & UART_DATA_READY);
}

static void grlib_apbuart_receive(void *opaque, const uint8_t *buf, int size)
{
    UART *uart = opaque;

    uart->receive  = *buf;
    uart->status  |= UART_DATA_READY;

    if (uart->control & UART_RECEIVE_INTERRUPT) {
        qemu_irq_pulse(uart->irq);
    }
}

static void grlib_apbuart_event(void *opaque, int event)
{
    trace_grlib_apbuart_event(event);
}

static void
grlib_apbuart_writel(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    UART          *uart = opaque;
    unsigned char  c    = 0;

    addr &= 0xff;

    /* Unit registers */
    switch (addr) {
    case DATA_OFFSET:
        c = value & 0xFF;
        qemu_chr_write(uart->chr, &c, 1);
        return;

    case STATUS_OFFSET:
        /* Read Only */
        return;

    case CONTROL_OFFSET:
        /* Not supported */
        return;

    case SCALER_OFFSET:
        /* Not supported */
        return;

    default:
        break;
    }

    trace_grlib_apbuart_unknown_register("write", addr);
}

static CPUReadMemoryFunc * const grlib_apbuart_read[] = {
    NULL, NULL, NULL,
};

static CPUWriteMemoryFunc * const grlib_apbuart_write[] = {
    NULL, NULL, grlib_apbuart_writel,
};

static int grlib_apbuart_init(SysBusDevice *dev)
{
    UART *uart      = FROM_SYSBUS(typeof(*uart), dev);
    int   uart_regs = 0;

    qemu_chr_add_handlers(uart->chr,
                          grlib_apbuart_can_receive,
                          grlib_apbuart_receive,
                          grlib_apbuart_event,
                          uart);

    sysbus_init_irq(dev, &uart->irq);

    uart_regs = cpu_register_io_memory(grlib_apbuart_read,
                                       grlib_apbuart_write,
                                       uart, DEVICE_NATIVE_ENDIAN);
    if (uart_regs < 0) {
        return -1;
    }

    sysbus_init_mmio(dev, UART_REG_SIZE, uart_regs);

    return 0;
}

static SysBusDeviceInfo grlib_gptimer_info = {
    .init       = grlib_apbuart_init,
    .qdev.name  = "grlib,apbuart",
    .qdev.size  = sizeof(UART),
    .qdev.props = (Property[]) {
        DEFINE_PROP_CHR("chrdev", UART, chr),
        DEFINE_PROP_END_OF_LIST()
    }
};

static void grlib_gptimer_register(void)
{
    sysbus_register_withprop(&grlib_gptimer_info);
}

device_init(grlib_gptimer_register)
