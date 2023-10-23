/*
 * Model of Petalogix linux reference design targeting Xilinx Spartan 3ADSP-1800
 * boards.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
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
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/misc/unimp.h"
#include "exec/address-spaces.h"
#include "hw/char/xilinx_uartlite.h"

#include "boot.h"

#define LMB_BRAM_SIZE  (128 * KiB)
#define FLASH_SIZE     (16 * MiB)

#define BINARY_DEVICE_TREE_FILE "petalogix-s3adsp1800.dtb"

#define MEMORY_BASEADDR 0x90000000
#define FLASH_BASEADDR 0xa0000000
#define GPIO_BASEADDR 0x81400000
#define INTC_BASEADDR 0x81800000
#define TIMER_BASEADDR 0x83c00000
#define UARTLITE_BASEADDR 0x84000000
#define ETHLITE_BASEADDR 0x81000000

#define TIMER_IRQ           0
#define ETHLITE_IRQ         1
#define UARTLITE_IRQ        3

static void
petalogix_s3adsp1800_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    DeviceState *dev;
    MicroBlazeCPU *cpu;
    DriveInfo *dinfo;
    int i;
    hwaddr ddr_base = MEMORY_BASEADDR;
    MemoryRegion *phys_lmb_bram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32];
    MemoryRegion *sysmem = get_system_memory();

    cpu = MICROBLAZE_CPU(object_new(TYPE_MICROBLAZE_CPU));
    object_property_set_str(OBJECT(cpu), "version", "7.10.d", &error_abort);
    qdev_realize(DEVICE(cpu), NULL, &error_abort);

    /* Attach emulated BRAM through the LMB.  */
    memory_region_init_ram(phys_lmb_bram, NULL,
                           "petalogix_s3adsp1800.lmb_bram", LMB_BRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0x00000000, phys_lmb_bram);

    memory_region_init_ram(phys_ram, NULL, "petalogix_s3adsp1800.ram",
                           ram_size, &error_fatal);
    memory_region_add_subregion(sysmem, ddr_base, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(FLASH_BASEADDR,
                          "petalogix_s3adsp1800.flash", FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          64 * KiB, 1, 0x89, 0x18, 0x0000, 0x0, 1);

    dev = qdev_new("xlnx.xps-intc");
    qdev_prop_set_uint32(dev, "kind-of-intr",
                         1 << ETHLITE_IRQ | 1 << UARTLITE_IRQ);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, INTC_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(DEVICE(cpu), MB_CPU_IRQ));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    dev = qdev_new(TYPE_XILINX_UARTLITE);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, UARTLITE_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[UARTLITE_IRQ]);

    /* 2 timers at irq 2 @ 62 Mhz.  */
    dev = qdev_new("xlnx.xps-timer");
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 62 * 1000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ]);

    dev = qdev_new("xlnx.xps-ethernetlite");
    qemu_configure_nic_device(dev, true, NULL);
    qdev_prop_set_uint32(dev, "tx-ping-pong", 0);
    qdev_prop_set_uint32(dev, "rx-ping-pong", 0);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, ETHLITE_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[ETHLITE_IRQ]);

    create_unimplemented_device("gpio", GPIO_BASEADDR, 0x10000);

    microblaze_load_kernel(cpu, ddr_base, ram_size,
                           machine->initrd_filename,
                           BINARY_DEVICE_TREE_FILE,
                           NULL);
}

static void petalogix_s3adsp1800_machine_init(MachineClass *mc)
{
    mc->desc = "PetaLogix linux refdesign for xilinx Spartan 3ADSP1800";
    mc->init = petalogix_s3adsp1800_init;
    mc->is_default = true;
}

DEFINE_MACHINE("petalogix-s3adsp1800", petalogix_s3adsp1800_machine_init)
