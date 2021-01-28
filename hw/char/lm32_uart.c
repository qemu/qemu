/*
 *  QEMU model of the LatticeMico32 UART block.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
 *   http://www.latticesemi.com/documents/mico32uart.pdf
 */


#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "chardev/char-fe.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qom/object.h"

enum {
    R_RXTX = 0,
    R_IER,
    R_IIR,
    R_LCR,
    R_MCR,
    R_LSR,
    R_MSR,
    R_DIV,
    R_MAX
};

enum {
    IER_RBRI = (1<<0),
    IER_THRI = (1<<1),
    IER_RLSI = (1<<2),
    IER_MSI  = (1<<3),
};

enum {
    IIR_STAT = (1<<0),
    IIR_ID0  = (1<<1),
    IIR_ID1  = (1<<2),
};

enum {
    LCR_WLS0 = (1<<0),
    LCR_WLS1 = (1<<1),
    LCR_STB  = (1<<2),
    LCR_PEN  = (1<<3),
    LCR_EPS  = (1<<4),
    LCR_SP   = (1<<5),
    LCR_SB   = (1<<6),
};

enum {
    MCR_DTR  = (1<<0),
    MCR_RTS  = (1<<1),
};

enum {
    LSR_DR   = (1<<0),
    LSR_OE   = (1<<1),
    LSR_PE   = (1<<2),
    LSR_FE   = (1<<3),
    LSR_BI   = (1<<4),
    LSR_THRE = (1<<5),
    LSR_TEMT = (1<<6),
};

enum {
    MSR_DCTS = (1<<0),
    MSR_DDSR = (1<<1),
    MSR_TERI = (1<<2),
    MSR_DDCD = (1<<3),
    MSR_CTS  = (1<<4),
    MSR_DSR  = (1<<5),
    MSR_RI   = (1<<6),
    MSR_DCD  = (1<<7),
};

#define TYPE_LM32_UART "lm32-uart"
OBJECT_DECLARE_SIMPLE_TYPE(LM32UartState, LM32_UART)

struct LM32UartState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;

    uint32_t regs[R_MAX];
};

static void uart_update_irq(LM32UartState *s)
{
    unsigned int irq;

    if ((s->regs[R_LSR] & (LSR_OE | LSR_PE | LSR_FE | LSR_BI))
            && (s->regs[R_IER] & IER_RLSI)) {
        irq = 1;
        s->regs[R_IIR] = IIR_ID1 | IIR_ID0;
    } else if ((s->regs[R_LSR] & LSR_DR) && (s->regs[R_IER] & IER_RBRI)) {
        irq = 1;
        s->regs[R_IIR] = IIR_ID1;
    } else if ((s->regs[R_LSR] & LSR_THRE) && (s->regs[R_IER] & IER_THRI)) {
        irq = 1;
        s->regs[R_IIR] = IIR_ID0;
    } else if ((s->regs[R_MSR] & 0x0f) && (s->regs[R_IER] & IER_MSI)) {
        irq = 1;
        s->regs[R_IIR] = 0;
    } else {
        irq = 0;
        s->regs[R_IIR] = IIR_STAT;
    }

    trace_lm32_uart_irq_state(irq);
    qemu_set_irq(s->irq, irq);
}

static uint64_t uart_read(void *opaque, hwaddr addr,
                          unsigned size)
{
    LM32UartState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_RXTX:
        r = s->regs[R_RXTX];
        s->regs[R_LSR] &= ~LSR_DR;
        uart_update_irq(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case R_IIR:
    case R_LSR:
    case R_MSR:
        r = s->regs[addr];
        break;
    case R_IER:
    case R_LCR:
    case R_MCR:
    case R_DIV:
        error_report("lm32_uart: read access to write only register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    default:
        error_report("lm32_uart: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_lm32_uart_memory_read(addr << 2, r);
    return r;
}

static void uart_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned size)
{
    LM32UartState *s = opaque;
    unsigned char ch = value;

    trace_lm32_uart_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_RXTX:
        /* XXX this blocks entire thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks */
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        break;
    case R_IER:
    case R_LCR:
    case R_MCR:
    case R_DIV:
        s->regs[addr] = value;
        break;
    case R_IIR:
    case R_LSR:
    case R_MSR:
        error_report("lm32_uart: write access to read only register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    default:
        error_report("lm32_uart: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
    uart_update_irq(s);
}

static const MemoryRegionOps uart_ops = {
    .read = uart_read,
    .write = uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    LM32UartState *s = opaque;

    if (s->regs[R_LSR] & LSR_DR) {
        s->regs[R_LSR] |= LSR_OE;
    }

    s->regs[R_LSR] |= LSR_DR;
    s->regs[R_RXTX] = *buf;

    uart_update_irq(s);
}

static int uart_can_rx(void *opaque)
{
    LM32UartState *s = opaque;

    return !(s->regs[R_LSR] & LSR_DR);
}

static void uart_event(void *opaque, QEMUChrEvent event)
{
}

static void uart_reset(DeviceState *d)
{
    LM32UartState *s = LM32_UART(d);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }

    /* defaults */
    s->regs[R_LSR] = LSR_THRE | LSR_TEMT;
}

static void lm32_uart_init(Object *obj)
{
    LM32UartState *s = LM32_UART(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(dev, &s->irq);

    memory_region_init_io(&s->iomem, obj, &uart_ops, s,
                          "uart", R_MAX * 4);
    sysbus_init_mmio(dev, &s->iomem);
}

static void lm32_uart_realize(DeviceState *dev, Error **errp)
{
    LM32UartState *s = LM32_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, uart_can_rx, uart_rx,
                             uart_event, NULL, s, NULL, true);
}

static const VMStateDescription vmstate_lm32_uart = {
    .name = "lm32-uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LM32UartState, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property lm32_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", LM32UartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void lm32_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = uart_reset;
    dc->vmsd = &vmstate_lm32_uart;
    device_class_set_props(dc, lm32_uart_properties);
    dc->realize = lm32_uart_realize;
}

static const TypeInfo lm32_uart_info = {
    .name          = TYPE_LM32_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LM32UartState),
    .instance_init = lm32_uart_init,
    .class_init    = lm32_uart_class_init,
};

static void lm32_uart_register_types(void)
{
    type_register_static(&lm32_uart_info);
}

type_init(lm32_uart_register_types)
