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
    R_MAX
};

struct MilkymistUartState {
    SysBusDevice busdev;
    MemoryRegion regs_region;
    CharDriverState *chr;

    qemu_irq rx_irq;
    qemu_irq tx_irq;

    uint32_t regs[R_MAX];
};
typedef struct MilkymistUartState MilkymistUartState;

static uint64_t uart_read(void *opaque, target_phys_addr_t addr,
                          unsigned size)
{
    MilkymistUartState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_RXTX:
    case R_DIV:
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

static void uart_write(void *opaque, target_phys_addr_t addr, uint64_t value,
                       unsigned size)
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
        trace_milkymist_uart_pulse_irq_tx();
        qemu_irq_pulse(s->tx_irq);
        break;
    case R_DIV:
        s->regs[addr] = value;
        break;

    default:
        error_report("milkymist_uart: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static const MemoryRegionOps uart_mmio_ops = {
    .read = uart_read,
    .write = uart_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    MilkymistUartState *s = opaque;

    s->regs[R_RXTX] = *buf;
    trace_milkymist_uart_pulse_irq_rx();
    qemu_irq_pulse(s->rx_irq);
}

static int uart_can_rx(void *opaque)
{
    return 1;
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
}

static int milkymist_uart_init(SysBusDevice *dev)
{
    MilkymistUartState *s = FROM_SYSBUS(typeof(*s), dev);

    sysbus_init_irq(dev, &s->rx_irq);
    sysbus_init_irq(dev, &s->tx_irq);

    memory_region_init_io(&s->regs_region, &uart_mmio_ops, s,
            "milkymist-uart", R_MAX * 4);
    sysbus_init_mmio_region(dev, &s->regs_region);

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
