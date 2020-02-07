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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "ui/console.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/qtest.h"

#undef DEBUG_PUV3
#include "hw/unicore32/puv3.h"
#include "hw/input/i8042.h"
#include "hw/irq.h"

#define KERNEL_LOAD_ADDR        0x03000000
#define KERNEL_MAX_SIZE         0x00800000 /* Just a guess */

/* PKUnity System bus (AHB): 0xc0000000 - 0xedffffff (640MB) */
#define PUV3_DMA_BASE           (0xc0200000) /* AHB-4 */

/* PKUnity Peripheral bus (APB): 0xee000000 - 0xefffffff (128MB) */
#define PUV3_GPIO_BASE          (0xee500000) /* APB-5 */
#define PUV3_INTC_BASE          (0xee600000) /* APB-6 */
#define PUV3_OST_BASE           (0xee800000) /* APB-8 */
#define PUV3_PM_BASE            (0xeea00000) /* APB-10 */
#define PUV3_PS2_BASE           (0xeeb00000) /* APB-11 */

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
    qemu_irq cpu_intc, irqs[PUV3_IRQS_NR];
    DeviceState *dev;
    MemoryRegion *i8042 = g_new(MemoryRegion, 1);
    int i;

    /* Initialize interrupt controller */
    cpu_intc = qemu_allocate_irq(puv3_intc_cpu_handler,
                                 env_archcpu(env), 0);
    dev = sysbus_create_simple("puv3_intc", PUV3_INTC_BASE, cpu_intc);
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
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0, ram_memory);
}

static const GraphicHwOps no_ops;

static void puv3_load_kernel(const char *kernel_filename)
{
    int size;

    if (kernel_filename == NULL && qtest_enabled()) {
        return;
    }
    if (kernel_filename == NULL) {
        error_report("kernel parameter cannot be empty");
        exit(1);
    }

    /* only zImage format supported */
    size = load_image_targphys(kernel_filename, KERNEL_LOAD_ADDR,
            KERNEL_MAX_SIZE);
    if (size < 0) {
        error_report("Load kernel error: '%s'", kernel_filename);
        exit(1);
    }

    /* cheat curses that we have a graphic console, only under ocd console */
    graphic_console_init(NULL, 0, &no_ops, NULL);
}

static void puv3_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    CPUUniCore32State *env;
    UniCore32CPU *cpu;

    if (initrd_filename) {
        error_report("Please use kernel built-in initramdisk");
        exit(1);
    }

    cpu = UNICORE32_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    puv3_soc_init(env);
    puv3_board_init(env, ram_size);
    puv3_load_kernel(kernel_filename);
}

static void puv3_machine_init(MachineClass *mc)
{
    mc->desc = "PKUnity Version-3 based on UniCore32";
    mc->init = puv3_init;
    mc->is_default = true;
    mc->default_cpu_type = UNICORE32_CPU_TYPE_NAME("UniCore-II");
}

DEFINE_MACHINE("puv3", puv3_machine_init)
