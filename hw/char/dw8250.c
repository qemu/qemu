/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synopsys DesignWare APB UART (DW 8250)
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/char/dw8250.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"

#define DW_UART_REGION_SIZE 0x100

#define DW_UART_RE_EN (0xB4 >> 2) /* Receiver Output Enable Register */
#define DW_UART_DLF   (0xC0 >> 2) /* Divisor Latch Fraction Register */
#define DW_UART_CPR   (0xF4 >> 2) /* Component Parameter Register */
#define DW_UART_UCV   (0xF8 >> 2) /* UART Component Version */
#define DW_UART_CTR   (0xFC >> 2) /* Component Type Register */

#define DW_UART_UCV_VALUE 0x3332332A /* "323*" -> v3.23a */
#define DW_UART_CTR_VALUE 0x44570110 /* "DW" */

static uint64_t dw8250_ext_read(void *opaque, hwaddr addr, unsigned int size)
{
    DW8250State *s = opaque;

    switch (addr >> s->regshift) {
    case DW_UART_UCV:
        return DW_UART_UCV_VALUE;
    case DW_UART_CPR:
        return 0x00000000; /* No advanced features (DMA, extra FIFOs) */
    case DW_UART_CTR:
        return DW_UART_CTR_VALUE;

    case DW_UART_RE_EN:
    case DW_UART_DLF:
        /*
         * Return 0 to indicate these optional features
         * (RS485 and Fractional Divisor) are not implemented.
         */
        return 0x00000000;

    default:
        return 0;
    }
}

static void dw8250_ext_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write to address 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
}

static const MemoryRegionOps dw8250_ext_ops = {
    .read = dw8250_ext_read,
    .write = dw8250_ext_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void dw8250_instance_init(Object *obj)
{
    DW8250State *s = DW8250(obj);

    object_initialize_child(obj, "serial-mm", &s->serial_mm, TYPE_SERIAL_MM);
    object_property_add_alias(obj, "chardev", OBJECT(&s->serial_mm), "chardev");
}

static void dw8250_realize(DeviceState *dev, Error **errp)
{
    DW8250State *s = DW8250(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *serial_sbd = SYS_BUS_DEVICE(&s->serial_mm);

    memory_region_init(&s->container, OBJECT(dev), "dw8250-container",
                       DW_UART_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->container);

    qdev_prop_set_uint8(DEVICE(&s->serial_mm), "regshift", s->regshift);
    qdev_prop_set_uint8(DEVICE(&s->serial_mm), "endianness", DEVICE_LITTLE_ENDIAN);
    if (!sysbus_realize(serial_sbd, errp)) {
        return;
    }

    memory_region_init_io(&s->ext_iomem, OBJECT(dev), &dw8250_ext_ops, s,
                          "dw8250-ext", DW_UART_REGION_SIZE);
    memory_region_add_subregion(&s->container, 0, &s->ext_iomem);

    memory_region_add_subregion_overlap(&s->container, 0,
                                        sysbus_mmio_get_region(serial_sbd, 0), 1);

    sysbus_pass_irq(sbd, serial_sbd);
}

static const Property dw8250_properties[] = {
    DEFINE_PROP_UINT8("regshift", DW8250State, regshift, 2),
};

static void dw8250_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dw8250_realize;
    device_class_set_props(dc, dw8250_properties);
}

static const TypeInfo dw8250_info = {
    .name          = TYPE_DW8250,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DW8250State),
    .instance_init = dw8250_instance_init,
    .class_init    = dw8250_class_init,
};

static void dw8250_register_types(void)
{
    type_register_static(&dw8250_info);
}

type_init(dw8250_register_types)
