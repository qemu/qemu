/*
 *  QEMU model of the Milkymist UART block.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://www.milkymist.org/socdoc/uart.pdf
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "qemu-char.h"
#include "qemu-error.h"

enum {
    R_RXTX = 0,
    R_DIV,
    R_STAT,
    R_CTRL,
    R_DBG,
    R_MAX
};

enum {
    STAT_THRE   = (1<<0),
    STAT_RX_EVT = (1<<1),
    STAT_TX_EVT = (1<<2),
};

enum {
    CTRL_RX_IRQ_EN = (1<<0),
    CTRL_TX_IRQ_EN = (1<<1),
    CTRL_THRU_EN   = (1<<2),
};

enum {
    DBG_BREAK_EN = (1<<0),
};

struct MilkymistUartState {
    SysBusDevice busdev;
    CharDriverState *chr;
    qemu_irq irq;

    uint32_t regs[R_MAX];
};
typedef struct MilkymistUartState MilkymistUartState;

static void uart_update_irq(MilkymistUartState *s)
{
    int rx_event = s->regs[R_STAT] & STAT_RX_EVT;
    int tx_event = s->regs[R_STAT] & STAT_TX_EVT;
    int rx_irq_en = s->regs[R_CTRL] & CTRL_RX_IRQ_EN;
    int tx_irq_en = s->regs[R_CTRL] & CTRL_TX_IRQ_EN;

    if ((rx_irq_en && rx_event) || (tx_irq_en && tx_event)) {
        trace_milkymist_uart_raise_irq();
        qemu_irq_raise(s->irq);
    } else {
        trace_milkymist_uart_lower_irq();
        qemu_irq_lower(s->irq);
    }
}

static uint32_t uart_read(void *opaque, target_phys_addr_t addr)
{
    MilkymistUartState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_RXTX:
        r = s->regs[addr];
        break;
    case R_DIV:
    case R_STAT:
    case R_CTRL:
    case R_DBG:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_uart: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_uart_memory_read(addr << 2, r);

    return r;
}

static void uart_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MilkymistUartState *s = opaque;
    unsigned char ch = value;

    trace_milkymist_uart_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_RXTX:
        if (s->chr) {
            qemu_chr_fe_write(s->chr, &ch, 1);
        }
        s->regs[R_STAT] |= STAT_TX_EVT;
        break;
    case R_DIV:
    case R_CTRL:
    case R_DBG:
        s->regs[addr] = value;
        break;

    case R_STAT:
        /* write one to clear bits */
        s->regs[addr] &= ~(value & (STAT_RX_EVT | STAT_TX_EVT));
        break;

    default:
        error_report("milkymist_uart: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    uart_update_irq(s);
}

static CPUReadMemoryFunc * const uart_read_fn[] = {
    NULL,
    NULL,
    &uart_read,
};

static CPUWriteMemoryFunc * const uart_write_fn[] = {
    NULL,
    NULL,
    &uart_write,
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    MilkymistUartState *s = opaque;

    assert(!(s->regs[R_STAT] & STAT_RX_EVT));

    s->regs[R_STAT] |= STAT_RX_EVT;
    s->regs[R_RXTX] = *buf;

    uart_update_irq(s);
}

static int uart_can_rx(void *opaque)
{
    MilkymistUartState *s = opaque;

    return !(s->regs[R_STAT] & STAT_RX_EVT);
}

static void uart_event(void *opaque, int event)
{
}

static void milkymist_uart_reset(DeviceState *d)
{
    MilkymistUartState *s = container_of(d, MilkymistUartState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

    /* THRE is always set */
    s->regs[R_STAT] = STAT_THRE;
}

static int milkymist_uart_init(SysBusDevice *dev)
{
    MilkymistUartState *s = FROM_SYSBUS(typeof(*s), dev);
    int uart_regs;

    sysbus_init_irq(dev, &s->irq);

    uart_regs = cpu_register_io_memory(uart_read_fn, uart_write_fn, s,
            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, uart_regs);

    s->chr = qdev_init_chardev(&dev->qdev);
    if (s->chr) {
        qemu_chr_add_handlers(s->chr, uart_can_rx, uart_rx, uart_event, s);
    }

    return 0;
}

static const VMStateDescription vmstate_milkymist_uart = {
    .name = "milkymist-uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistUartState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo milkymist_uart_info = {
    .init = milkymist_uart_init,
    .qdev.name  = "milkymist-uart",
    .qdev.size  = sizeof(MilkymistUartState),
    .qdev.vmsd  = &vmstate_milkymist_uart,
    .qdev.reset = milkymist_uart_reset,
};

static void milkymist_uart_register(void)
{
    sysbus_register_withprop(&milkymist_uart_info);
}

device_init(milkymist_uart_register)
