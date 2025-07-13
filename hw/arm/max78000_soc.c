/*
 * MAX78000 SOC
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implementation based on stm32f205 and Max78000 user guide at
 * https://www.analog.com/media/en/technical-documentation/user-guides/max78000-user-guide.pdf
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/arm/max78000_soc.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"

static const uint32_t max78000_icc_addr[] = {0x4002a000, 0x4002a800};
static const uint32_t max78000_uart_addr[] = {0x40042000, 0x40043000,
                                              0x40044000};

static const int max78000_uart_irq[] = {14, 15, 34};

static void max78000_soc_initfn(Object *obj)
{
    MAX78000State *s = MAX78000_SOC(obj);
    int i;

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    object_initialize_child(obj, "gcr", &s->gcr, TYPE_MAX78000_GCR);

    for (i = 0; i < MAX78000_NUM_ICC; i++) {
        g_autofree char *name = g_strdup_printf("icc%d", i);
        object_initialize_child(obj, name, &s->icc[i], TYPE_MAX78000_ICC);
    }

    for (i = 0; i < MAX78000_NUM_UART; i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i);
        object_initialize_child(obj, name, &s->uart[i],
                                TYPE_MAX78000_UART);
    }

    object_initialize_child(obj, "trng", &s->trng, TYPE_MAX78000_TRNG);

    object_initialize_child(obj, "aes", &s->aes, TYPE_MAX78000_AES);

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
}

static void max78000_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MAX78000State *s = MAX78000_SOC(dev_soc);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *dev, *gcrdev, *armv7m;
    SysBusDevice *busdev;
    Error *err = NULL;
    int i;

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    memory_region_init_rom(&s->flash, OBJECT(dev_soc), "MAX78000.flash",
                           FLASH_SIZE, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, &s->flash);

    memory_region_init_ram(&s->sram, NULL, "MAX78000.sram", SRAM_SIZE,
                           &err);

    gcrdev = DEVICE(&s->gcr);
    object_property_set_link(OBJECT(gcrdev), "sram", OBJECT(&s->sram),
                                 &err);

    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, &s->sram);

    armv7m = DEVICE(&s->armv7m);

    /*
     * The MAX78000 user guide's Interrupt Vector Table section
     * suggests that there are 120 IRQs in the text, while only listing
     * 104 in table 5-1. Implement the more generous of the two.
     * This has not been tested in hardware.
     */
    qdev_prop_set_uint32(armv7m, "num-irq", 120);
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 3);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    for (i = 0; i < MAX78000_NUM_ICC; i++) {
        dev = DEVICE(&(s->icc[i]));
        sysbus_realize(SYS_BUS_DEVICE(dev), errp);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, max78000_icc_addr[i]);
    }

    for (i = 0; i < MAX78000_NUM_UART; i++) {
        g_autofree char *link = g_strdup_printf("uart%d", i);
        dev = DEVICE(&(s->uart[i]));
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }

        object_property_set_link(OBJECT(gcrdev), link, OBJECT(dev),
                                 &err);

        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, max78000_uart_addr[i]);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m,
                                                       max78000_uart_irq[i]));
    }

    dev = DEVICE(&s->trng);
    sysbus_realize(SYS_BUS_DEVICE(dev), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x4004d000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(armv7m, 4));

    object_property_set_link(OBJECT(gcrdev), "trng", OBJECT(dev), &err);

    dev = DEVICE(&s->aes);
    sysbus_realize(SYS_BUS_DEVICE(dev), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x40007400);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(armv7m, 5));

    object_property_set_link(OBJECT(gcrdev), "aes", OBJECT(dev), &err);

    dev = DEVICE(&s->gcr);
    sysbus_realize(SYS_BUS_DEVICE(dev), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x40000000);

    create_unimplemented_device("systemInterface",      0x40000400, 0x400);
    create_unimplemented_device("functionControl",      0x40000800, 0x400);
    create_unimplemented_device("watchdogTimer0",       0x40003000, 0x400);
    create_unimplemented_device("dynamicVoltScale",     0x40003c00, 0x40);
    create_unimplemented_device("SIMO",                 0x40004400, 0x400);
    create_unimplemented_device("trimSystemInit",       0x40005400, 0x400);
    create_unimplemented_device("generalCtrlFunc",      0x40005800, 0x400);
    create_unimplemented_device("wakeupTimer",          0x40006400, 0x400);
    create_unimplemented_device("powerSequencer",       0x40006800, 0x400);
    create_unimplemented_device("miscControl",          0x40006c00, 0x400);

    create_unimplemented_device("gpio0",                0x40008000, 0x1000);
    create_unimplemented_device("gpio1",                0x40009000, 0x1000);

    create_unimplemented_device("parallelCamInterface", 0x4000e000, 0x1000);
    create_unimplemented_device("CRC",                  0x4000f000, 0x1000);

    create_unimplemented_device("timer0",               0x40010000, 0x1000);
    create_unimplemented_device("timer1",               0x40011000, 0x1000);
    create_unimplemented_device("timer2",               0x40012000, 0x1000);
    create_unimplemented_device("timer3",               0x40013000, 0x1000);

    create_unimplemented_device("i2c0",                 0x4001d000, 0x1000);
    create_unimplemented_device("i2c1",                 0x4001e000, 0x1000);
    create_unimplemented_device("i2c2",                 0x4001f000, 0x1000);

    create_unimplemented_device("standardDMA",          0x40028000, 0x1000);
    create_unimplemented_device("flashController0",     0x40029000, 0x400);

    create_unimplemented_device("adc",                  0x40034000, 0x1000);
    create_unimplemented_device("pulseTrainEngine",     0x4003c000, 0xa0);
    create_unimplemented_device("oneWireMaster",        0x4003d000, 0x1000);
    create_unimplemented_device("semaphore",            0x4003e000, 0x1000);

    create_unimplemented_device("spi1",                 0x40046000, 0x2000);
    create_unimplemented_device("i2s",                  0x40060000, 0x1000);
    create_unimplemented_device("lowPowerControl",      0x40080000, 0x400);
    create_unimplemented_device("gpio2",                0x40080400, 0x200);
    create_unimplemented_device("lowPowerWatchdogTimer",    0x40080800, 0x400);
    create_unimplemented_device("lowPowerTimer4",       0x40080c00, 0x400);

    create_unimplemented_device("lowPowerTimer5",       0x40081000, 0x400);
    create_unimplemented_device("lowPowerUART0",        0x40081400, 0x400);
    create_unimplemented_device("lowPowerComparator",   0x40088000, 0x400);

    create_unimplemented_device("spi0",                 0x400be000, 0x400);

    /*
     * The MAX78000 user guide's base address map lists the CNN TX FIFO as
     * beginning at 0x400c0400 and ending at 0x400c0400. Given that CNN_FIFO
     * is listed as having data accessible up to offset 0x1000, the user
     * guide is likely incorrect.
     */
    create_unimplemented_device("cnnTxFIFO",            0x400c0400, 0x2000);

    create_unimplemented_device("cnnGlobalControl",     0x50000000, 0x10000);
    create_unimplemented_device("cnnx16quad0",          0x50100000, 0x40000);
    create_unimplemented_device("cnnx16quad1",          0x50500000, 0x40000);
    create_unimplemented_device("cnnx16quad2",          0x50900000, 0x40000);
    create_unimplemented_device("cnnx16quad3",          0x50d00000, 0x40000);

}

static void max78000_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = max78000_soc_realize;
}

static const TypeInfo max78000_soc_info = {
    .name          = TYPE_MAX78000_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MAX78000State),
    .instance_init = max78000_soc_initfn,
    .class_init    = max78000_soc_class_init,
};

static void max78000_soc_types(void)
{
    type_register_static(&max78000_soc_info);
}

type_init(max78000_soc_types)
