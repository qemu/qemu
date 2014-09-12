/*
 * Generic PKUnity SoC machine and board descriptor
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */

#include "qemu-common.h"
#include "ui/console.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/i386/pc.h"
#include "sysemu/qtest.h"

#undef DEBUG_PUV3
#include "hw/unicore32/puv3.h"

#define KERNEL_LOAD_ADDR        0x03000000
#define KERNEL_MAX_SIZE         0x00800000 /* Just a guess */

static void puv3_intc_cpu_handler(void *opaque, int irq, int level)
{
    UniCore32CPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    assert(irq == 0);
    if (level) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void puv3_soc_init(CPUUniCore32State *env)
{
    qemu_irq *cpu_intc, irqs[PUV3_IRQS_NR];
    DeviceState *dev;
    MemoryRegion *i8042 = g_new(MemoryRegion, 1);
    int i;

    /* Initialize interrupt controller */
    cpu_intc = qemu_allocate_irqs(puv3_intc_cpu_handler,
                                  uc32_env_get_cpu(env), 1);
    dev = sysbus_create_simple("puv3_intc", PUV3_INTC_BASE, *cpu_intc);
    for (i = 0; i < PUV3_IRQS_NR; i++) {
        irqs[i] = qdev_get_gpio_in(dev, i);
    }

    /* Initialize minimal necessary devices for kernel booting */
    sysbus_create_simple("puv3_pm", PUV3_PM_BASE, NULL);
    sysbus_create_simple("puv3_dma", PUV3_DMA_BASE, NULL);
    sysbus_create_simple("puv3_ost", PUV3_OST_BASE, irqs[PUV3_IRQS_OST0]);
    sysbus_create_varargs("puv3_gpio", PUV3_GPIO_BASE,
            irqs[PUV3_IRQS_GPIOLOW0], irqs[PUV3_IRQS_GPIOLOW1],
            irqs[PUV3_IRQS_GPIOLOW2], irqs[PUV3_IRQS_GPIOLOW3],
            irqs[PUV3_IRQS_GPIOLOW4], irqs[PUV3_IRQS_GPIOLOW5],
            irqs[PUV3_IRQS_GPIOLOW6], irqs[PUV3_IRQS_GPIOLOW7],
            irqs[PUV3_IRQS_GPIOHIGH], NULL);

    /* Keyboard (i8042), mouse disabled for nographic */
    i8042_mm_init(irqs[PUV3_IRQS_PS2_KBD], NULL, i8042, PUV3_REGS_OFFSET, 4);
    memory_region_add_subregion(get_system_memory(), PUV3_PS2_BASE, i8042);
}

static void puv3_board_init(CPUUniCore32State *env, ram_addr_t ram_size)
{
    MemoryRegion *ram_memory = g_new(MemoryRegion, 1);

    /* SDRAM at address zero.  */
    memory_region_init_ram(ram_memory, NULL, "puv3.ram", ram_size,
                           &error_abort);
    vmstate_register_ram_global(ram_memory);
    memory_region_add_subregion(get_system_memory(), 0, ram_memory);
}

static const GraphicHwOps no_ops;

static void puv3_load_kernel(const char *kernel_filename)
{
    int size;

    if (kernel_filename == NULL && qtest_enabled()) {
        return;
    }
    assert(kernel_filename != NULL);

    /* only zImage format supported */
    size = load_image_targphys(kernel_filename, KERNEL_LOAD_ADDR,
            KERNEL_MAX_SIZE);
    if (size < 0) {
        hw_error("Load kernel error: '%s'\n", kernel_filename);
    }

    /* cheat curses that we have a graphic console, only under ocd console */
    graphic_console_init(NULL, 0, &no_ops, NULL);
}

static void puv3_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    CPUUniCore32State *env;

    if (initrd_filename) {
        hw_error("Please use kernel built-in initramdisk.\n");
    }

    if (!cpu_model) {
        cpu_model = "UniCore-II";
    }

    env = cpu_init(cpu_model);
    if (!env) {
        hw_error("Unable to find CPU definition\n");
    }

    puv3_soc_init(env);
    puv3_board_init(env, ram_size);
    puv3_load_kernel(kernel_filename);
}

static QEMUMachine puv3_machine = {
    .name = "puv3",
    .desc = "PKUnity Version-3 based on UniCore32",
    .init = puv3_init,
    .is_default = 1,
};

static void puv3_machine_init(void)
{
    qemu_register_machine(&puv3_machine);
}

machine_init(puv3_machine_init)
