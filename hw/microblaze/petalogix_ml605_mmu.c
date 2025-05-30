/*
 * Model of Petalogix linux reference design targeting Xilinx Spartan ml605
 * board.
 *
 * Copyright (c) 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2011 PetaLogix
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
#include "system/system.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/qdev-properties.h"
#include "system/address-spaces.h"
#include "hw/ssi/ssi.h"

#include "boot.h"

#include "hw/stream.h"

#define LMB_BRAM_SIZE  (128 * KiB)
#define FLASH_SIZE     (32 * MiB)

#define BINARY_DEVICE_TREE_FILE "petalogix-ml605.dtb"

#define NUM_SPI_FLASHES 4

#define SPI_BASEADDR 0x40a00000
#define MEMORY_BASEADDR 0x50000000
#define FLASH_BASEADDR 0x86000000
#define INTC_BASEADDR 0x81800000
#define TIMER_BASEADDR 0x83c00000
#define UART16550_BASEADDR 0x83e00000
#define AXIENET_BASEADDR 0x82780000
#define AXIDMA_BASEADDR 0x84600000

#define AXIDMA_IRQ1         0
#define AXIDMA_IRQ0         1
#define TIMER_IRQ           2
#define AXIENET_IRQ         3
#define SPI_IRQ             4
#define UART16550_IRQ       5

static void
petalogix_ml605_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev, *dma, *eth0;
    Object *ds, *cs;
    MicroBlazeCPU *cpu;
    SysBusDevice *busdev;
    DriveInfo *dinfo;
    int i;
    MemoryRegion *phys_lmb_bram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32];

    /* init CPUs */
    cpu = MICROBLAZE_CPU(object_new(TYPE_MICROBLAZE_CPU));
    object_property_set_str(OBJECT(cpu), "version", "8.10.a", &error_abort);
    /* Use FPU but don't use floating point conversion and square
     * root instructions
     */
    object_property_set_int(OBJECT(cpu), "use-fpu", 1, &error_abort);
    object_property_set_bool(OBJECT(cpu), "dcache-writeback", true,
                             &error_abort);
    object_property_set_bool(OBJECT(cpu), "little-endian", true, &error_abort);
    qdev_realize(DEVICE(cpu), NULL, &error_abort);

    /* Attach emulated BRAM through the LMB.  */
    memory_region_init_ram(phys_lmb_bram, NULL, "petalogix_ml605.lmb_bram",
                           LMB_BRAM_SIZE, &error_fatal);
    memory_region_add_subregion(address_space_mem, 0x00000000, phys_lmb_bram);

    memory_region_init_ram(phys_ram, NULL, "petalogix_ml605.ram", ram_size,
                           &error_fatal);
    memory_region_add_subregion(address_space_mem, MEMORY_BASEADDR, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* 5th parameter 2 means bank-width
     * 10th parameter 0 means little-endian */
    pflash_cfi01_register(FLASH_BASEADDR, "petalogix_ml605.flash", FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          64 * KiB, 2, 0x89, 0x18, 0x0000, 0x0, 0);


    dev = qdev_new("xlnx.xps-intc");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_uint32(dev, "kind-of-intr", 1 << TIMER_IRQ);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, INTC_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(DEVICE(cpu), MB_CPU_IRQ));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    serial_mm_init(address_space_mem, UART16550_BASEADDR + 0x1000, 2,
                   irq[UART16550_IRQ], 115200, serial_hd(0),
                   DEVICE_LITTLE_ENDIAN);

    /* 2 timers at irq 2 @ 100 Mhz.  */
    dev = qdev_new("xlnx.xps-timer");
    qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 100 * 1000000);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ]);

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
    qdev_prop_set_uint32(dma, "freqhz", 100 * 1000000);
    object_property_set_link(OBJECT(dma), "axistream-connected", ds,
                             &error_abort);
    object_property_set_link(OBJECT(dma), "axistream-control-connected", cs,
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dma), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dma), 0, AXIDMA_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dma), 0, irq[AXIDMA_IRQ0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(dma), 1, irq[AXIDMA_IRQ1]);

    {
        SSIBus *spi;

        dev = qdev_new("xlnx.xps-spi");
        qdev_prop_set_enum(dev, "endianness", ENDIAN_MODE_LITTLE);
        qdev_prop_set_uint8(dev, "num-ss-bits", NUM_SPI_FLASHES);
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(busdev, &error_fatal);
        sysbus_mmio_map(busdev, 0, SPI_BASEADDR);
        sysbus_connect_irq(busdev, 0, irq[SPI_IRQ]);

        spi = (SSIBus *)qdev_get_child_bus(dev, "spi");

        for (i = 0; i < NUM_SPI_FLASHES; i++) {
            dinfo = drive_get(IF_MTD, 0, i);
            qemu_irq cs_line;

            dev = qdev_new("n25q128");
            if (dinfo) {
                qdev_prop_set_drive_err(dev, "drive",
                                        blk_by_legacy_dinfo(dinfo),
                                        &error_fatal);
            }
            qdev_prop_set_uint8(dev, "cs", i);
            qdev_realize_and_unref(dev, BUS(spi), &error_fatal);

            cs_line = qdev_get_gpio_in_named(dev, SSI_GPIO_CS, 0);
            sysbus_connect_irq(busdev, i+1, cs_line);
        }
    }

    /* setup PVR to match kernel settings */
    cpu->cfg.pvr_regs[4] = 0xc56b8000;
    cpu->cfg.pvr_regs[5] = 0xc56be000;
    cpu->cfg.pvr_regs[10] = 0x0e000000; /* virtex 6 */

    microblaze_load_kernel(cpu, true, MEMORY_BASEADDR, ram_size,
                           machine->initrd_filename,
                           BINARY_DEVICE_TREE_FILE,
                           NULL);

}

static void petalogix_ml605_machine_init(MachineClass *mc)
{
    mc->desc = "PetaLogix linux refdesign for xilinx ml605 (little endian)";
    mc->init = petalogix_ml605_init;
}

DEFINE_MACHINE("petalogix-ml605", petalogix_ml605_machine_init)
