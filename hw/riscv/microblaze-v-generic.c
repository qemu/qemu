/*
 * QEMU model of Microblaze V generic board.
 *
 * based on hw/microblaze/petalogix_ml605_mmu.c
 *
 * Copyright (c) 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2011 PetaLogix
 * Copyright (c) 2009 Edgar E. Iglesias.
 * Copyright (C) 2024, Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by Sai Pavan Boddu <sai.pavan.boddu@amd.com
 *     and by Michal Simek <michal.simek@amd.com>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "system/system.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "system/address-spaces.h"
#include "hw/char/xilinx_uartlite.h"
#include "hw/misc/unimp.h"

#define LMB_BRAM_SIZE (128 * KiB)
#define MEMORY_BASEADDR 0x80000000
#define INTC_BASEADDR 0x41200000
#define TIMER_BASEADDR 0x41c00000
#define TIMER_BASEADDR2 0x41c10000
#define UARTLITE_BASEADDR 0x40600000
#define ETHLITE_BASEADDR 0x40e00000
#define UART16550_BASEADDR 0x44a10000
#define AXIENET_BASEADDR 0x40c00000
#define AXIDMA_BASEADDR 0x41e00000
#define GPIO_BASEADDR 0x40000000
#define GPIO_BASEADDR2 0x40010000
#define GPIO_BASEADDR3 0x40020000
#define I2C_BASEADDR 0x40800000
#define QSPI_BASEADDR 0x44a00000

#define TIMER_IRQ           0
#define UARTLITE_IRQ        1
#define UART16550_IRQ       4
#define ETHLITE_IRQ         5
#define TIMER_IRQ2          6
#define AXIENET_IRQ         7
#define AXIDMA_IRQ1         8
#define AXIDMA_IRQ0         9

static void mb_v_generic_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    DeviceState *dev, *dma, *eth0;
    Object *ds, *cs;
    int i;
    RISCVCPU *cpu;
    hwaddr ddr_base = MEMORY_BASEADDR;
    MemoryRegion *phys_lmb_bram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32];
    MemoryRegion *sysmem = get_system_memory();

    cpu = RISCV_CPU(object_new(machine->cpu_type));
    object_property_set_bool(OBJECT(cpu), "h", false, NULL);
    object_property_set_bool(OBJECT(cpu), "d", false, NULL);
    qdev_realize(DEVICE(cpu), NULL, &error_abort);
    /* Attach emulated BRAM through the LMB.  */
    memory_region_init_ram(phys_lmb_bram, NULL,
                           "mb_v.lmb_bram", LMB_BRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0x00000000, phys_lmb_bram);

    memory_region_init_ram(phys_ram, NULL, "mb_v.ram",
                           ram_size, &error_fatal);
    memory_region_add_subregion(sysmem, ddr_base, phys_ram);

    dev = qdev_new("xlnx.xps-intc");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_uint32(dev, "kind-of-intr",
                         1 << UARTLITE_IRQ);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, INTC_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(DEVICE(cpu), 11));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    /* Uartlite */
    dev = qdev_new(TYPE_XILINX_UARTLITE);
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, UARTLITE_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[UARTLITE_IRQ]);

    /* Full uart */
    serial_mm_init(sysmem, UART16550_BASEADDR + 0x1000, 2,
                   irq[UART16550_IRQ], 115200, serial_hd(1),
                   DEVICE_LITTLE_ENDIAN);

    /* 2 timers at irq 0 @ 100 Mhz.  */
    dev = qdev_new("xlnx.xps-timer");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 100000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ]);

    /* 2 timers at irq 3 @ 100 Mhz.  */
    dev = qdev_new("xlnx.xps-timer");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 100000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR2);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ2]);

    /* Emaclite */
    dev = qdev_new("xlnx.xps-ethernetlite");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
    qemu_configure_nic_device(dev, true, NULL);
    qdev_prop_set_uint32(dev, "tx-ping-pong", 0);
    qdev_prop_set_uint32(dev, "rx-ping-pong", 0);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, ETHLITE_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[ETHLITE_IRQ]);

    /* axi ethernet and dma initialization. */
    eth0 = qdev_new("xlnx.axi-ethernet");
    dma = qdev_new("xlnx.axi-dma");

    /* FIXME: attach to the sysbus instead */
    object_property_add_child(qdev_get_machine(), "xilinx-eth", OBJECT(eth0));
    object_property_add_child(qdev_get_machine(), "xilinx-dma", OBJECT(dma));

    ds = object_property_get_link(OBJECT(dma),
                                  "axistream-connected-target", NULL);
    cs = object_property_get_link(OBJECT(dma),
                                  "axistream-control-connected-target", NULL);
    qemu_configure_nic_device(eth0, true, NULL);
    qdev_prop_set_uint32(eth0, "rxmem", 0x1000);
    qdev_prop_set_uint32(eth0, "txmem", 0x1000);
    object_property_set_link(OBJECT(eth0), "axistream-connected", ds,
                             &error_abort);
    object_property_set_link(OBJECT(eth0), "axistream-control-connected", cs,
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(eth0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(eth0), 0, AXIENET_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(eth0), 0, irq[AXIENET_IRQ]);

    ds = object_property_get_link(OBJECT(eth0),
                                  "axistream-connected-target", NULL);
    cs = object_property_get_link(OBJECT(eth0),
                                  "axistream-control-connected-target", NULL);
    qdev_prop_set_uint32(dma, "freqhz", 100000000);
    object_property_set_link(OBJECT(dma), "axistream-connected", ds,
                             &error_abort);
    object_property_set_link(OBJECT(dma), "axistream-control-connected", cs,
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dma), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dma), 0, AXIDMA_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dma), 0, irq[AXIDMA_IRQ0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(dma), 1, irq[AXIDMA_IRQ1]);

    /* unimplemented devices */
    create_unimplemented_device("gpio", GPIO_BASEADDR, 0x10000);
    create_unimplemented_device("gpio2", GPIO_BASEADDR2, 0x10000);
    create_unimplemented_device("gpio3", GPIO_BASEADDR3, 0x10000);
    create_unimplemented_device("i2c", I2C_BASEADDR, 0x10000);
    create_unimplemented_device("qspi", QSPI_BASEADDR, 0x10000);
}

static void mb_v_generic_machine_init(MachineClass *mc)
{
    mc->desc = "AMD Microblaze-V generic platform";
    mc->init = mb_v_generic_init;
    mc->min_cpus = 1;
    mc->max_cpus = 1;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->default_cpus = 1;
}

DEFINE_MACHINE("amd-microblaze-v-generic", mb_v_generic_machine_init)
