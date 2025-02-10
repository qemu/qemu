/*
 * SHAKTI UART
 *
 * Copyright (c) 2021 Vijai Kumar K <vijai@behindbytes.com>
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
#include "hw/char/shakti_uart.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/log.h"

static uint64_t shakti_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    ShaktiUartState *s = opaque;

    switch (addr) {
    case SHAKTI_UART_BAUD:
        return s->uart_baud;
    case SHAKTI_UART_RX:
        qemu_chr_fe_accept_input(&s->chr);
        s->uart_status &= ~SHAKTI_UART_STATUS_RX_NOT_EMPTY;
        return s->uart_rx;
    case SHAKTI_UART_STATUS:
        return s->uart_status;
    case SHAKTI_UART_DELAY:
        return s->uart_delay;
    case SHAKTI_UART_CONTROL:
        return s->uart_control;
    case SHAKTI_UART_INT_EN:
        return s->uart_interrupt;
    case SHAKTI_UART_IQ_CYCLES:
        return s->uart_iq_cycles;
    case SHAKTI_UART_RX_THRES:
        return s->uart_rx_threshold;
    default:
        /* Also handles TX REG which is write only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }

    return 0;
}

static void shakti_uart_write(void *opaque, hwaddr addr,
                              uint64_t data, unsigned size)
{
    ShaktiUartState *s = opaque;
    uint32_t value = data;
    uint8_t ch;

    switch (addr) {
    case SHAKTI_UART_BAUD:
        s->uart_baud = value;
        break;
    case SHAKTI_UART_TX:
        ch = value;
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        s->uart_status &= ~SHAKTI_UART_STATUS_TX_FULL;
        break;
    case SHAKTI_UART_STATUS:
        s->uart_status = value;
        break;
    case SHAKTI_UART_DELAY:
        s->uart_delay = value;
        break;
    case SHAKTI_UART_CONTROL:
        s->uart_control = value;
        break;
    case SHAKTI_UART_INT_EN:
        s->uart_interrupt = value;
        break;
    case SHAKTI_UART_IQ_CYCLES:
        s->uart_iq_cycles = value;
        break;
    case SHAKTI_UART_RX_THRES:
        s->uart_rx_threshold = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps shakti_uart_ops = {
    .read = shakti_uart_read,
    .write = shakti_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.min_access_size = 1, .max_access_size = 4},
    .valid = {.min_access_size = 1, .max_access_size = 4},
};

static void shakti_uart_reset(DeviceState *dev)
{
    ShaktiUartState *s = SHAKTI_UART(dev);

    s->uart_baud = SHAKTI_UART_BAUD_DEFAULT;
    s->uart_tx = 0x0;
    s->uart_rx = 0x0;
    s->uart_status = 0x0000;
    s->uart_delay = 0x0000;
    s->uart_control = SHAKTI_UART_CONTROL_DEFAULT;
    s->uart_interrupt = 0x0000;
    s->uart_iq_cycles = 0x00;
    s->uart_rx_threshold = 0x00;
}

static int shakti_uart_can_receive(void *opaque)
{
    ShaktiUartState *s = opaque;

    return !(s->uart_status & SHAKTI_UART_STATUS_RX_NOT_EMPTY);
}

static void shakti_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    ShaktiUartState *s = opaque;

    s->uart_rx = *buf;
    s->uart_status |= SHAKTI_UART_STATUS_RX_NOT_EMPTY;
}

static void shakti_uart_realize(DeviceState *dev, Error **errp)
{
    ShaktiUartState *sus = SHAKTI_UART(dev);
    qemu_chr_fe_set_handlers(&sus->chr, shakti_uart_can_receive,
                             shakti_uart_receive, NULL, NULL, sus, NULL, true);
}

static void shakti_uart_instance_init(Object *obj)
{
    ShaktiUartState *sus = SHAKTI_UART(obj);
    memory_region_init_io(&sus->mmio,
                          obj,
                          &shakti_uart_ops,
                          sus,
                          TYPE_SHAKTI_UART,
                          0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &sus->mmio);
}

static const Property shakti_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", ShaktiUartState, chr),
};

static void shakti_uart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, shakti_uart_reset);
    dc->realize = shakti_uart_realize;
    device_class_set_props(dc, shakti_uart_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo shakti_uart_info = {
    .name = TYPE_SHAKTI_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ShaktiUartState),
    .class_init = shakti_uart_class_init,
    .instance_init = shakti_uart_instance_init,
};

static void shakti_uart_register_types(void)
{
    type_register_static(&shakti_uart_info);
}
type_init(shakti_uart_register_types)
