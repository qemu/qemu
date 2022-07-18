/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/bcm2835_peripherals.h"
#include "hw/misc/bcm2835_mbox_defs.h"
#include "hw/arm/raspi_platform.h"
#include "sysemu/sysemu.h"

/* Peripheral base address on the VC (GPU) system bus */
#define BCM2835_VC_PERI_BASE 0x7e000000

/* Capabilities for SD controller: no DMA, high-speed, default clocks etc. */
#define BCM2835_SDHC_CAPAREG 0x52134b4

/*
 * According to Linux driver & DTS, dma channels 0--10 have separate IRQ,
 * while channels 11--14 share one IRQ:
 */
#define SEPARATE_DMA_IRQ_MAX 10
#define ORGATED_DMA_IRQ_COUNT 4

static void create_unimp(BCM2835PeripheralState *ps,
                         UnimplementedDeviceState *uds,
                         const char *name, hwaddr ofs, hwaddr size)
{
    object_initialize_child(OBJECT(ps), name, uds, TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(DEVICE(uds), "name", name);
    qdev_prop_set_uint64(DEVICE(uds), "size", size);
    sysbus_realize(SYS_BUS_DEVICE(uds), &error_fatal);
    memory_region_add_subregion_overlap(&ps->peri_mr, ofs,
                    sysbus_mmio_get_region(SYS_BUS_DEVICE(uds), 0), -1000);
}

static void bcm2835_peripherals_init(Object *obj)
{
    BCM2835PeripheralState *s = BCM2835_PERIPHERALS(obj);

    /* Memory region for peripheral devices, which we export to our parent */
    memory_region_init(&s->peri_mr, obj,"bcm2835-peripherals", 0x1000000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->peri_mr);

    /* Internal memory region for peripheral bus addresses (not exported) */
    memory_region_init(&s->gpu_bus_mr, obj, "bcm2835-gpu", (uint64_t)1 << 32);

    /* Internal memory region for request/response communication with
     * mailbox-addressable peripherals (not exported)
     */
    memory_region_init(&s->mbox_mr, obj, "bcm2835-mbox",
                       MBOX_CHAN_COUNT << MBOX_AS_CHAN_SHIFT);

    /* Interrupt Controller */
    object_initialize_child(obj, "ic", &s->ic, TYPE_BCM2835_IC);

    /* SYS Timer */
    object_initialize_child(obj, "systimer", &s->systmr,
                            TYPE_BCM2835_SYSTIMER);

    /* UART0 */
    object_initialize_child(obj, "uart0", &s->uart0, TYPE_PL011);

    /* AUX / UART1 */
    object_initialize_child(obj, "aux", &s->aux, TYPE_BCM2835_AUX);

    /* Mailboxes */
    object_initialize_child(obj, "mbox", &s->mboxes, TYPE_BCM2835_MBOX);

    object_property_add_const_link(OBJECT(&s->mboxes), "mbox-mr",
                                   OBJECT(&s->mbox_mr));

    /* Framebuffer */
    object_initialize_child(obj, "fb", &s->fb, TYPE_BCM2835_FB);
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->fb), "vcram-size");

    object_property_add_const_link(OBJECT(&s->fb), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr));

    /* Property channel */
    object_initialize_child(obj, "property", &s->property,
                            TYPE_BCM2835_PROPERTY);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->property),
                              "board-rev");

    object_property_add_const_link(OBJECT(&s->property), "fb",
                                   OBJECT(&s->fb));
    object_property_add_const_link(OBJECT(&s->property), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr));

    /* Random Number Generator */
    object_initialize_child(obj, "rng", &s->rng, TYPE_BCM2835_RNG);

    /* Extended Mass Media Controller */
    object_initialize_child(obj, "sdhci", &s->sdhci, TYPE_SYSBUS_SDHCI);

    /* SDHOST */
    object_initialize_child(obj, "sdhost", &s->sdhost, TYPE_BCM2835_SDHOST);

    /* DMA Channels */
    object_initialize_child(obj, "dma", &s->dma, TYPE_BCM2835_DMA);

    object_initialize_child(obj, "orgated-dma-irq",
                            &s->orgated_dma_irq, TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->orgated_dma_irq), "num-lines",
                            ORGATED_DMA_IRQ_COUNT, &error_abort);

    object_property_add_const_link(OBJECT(&s->dma), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr));

    /* Thermal */
    object_initialize_child(obj, "thermal", &s->thermal, TYPE_BCM2835_THERMAL);

    /* GPIO */
    object_initialize_child(obj, "gpio", &s->gpio, TYPE_BCM2835_GPIO);

    object_property_add_const_link(OBJECT(&s->gpio), "sdbus-sdhci",
                                   OBJECT(&s->sdhci.sdbus));
    object_property_add_const_link(OBJECT(&s->gpio), "sdbus-sdhost",
                                   OBJECT(&s->sdhost.sdbus));

    /* Mphi */
    object_initialize_child(obj, "mphi", &s->mphi, TYPE_BCM2835_MPHI);

    /* DWC2 */
    object_initialize_child(obj, "dwc2", &s->dwc2, TYPE_DWC2_USB);

    /* CPRMAN clock manager */
    object_initialize_child(obj, "cprman", &s->cprman, TYPE_BCM2835_CPRMAN);

    object_property_add_const_link(OBJECT(&s->dwc2), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr));

    /* Power Management */
    object_initialize_child(obj, "powermgt", &s->powermgt,
                            TYPE_BCM2835_POWERMGT);
}

