/*
 * Allwinner A10 SoC emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qemu/datadir.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend-io.h"
#include "sysemu/dma.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "hw/misc/unimp.h"
#include "hw/arm/allwinner-f1.h"

/* List of unimplemented devices */
static struct AwF1Unimplemented {
    const char *name;
    hwaddr      base;
    hwaddr      size;
} unimplemented[] = {
    { "dramc",        0x01c01000, 4 * KiB },
    { "dma",          0x01c02000, 4 * KiB },
    { "spi0",         0x01c05000, 4 * KiB },
    { "spi1",         0x01c06000, 4 * KiB },
    { "tve",          0x01c0a000, 4 * KiB },
    { "tvd",          0x01c0b000, 4 * KiB },
    { "tcon",         0x01c0c000, 4 * KiB },
    { "ve",           0x01c0e000, 4 * KiB },
    { "mmc1",         0x01c10000, 4 * KiB },
    { "usb-otg",      0x01c13000, 4 * KiB },
    //{ "ccu",          0x01c20000, 1 * KiB },
    //{ "pio",          0x01c20800, 1 * KiB },
    { "owa",          0x01c21000, 1 * KiB },
    { "pwm",          0x01c21400, 1 * KiB },
    { "daudio",       0x01c22000, 1 * KiB },
    { "cir",          0x01c22c00, 1 * KiB },
    //{ "keyadc",       0x01c23400, 1 * KiB },
    { "aud-codec",    0x01c23c00, 1 * KiB },
    { "tp",           0x01c24800, 1 * KiB },
    { "uart0",        0x01c25000, 1 * KiB },
    { "uart1",        0x01c25400, 1 * KiB },
    { "uart2",        0x01c25800, 1 * KiB },
    { "twi0",         0x01c27000, 1 * KiB },
    { "twi1",         0x01c27400, 1 * KiB },
    { "twi2",         0x01c27800, 1 * KiB },
    { "csi",          0x01cb0000, 4 * KiB },
    { "defe",         0x01e00000, 128 * KiB },
    //{ "debe",      0x01e60000, 64 * KiB },
    { "de-interlace", 0x01e70000, 64 * KiB },
    //{ "ddr-mem",     0x80000000, 1 * GiB },
    //{ "s-brom",      0xffff0000, 64 * KiB }
};

void aw_f1_bootrom_setup(Object *obj)
{
    AwF1State *s = AW_F1(obj);
    char *filename;
    
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "aw-f1.brom");    

    if (filename == NULL) {
        error_setg(&error_fatal, "%s: failed to read GROM data",
                   __func__);
        return;        
    } else {
        g_autofree uint8_t *buffer = g_new0(uint8_t, AW_F1_BROM_SIZE);
        
        FILE *fd = fopen(filename, "rb");
        
        (void)!fread(buffer, 1, AW_F1_BROM_SIZE, fd);
        fclose(fd);
        
        rom_add_blob_fixed("allwinner-f1.bootrom", buffer, AW_F1_BROM_SIZE, AW_F1_BROM_ADDR);

        s->intc.reset_addr = AW_F1_BROM_ADDR;
    }
}

void aw_f1_spl_setup(Object *obj, BlockBackend *blk)
{
    AwF1State *s = AW_F1(obj);
    const int64_t sram_size = 32 * KiB;

    g_autofree uint8_t *buffer = g_new0(uint8_t, sram_size);

    if (blk_pread(blk, 8 * KiB, buffer, sram_size) < 0) {
        error_setg(&error_fatal, "%s: failed to read SPL data",
                   __func__);
        return;
    }
    
    rom_add_blob_fixed("allwinner-f1.spl", buffer, sram_size, AW_F1_SRAM_ADDR);
    
    s->intc.reset_addr = AW_F1_SRAM_ADDR;
}

static void aw_f1_init(Object *obj)
{
    AwF1State *s = AW_F1(obj);

    object_initialize_child(obj, "cpu",   &s->cpu,
                            ARM_CPU_TYPE_NAME("arm926"));
#if 0
    object_initialize_child(obj, "sysctl", &s->sysctrl, TYPE_AW_F1_SYSCTRL);
    object_initialize_child(obj, "drams",  &s->dramctl, TYPE_AW_F1_DRAMC);
#endif
    object_initialize_child(obj, "intc",  &s->intc,  TYPE_AW_F1_PIC);
    object_initialize_child(obj, "pio",   &s->pio,   TYPE_AW_F1_PIO);
    object_initialize_child(obj, "timer", &s->timer, TYPE_AW_F1_PIT);
    object_initialize_child(obj, "mmc0",  &s->mmc0,  TYPE_AW_SDHOST_SUN4I);
    object_initialize_child(obj, "ccu",   &s->ccu,   TYPE_AW_F1_CCU);
    object_initialize_child(obj, "keyadc", &s->keyadc,  TYPE_AW_KEYADC);
    object_initialize_child(obj, "debe",  &s->debe,  TYPE_AW_F1_DEBE);
}

