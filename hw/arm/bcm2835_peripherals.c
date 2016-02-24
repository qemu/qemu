/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "qemu/osdep.h"
#include "hw/arm/bcm2835_peripherals.h"
#include "hw/misc/bcm2835_mbox_defs.h"
#include "hw/arm/raspi_platform.h"

/* Peripheral base address on the VC (GPU) system bus */
#define BCM2835_VC_PERI_BASE 0x7e000000

/* Capabilities for SD controller: no DMA, high-speed, default clocks etc. */
#define BCM2835_SDHC_CAPAREG 0x52034b4

static void bcm2835_peripherals_init(Object *obj)
{
    BCM2835PeripheralState *s = BCM2835_PERIPHERALS(obj);

    /* Memory region for peripheral devices, which we export to our parent */
    memory_region_init(&s->peri_mr, obj,"bcm2835-peripherals", 0x1000000);
    object_property_add_child(obj, "peripheral-io", OBJECT(&s->peri_mr), NULL);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->peri_mr);

    /* Internal memory region for peripheral bus addresses (not exported) */
    memory_region_init(&s->gpu_bus_mr, obj, "bcm2835-gpu", (uint64_t)1 << 32);
    object_property_add_child(obj, "gpu-bus", OBJECT(&s->gpu_bus_mr), NULL);

    /* Internal memory region for request/response communication with
     * mailbox-addressable peripherals (not exported)
     */
    memory_region_init(&s->mbox_mr, obj, "bcm2835-mbox",
                       MBOX_CHAN_COUNT << MBOX_AS_CHAN_SHIFT);

    /* Interrupt Controller */
    object_initialize(&s->ic, sizeof(s->ic), TYPE_BCM2835_IC);
    object_property_add_child(obj, "ic", OBJECT(&s->ic), NULL);
    qdev_set_parent_bus(DEVICE(&s->ic), sysbus_get_default());

    /* UART0 */
    s->uart0 = SYS_BUS_DEVICE(object_new("pl011"));
    object_property_add_child(obj, "uart0", OBJECT(s->uart0), NULL);
    qdev_set_parent_bus(DEVICE(s->uart0), sysbus_get_default());

    /* Mailboxes */
    object_initialize(&s->mboxes, sizeof(s->mboxes), TYPE_BCM2835_MBOX);
    object_property_add_child(obj, "mbox", OBJECT(&s->mboxes), NULL);
    qdev_set_parent_bus(DEVICE(&s->mboxes), sysbus_get_default());

    object_property_add_const_link(OBJECT(&s->mboxes), "mbox-mr",
                                   OBJECT(&s->mbox_mr), &error_abort);

    /* Property channel */
    object_initialize(&s->property, sizeof(s->property), TYPE_BCM2835_PROPERTY);
    object_property_add_child(obj, "property", OBJECT(&s->property), NULL);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->property),
                              "board-rev", &error_abort);
    qdev_set_parent_bus(DEVICE(&s->property), sysbus_get_default());

    object_property_add_const_link(OBJECT(&s->property), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr), &error_abort);

    /* Extended Mass Media Controller */
    object_initialize(&s->sdhci, sizeof(s->sdhci), TYPE_SYSBUS_SDHCI);
    object_property_add_child(obj, "sdhci", OBJECT(&s->sdhci), NULL);
    qdev_set_parent_bus(DEVICE(&s->sdhci), sysbus_get_default());
}

static void bcm2835_peripherals_realize(DeviceState *dev, Error **errp)
{
    BCM2835PeripheralState *s = BCM2835_PERIPHERALS(dev);
    Object *obj;
    MemoryRegion *ram;
    Error *err = NULL;
    uint32_t ram_size;
    int n;

    obj = object_property_get_link(OBJECT(dev), "ram", &err);
    if (obj == NULL) {
        error_setg(errp, "%s: required ram link not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }

    ram = MEMORY_REGION(obj);
    ram_size = memory_region_size(ram);

    /* Map peripherals and RAM into the GPU address space. */
    memory_region_init_alias(&s->peri_mr_alias, OBJECT(s),
                             "bcm2835-peripherals", &s->peri_mr, 0,
                             memory_region_size(&s->peri_mr));

    memory_region_add_subregion_overlap(&s->gpu_bus_mr, BCM2835_VC_PERI_BASE,
                                        &s->peri_mr_alias, 1);

    /* RAM is aliased four times (different cache configurations) on the GPU */
    for (n = 0; n < 4; n++) {
        memory_region_init_alias(&s->ram_alias[n], OBJECT(s),
                                 "bcm2835-gpu-ram-alias[*]", ram, 0, ram_size);
        memory_region_add_subregion_overlap(&s->gpu_bus_mr, (hwaddr)n << 30,
                                            &s->ram_alias[n], 0);
    }

    /* Interrupt Controller */
    object_property_set_bool(OBJECT(&s->ic), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->peri_mr, ARMCTRL_IC_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ic), 0));
    sysbus_pass_irq(SYS_BUS_DEVICE(s), SYS_BUS_DEVICE(&s->ic));

    /* UART0 */
    object_property_set_bool(OBJECT(s->uart0), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->peri_mr, UART0_OFFSET,
                                sysbus_mmio_get_region(s->uart0, 0));
    sysbus_connect_irq(s->uart0, 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_UART));

    /* Mailboxes */
    object_property_set_bool(OBJECT(&s->mboxes), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->peri_mr, ARMCTRL_0_SBM_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mboxes), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mboxes), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_ARM_IRQ,
                               INTERRUPT_ARM_MAILBOX));

    /* Property channel */
    object_property_set_int(OBJECT(&s->property), ram_size, "ram-size", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->property), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->mbox_mr,
                MBOX_CHAN_PROPERTY << MBOX_AS_CHAN_SHIFT,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->property), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->property), 0,
                      qdev_get_gpio_in(DEVICE(&s->mboxes), MBOX_CHAN_PROPERTY));

    /* Extended Mass Media Controller */
    object_property_set_int(OBJECT(&s->sdhci), BCM2835_SDHC_CAPAREG, "capareg",
                            &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->sdhci), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(&s->peri_mr, EMMC_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sdhci), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_ARASANSDIO));
    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->sdhci), "sd-bus",
                              &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

}

static void bcm2835_peripherals_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = bcm2835_peripherals_realize;
}

static const TypeInfo bcm2835_peripherals_type_info = {
    .name = TYPE_BCM2835_PERIPHERALS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835PeripheralState),
    .instance_init = bcm2835_peripherals_init,
    .class_init = bcm2835_peripherals_class_init,
};

static void bcm2835_peripherals_register_types(void)
{
    type_register_static(&bcm2835_peripherals_type_info);
}

type_init(bcm2835_peripherals_register_types)