static void bcm2835_peripherals_realize(DeviceState *dev, Error **errp)
{
    BCM2835PeripheralState *s = BCM2835_PERIPHERALS(dev);
    Object *obj;
    MemoryRegion *ram;
    Error *err = NULL;
    uint64_t ram_size, vcram_size;
    int n;

    obj = object_property_get_link(OBJECT(dev), "ram", &error_abort);

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
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ic), errp)) {
        return;
    }

    /* CPRMAN clock manager */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->cprman), errp)) {
        return;
    }
    memory_region_add_subregion(&s->peri_mr, CPRMAN_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cprman), 0));
    qdev_connect_clock_in(DEVICE(&s->uart0), "clk",
                          qdev_get_clock_out(DEVICE(&s->cprman), "uart-out"));

    memory_region_add_subregion(&s->peri_mr, ARMCTRL_IC_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ic), 0));
    sysbus_pass_irq(SYS_BUS_DEVICE(s), SYS_BUS_DEVICE(&s->ic));

    /* Sys Timer */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->systmr), errp)) {
        return;
    }
    memory_region_add_subregion(&s->peri_mr, ST_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->systmr), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->systmr), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_TIMER0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->systmr), 1,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_TIMER1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->systmr), 2,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_TIMER2));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->systmr), 3,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_TIMER3));

    /* UART0 */
    qdev_prop_set_chr(DEVICE(&s->uart0), "chardev", serial_hd(0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart0), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, UART0_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart0), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart0), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_UART0));

    /* AUX / UART1 */
    qdev_prop_set_chr(DEVICE(&s->aux), "chardev", serial_hd(1));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->aux), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, AUX_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->aux), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->aux), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_AUX));

    /* Mailboxes */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mboxes), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, ARMCTRL_0_SBM_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mboxes), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mboxes), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_ARM_IRQ,
                               INTERRUPT_ARM_MAILBOX));

    /* Framebuffer */
    vcram_size = object_property_get_uint(OBJECT(s), "vcram-size", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (!object_property_set_uint(OBJECT(&s->fb), "vcram-base",
                                  ram_size - vcram_size, errp)) {
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fb), errp)) {
        return;
    }

    memory_region_add_subregion(&s->mbox_mr, MBOX_CHAN_FB << MBOX_AS_CHAN_SHIFT,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->fb), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fb), 0,
                       qdev_get_gpio_in(DEVICE(&s->mboxes), MBOX_CHAN_FB));

    /* Property channel */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->property), errp)) {
        return;
    }

    memory_region_add_subregion(&s->mbox_mr,
                MBOX_CHAN_PROPERTY << MBOX_AS_CHAN_SHIFT,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->property), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->property), 0,
                      qdev_get_gpio_in(DEVICE(&s->mboxes), MBOX_CHAN_PROPERTY));

    /* Random Number Generator */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rng), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, RNG_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rng), 0));

    /* Extended Mass Media Controller
     *
     * Compatible with:
     * - SD Host Controller Specification Version 3.0 Draft 1.0
     * - SDIO Specification Version 3.0
     * - MMC Specification Version 4.4
     *
     * For the exact details please refer to the Arasan documentation:
     *   SD3.0_Host_AHB_eMMC4.4_Usersguide_ver5.9_jan11_10.pdf
     */
    object_property_set_uint(OBJECT(&s->sdhci), "sd-spec-version", 3,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->sdhci), "capareg",
                             BCM2835_SDHC_CAPAREG, &error_abort);
    object_property_set_bool(OBJECT(&s->sdhci), "pending-insert-quirk", true,
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdhci), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, EMMC1_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sdhci), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_ARASANSDIO));

    /* SDHOST */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdhost), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, MMCI0_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sdhost), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhost), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_SDIO));

    /* DMA Channels */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dma), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, DMA_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dma), 0));
    memory_region_add_subregion(&s->peri_mr, DMA15_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dma), 1));

    for (n = 0; n <= SEPARATE_DMA_IRQ_MAX; n++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->dma), n,
                           qdev_get_gpio_in_named(DEVICE(&s->ic),
                                                  BCM2835_IC_GPU_IRQ,
                                                  INTERRUPT_DMA0 + n));
    }
    if (!qdev_realize(DEVICE(&s->orgated_dma_irq), NULL, errp)) {
        return;
    }
    for (n = 0; n < ORGATED_DMA_IRQ_COUNT; n++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->dma),
                           SEPARATE_DMA_IRQ_MAX + 1 + n,
                           qdev_get_gpio_in(DEVICE(&s->orgated_dma_irq), n));
    }
    qdev_connect_gpio_out(DEVICE(&s->orgated_dma_irq), 0,
                          qdev_get_gpio_in_named(DEVICE(&s->ic),
                              BCM2835_IC_GPU_IRQ,
                              INTERRUPT_DMA0 + SEPARATE_DMA_IRQ_MAX + 1));

    /* THERMAL */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->thermal), errp)) {
        return;
    }
    memory_region_add_subregion(&s->peri_mr, THERMAL_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->thermal), 0));

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, GPIO_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0));

    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->gpio), "sd-bus");

    /* Mphi */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mphi), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, MPHI_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mphi), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mphi), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_HOSTPORT));

    /* DWC2 */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dwc2), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, USB_OTG_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dwc2), 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dwc2), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_USB));

    /* Power Management */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->powermgt), errp)) {
        return;
    }

    memory_region_add_subregion(&s->peri_mr, PM_OFFSET,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->powermgt), 0));

    create_unimp(s, &s->txp, "bcm2835-txp", TXP_OFFSET, 0x1000);
    create_unimp(s, &s->armtmr, "bcm2835-sp804", ARMCTRL_TIMER0_1_OFFSET, 0x40);
    create_unimp(s, &s->i2s, "bcm2835-i2s", I2S_OFFSET, 0x100);
    create_unimp(s, &s->smi, "bcm2835-smi", SMI_OFFSET, 0x100);
    create_unimp(s, &s->spi[0], "bcm2835-spi0", SPI0_OFFSET, 0x20);
    create_unimp(s, &s->bscsl, "bcm2835-spis", BSC_SL_OFFSET, 0x100);
    create_unimp(s, &s->i2c[0], "bcm2835-i2c0", BSC0_OFFSET, 0x20);
    create_unimp(s, &s->i2c[1], "bcm2835-i2c1", BSC1_OFFSET, 0x20);
    create_unimp(s, &s->i2c[2], "bcm2835-i2c2", BSC2_OFFSET, 0x20);
    create_unimp(s, &s->otp, "bcm2835-otp", OTP_OFFSET, 0x80);
    create_unimp(s, &s->dbus, "bcm2835-dbus", DBUS_OFFSET, 0x8000);
    create_unimp(s, &s->ave0, "bcm2835-ave0", AVE0_OFFSET, 0x8000);
    create_unimp(s, &s->v3d, "bcm2835-v3d", V3D_OFFSET, 0x1000);
    create_unimp(s, &s->sdramc, "bcm2835-sdramc", SDRAMC_OFFSET, 0x100);
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
