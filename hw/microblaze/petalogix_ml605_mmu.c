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

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "net/net.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "hw/devices.h"
#include "hw/boards.h"
#include "sysemu/blockdev.h"
#include "hw/char/serial.h"
#include "exec/address-spaces.h"
#include "hw/ssi.h"

#include "boot.h"

#include "hw/stream.h"

#define LMB_BRAM_SIZE  (128 * 1024)
#define FLASH_SIZE     (32 * 1024 * 1024)

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

static void machine_cpu_reset(MicroBlazeCPU *cpu)
{
    CPUMBState *env = &cpu->env;

    env->pvr.regs[10] = 0x0e000000; /* virtex 6 */
    /* setup pvr to match kernel setting */
    env->pvr.regs[5] |= PVR5_DCACHE_WRITEBACK_MASK;
    env->pvr.regs[0] |= PVR0_USE_FPU_MASK | PVR0_ENDI;
    env->pvr.regs[0] = (env->pvr.regs[0] & ~PVR0_VERSION_MASK) | (0x14 << 8);
    env->pvr.regs[2] ^= PVR2_USE_FPU2_MASK;
    env->pvr.regs[4] = 0xc56b8000;
    env->pvr.regs[5] = 0xc56be000;
}

static void
petalogix_ml605_init(QEMUMachineInitArgs *args)
{
    ram_addr_t ram_size = args->ram_size;
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev, *dma, *eth0;
    Object *ds, *cs;
    MicroBlazeCPU *cpu;
    SysBusDevice *busdev;
    DriveInfo *dinfo;
    int i;
    hwaddr ddr_base = MEMORY_BASEADDR;
    MemoryRegion *phys_lmb_bram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32];

    /* init CPUs */
    cpu = MICROBLAZE_CPU(object_new(TYPE_MICROBLAZE_CPU));
    object_property_set_bool(OBJECT(cpu), true, "realized", &error_abort);

    /* Attach emulated BRAM through the LMB.  */
    memory_region_init_ram(phys_lmb_bram, NULL, "petalogix_ml605.lmb_bram",
                           LMB_BRAM_SIZE);
    vmstate_register_ram_global(phys_lmb_bram);
    memory_region_add_subregion(address_space_mem, 0x00000000, phys_lmb_bram);

    memory_region_init_ram(phys_ram, NULL, "petalogix_ml605.ram", ram_size);
    vmstate_register_ram_global(phys_ram);
    memory_region_add_subregion(address_space_mem, ddr_base, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    /* 5th parameter 2 means bank-width
     * 10th paremeter 0 means little-endian */
    pflash_cfi01_register(FLASH_BASEADDR,
                          NULL, "petalogix_ml605.flash", FLASH_SIZE,
                          dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                          FLASH_SIZE >> 16,
                          2, 0x89, 0x18, 0x0000, 0x0, 0);


    dev = qdev_create(NULL, "xlnx.xps-intc");
    qdev_prop_set_uint32(dev, "kind-of-intr", 1 << TIMER_IRQ);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, INTC_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(DEVICE(cpu), MB_CPU_IRQ));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    serial_mm_init(address_space_mem, UART16550_BASEADDR + 0x1000, 2,
                   irq[UART16550_IRQ], 115200, serial_hds[0],
                   DEVICE_LITTLE_ENDIAN);

    /* 2 timers at irq 2 @ 100 Mhz.  */
    dev = qdev_create(NULL, "xlnx.xps-timer");
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 100 * 1000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ]);

    /* axi ethernet and dma initialization. */
    qemu_check_nic_model(&nd_table[0], "xlnx.axi-ethernet");
    eth0 = qdev_create(NULL, "xlnx.axi-ethernet");
    dma = qdev_create(NULL, "xlnx.axi-dma");

    /* FIXME: attach to the sysbus instead */
    object_property_add_child(qdev_get_machine(), "xilinx-eth", OBJECT(eth0),
                              NULL);
    object_property_add_child(qdev_get_machine(), "xilinx-dma", OBJECT(dma),
                              NULL);

    ds = object_property_get_link(OBJECT(dma),
                                  "axistream-connected-target", NULL);
    cs = object_property_get_link(OBJECT(dma),
                                  "axistream-control-connected-target", NULL);
    qdev_set_nic_properties(eth0, &nd_table[0]);
    qdev_prop_set_uint32(eth0, "rxmem", 0x1000);
    qdev_prop_set_uint32(eth0, "txmem", 0x1000);
    object_property_set_link(OBJECT(eth0), OBJECT(ds),
                             "axistream-connected", &error_abort);
    object_property_set_link(OBJECT(eth0), OBJECT(cs),
                             "axistream-control-connected", &error_abort);
    qdev_init_nofail(eth0);
    sysbus_mmio_map(SYS_BUS_DEVICE(eth0), 0, AXIENET_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(eth0), 0, irq[AXIENET_IRQ]);

    ds = object_property_get_link(OBJECT(eth0),
                                  "axistream-connected-target", NULL);
    cs = object_property_get_link(OBJECT(eth0),
                                  "axistream-control-connected-target", NULL);
    qdev_prop_set_uint32(dma, "freqhz", 100 * 1000000);
    object_property_set_link(OBJECT(dma), OBJECT(ds),
                             "axistream-connected", &error_abort);
    object_property_set_link(OBJECT(dma), OBJECT(cs),
                             "axistream-control-connected", &error_abort);
    qdev_init_nofail(dma);
    sysbus_mmio_map(SYS_BUS_DEVICE(dma), 0, AXIDMA_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dma), 0, irq[AXIDMA_IRQ0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(dma), 1, irq[AXIDMA_IRQ1]);

    {
        SSIBus *spi;

        dev = qdev_create(NULL, "xlnx.xps-spi");
        qdev_prop_set_uint8(dev, "num-ss-bits", NUM_SPI_FLASHES);
        qdev_init_nofail(dev);
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, SPI_BASEADDR);
        sysbus_connect_irq(busdev, 0, irq[SPI_IRQ]);

        spi = (SSIBus *)qdev_get_child_bus(dev, "spi");

        for (i = 0; i < NUM_SPI_FLASHES; i++) {
            qemu_irq cs_line;

            dev = ssi_create_slave(spi, "n25q128");
            cs_line = qdev_get_gpio_in(dev, 0);
            sysbus_connect_irq(busdev, i+1, cs_line);
        }
    }

    microblaze_load_kernel(cpu, ddr_base, ram_size,
                           args->initrd_filename,
                           BINARY_DEVICE_TREE_FILE,
                           machine_cpu_reset);

}

static QEMUMachine petalogix_ml605_machine = {
    .name = "petalogix-ml605",
    .desc = "PetaLogix linux refdesign for xilinx ml605 little endian",
    .init = petalogix_ml605_init,
    .is_default = 0,
};

static void petalogix_ml605_machine_init(void)
{
    qemu_register_machine(&petalogix_ml605_machine);
}

machine_init(petalogix_ml605_machine_init);
