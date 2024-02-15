/*
 * ASPEED SoC family
 *
 * Andrew Jeffery <andrew@aj.id.au>
 * Jeremy Kerr <jk@ozlabs.org>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/char/serial.h"


const char *aspeed_soc_cpu_type(AspeedSoCClass *sc)
{
    assert(sc->valid_cpu_types);
    assert(sc->valid_cpu_types[0]);
    assert(!sc->valid_cpu_types[1]);
    return sc->valid_cpu_types[0];
}

qemu_irq aspeed_soc_get_irq(AspeedSoCState *s, int dev)
{
    return ASPEED_SOC_GET_CLASS(s)->get_irq(s, dev);
}

bool aspeed_soc_uart_realize(AspeedSoCState *s, Error **errp)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    SerialMM *smm;

    for (int i = 0, uart = sc->uarts_base; i < sc->uarts_num; i++, uart++) {
        smm = &s->uart[i];

        /* Chardev property is set by the machine. */
        qdev_prop_set_uint8(DEVICE(smm), "regshift", 2);
        qdev_prop_set_uint32(DEVICE(smm), "baudbase", 38400);
        qdev_set_legacy_instance_id(DEVICE(smm), sc->memmap[uart], 2);
        qdev_prop_set_uint8(DEVICE(smm), "endianness", DEVICE_LITTLE_ENDIAN);
        if (!sysbus_realize(SYS_BUS_DEVICE(smm), errp)) {
            return false;
        }

        sysbus_connect_irq(SYS_BUS_DEVICE(smm), 0, aspeed_soc_get_irq(s, uart));
        aspeed_mmio_map(s, SYS_BUS_DEVICE(smm), 0, sc->memmap[uart]);
    }

    return true;
}

void aspeed_soc_uart_set_chr(AspeedSoCState *s, int dev, Chardev *chr)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int uart_first = aspeed_uart_first(sc);
    int uart_index = aspeed_uart_index(dev);
    int i = uart_index - uart_first;

    g_assert(0 <= i && i < ARRAY_SIZE(s->uart) && i < sc->uarts_num);
    qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", chr);
}

/*
 * SDMC should be realized first to get correct RAM size and max size
 * values
 */
bool aspeed_soc_dram_init(AspeedSoCState *s, Error **errp)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    ram_addr_t ram_size, max_ram_size;

    ram_size = object_property_get_uint(OBJECT(&s->sdmc), "ram-size",
                                        &error_abort);
    max_ram_size = object_property_get_uint(OBJECT(&s->sdmc), "max-ram-size",
                                            &error_abort);

    memory_region_init(&s->dram_container, OBJECT(s), "ram-container",
                       max_ram_size);
    memory_region_add_subregion(&s->dram_container, 0, s->dram_mr);

    /*
     * Add a memory region beyond the RAM region to let firmwares scan
     * the address space with load/store and guess how much RAM the
     * SoC has.
     */
    if (ram_size < max_ram_size) {
        DeviceState *dev = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);

        qdev_prop_set_string(dev, "name", "ram-empty");
        qdev_prop_set_uint64(dev, "size", max_ram_size  - ram_size);
        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp)) {
            return false;
        }

        memory_region_add_subregion_overlap(&s->dram_container, ram_size,
                      sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), -1000);
    }

    memory_region_add_subregion(s->memory,
                      sc->memmap[ASPEED_DEV_SDRAM], &s->dram_container);
    return true;
}

void aspeed_mmio_map(AspeedSoCState *s, SysBusDevice *dev, int n, hwaddr addr)
{
    memory_region_add_subregion(s->memory, addr,
                                sysbus_mmio_get_region(dev, n));
}

void aspeed_mmio_map_unimplemented(AspeedSoCState *s, SysBusDevice *dev,
                                   const char *name, hwaddr addr, uint64_t size)
{
    qdev_prop_set_string(DEVICE(dev), "name", name);
    qdev_prop_set_uint64(DEVICE(dev), "size", size);
    sysbus_realize(dev, &error_abort);

    memory_region_add_subregion_overlap(s->memory, addr,
                                        sysbus_mmio_get_region(dev, 0), -1000);
}

static void aspeed_soc_realize(DeviceState *dev, Error **errp)
{
    AspeedSoCState *s = ASPEED_SOC(dev);

    if (!s->memory) {
        error_setg(errp, "'memory' link is not set");
        return;
    }
}

static Property aspeed_soc_properties[] = {
    DEFINE_PROP_LINK("dram", AspeedSoCState, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("memory", AspeedSoCState, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aspeed_soc_realize;
    device_class_set_props(dc, aspeed_soc_properties);
}

static const TypeInfo aspeed_soc_types[] = {
    {
        .name           = TYPE_ASPEED_SOC,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(AspeedSoCState),
        .class_size     = sizeof(AspeedSoCClass),
        .class_init     = aspeed_soc_class_init,
        .abstract       = true,
    },
};

DEFINE_TYPES(aspeed_soc_types)