static void aw_f1_realize(DeviceState *dev, Error **errp)
{
    AwF1State *s = AW_F1(dev);
    SysBusDevice *sysbusdev;
    unsigned i;

    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }
    // USB Controller off. USB SRAM goto common SRAM?
    memory_region_init_ram(&s->sram, OBJECT(dev), "sram", (40 + 4) * KiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), AW_F1_SRAM_ADDR, &s->sram);

    //memory_region_init_ram(&s->dram, OBJECT(dev), "dram", 32 * MiB, &error_fatal);
    //memory_region_add_subregion(get_system_memory(), AW_F1_DRAM_ADDR, &s->dram);

    /* System Control */
    memory_region_init_ram(&s->sysctl, OBJECT(dev), "sysctl", 0x30, &error_fatal);
    memory_region_add_subregion(get_system_memory(),  AW_F1_SYSCTRL_REGS, &s->sysctl);
#if 0
    sysbusdev = SYS_BUS_DEVICE(&s->sysctrl);
    sysbus_realize(sysbusdev, &error_fatal);
    sysbus_mmio_map(sysbusdev, 0, s->memmap[AW_F1_DEV_SYSCTRL_REGS]);
    /* DRAMC */
    sysbusdev = SYS_BUS_DEVICE(&s->dramc);
    sysbus_realize(sysbusdev, &error_fatal);
    sysbus_mmio_map(sysbusdev, 0, s->memmap[AW_F1_DEV_DRAMC_REGS]);
#endif
    /* PIC */
    sysbusdev = SYS_BUS_DEVICE(&s->intc);
    if (!sysbus_realize(sysbusdev, errp)) {
        return;
    }
    sysbus_mmio_map(sysbusdev, 0, AW_F1_PIC_REGS);
    sysbus_connect_irq(sysbusdev, 0, qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(sysbusdev, 1, qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));
    qdev_pass_gpios(DEVICE(&s->intc), dev, NULL);
    /* GPIO Unit */
    sysbusdev = SYS_BUS_DEVICE(&s->pio);
    sysbus_realize(sysbusdev, &error_fatal);
    sysbus_mmio_map(sysbusdev, 0, AW_F1_PIO_REGS);
    /* TIMER */
    sysbusdev = SYS_BUS_DEVICE(&s->timer);
    if (!sysbus_realize(sysbusdev, errp)) {
        return;
    }
    sysbus_mmio_map(sysbusdev, 0, AW_F1_PIT_REGS);
    sysbus_connect_irq(sysbusdev, 0, qdev_get_gpio_in(dev, 13));
    sysbus_connect_irq(sysbusdev, 1, qdev_get_gpio_in(dev, 14));
    sysbus_connect_irq(sysbusdev, 2, qdev_get_gpio_in(dev, 15));
    // TODO: Watchdog
    //sysbus_connect_irq(sysbusdev, 3, qdev_get_gpio_in(dev, 16));
    /* SD/MMC */
    sysbusdev = SYS_BUS_DEVICE(&s->mmc0);
    object_property_set_link(OBJECT(&s->mmc0), "dma-memory",
                             OBJECT(get_system_memory()), &error_fatal);
    sysbus_realize(sysbusdev, &error_fatal);
    sysbus_mmio_map(sysbusdev, 0, AW_F1_MMC0_REGS);
    sysbus_connect_irq(sysbusdev, 0, qdev_get_gpio_in(dev, 23));
    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->mmc0), "sd-bus");
    /* Clock Control Unit */
    sysbusdev = SYS_BUS_DEVICE(&s->ccu);
    sysbus_realize(sysbusdev, &error_fatal);
    sysbus_mmio_map(sysbusdev, 0, AW_F1_CCU_REGS);
    /* KEYADC */
    sysbusdev = SYS_BUS_DEVICE(&s->keyadc);
    sysbus_realize(sysbusdev, &error_fatal);
    sysbus_mmio_map(sysbusdev, 0, AW_F1_KEYADC_REGS);
    /* Display Engine Back-End Unit */
    sysbusdev = SYS_BUS_DEVICE(&s->debe);
    sysbus_realize(sysbusdev, &error_fatal);
    sysbus_mmio_map(sysbusdev, 0, AW_F1_DEBE_REGS);
    /* TODO: SPI */
    /* FIXME use a qdev chardev prop instead of serial_hd() */
    serial_mm_init(get_system_memory(), AW_F1_UART0_REGS, 2,
                   qdev_get_gpio_in(dev, 1),
                   115200, serial_hd(0), DEVICE_NATIVE_ENDIAN);

    for (i = 0; i < ARRAY_SIZE(unimplemented); i++) {
        create_unimplemented_device(unimplemented[i].name, unimplemented[i].base, unimplemented[i].size);        
    }
}

static void aw_f1_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aw_f1_realize;
    dc->user_creatable = false;
}

static const TypeInfo aw_f1_info = {
    .name          = TYPE_AW_F1,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(AwF1State),
    .instance_init = aw_f1_init,
    .class_init    = aw_f1_class_init,
};

static void aw_f1_register_types(void)
{
    type_register_static(&aw_f1_info);
    // type_register_static(&aw_f1200s_info);
}

type_init(aw_f1_register_types)
