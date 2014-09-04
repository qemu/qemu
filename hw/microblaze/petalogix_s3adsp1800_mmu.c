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

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "net/net.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "hw/devices.h"
#include "hw/boards.h"
#include "sysemu/blockdev.h"
#include "exec/address-spaces.h"

#include "boot.h"

#define LMB_BRAM_SIZE  (128 * 1024)
#define FLASH_SIZE     (16 * 1024 * 1024)

#define BINARY_DEVICE_TREE_FILE "petalogix-s3adsp1800.dtb"

#define MEMORY_BASEADDR 0x90000000
#define FLASH_BASEADDR 0xa0000000
#define INTC_BASEADDR 0x81800000
#define TIMER_BASEADDR 0x83c00000
#define UARTLITE_BASEADDR 0x84000000
#define ETHLITE_BASEADDR 0x81000000

#define TIMER_IRQ           0
#define ETHLITE_IRQ         1
#define UARTLITE_IRQ        3

static void machine_cpu_reset(MicroBlazeCPU *cpu)
{
    CPUMBState *env = &cpu->env;

    env->pvr.regs[10] = 0x0c000000; /* spartan 3a dsp family.  */
}

static void
petalogix_s3adsp1800_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    DeviceState *dev;
    MicroBlazeCPU *cpu;
    DriveInfo *dinfo;
    int i;
    hwaddr ddr_base = MEMORY_BASEADDR;
    MemoryRegion *phys_lmb_bram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    qemu_irq irq[32];
    MemoryRegion *sysmem = get_system_memory();

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "microblaze";
    }
    cpu = cpu_mb_init(cpu_model);

    /* Attach emulated BRAM through the LMB.  */
    memory_region_init_ram(phys_lmb_bram, NULL,
                           "petalogix_s3adsp1800.lmb_bram", LMB_BRAM_SIZE);
    vmstate_register_ram_global(phys_lmb_bram);
    memory_region_add_subregion(sysmem, 0x00000000, phys_lmb_bram);

    memory_region_init_ram(phys_ram, NULL, "petalogix_s3adsp1800.ram", ram_size);
    vmstate_register_ram_global(phys_ram);
    memory_region_add_subregion(sysmem, ddr_base, phys_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi01_register(FLASH_BASEADDR,
                          NULL, "petalogix_s3adsp1800.flash", FLASH_SIZE,
                          dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                          FLASH_SIZE >> 16,
                          1, 0x89, 0x18, 0x0000, 0x0, 1);

    dev = qdev_create(NULL, "xlnx.xps-intc");
    qdev_prop_set_uint32(dev, "kind-of-intr",
                         1 << ETHLITE_IRQ | 1 << UARTLITE_IRQ);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, INTC_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(DEVICE(cpu), MB_CPU_IRQ));
    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    sysbus_create_simple("xlnx.xps-uartlite", UARTLITE_BASEADDR,
                         irq[UARTLITE_IRQ]);

    /* 2 timers at irq 2 @ 62 Mhz.  */
    dev = qdev_create(NULL, "xlnx.xps-timer");
    qdev_prop_set_uint32(dev, "one-timer-only", 0);
    qdev_prop_set_uint32(dev, "clock-frequency", 62 * 1000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[TIMER_IRQ]);

    qemu_check_nic_model(&nd_table[0], "xlnx.xps-ethernetlite");
    dev = qdev_create(NULL, "xlnx.xps-ethernetlite");
    qdev_set_nic_properties(dev, &nd_table[0]);
    qdev_prop_set_uint32(dev, "tx-ping-pong", 0);
    qdev_prop_set_uint32(dev, "rx-ping-pong", 0);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, ETHLITE_BASEADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[ETHLITE_IRQ]);

    microblaze_load_kernel(cpu, ddr_base, ram_size,
                           machine->initrd_filename,
                           BINARY_DEVICE_TREE_FILE,
                           machine_cpu_reset);
}

static QEMUMachine petalogix_s3adsp1800_machine = {
    .name = "petalogix-s3adsp1800",
    .desc = "PetaLogix linux refdesign for xilinx Spartan 3ADSP1800",
    .init = petalogix_s3adsp1800_init,
    .is_default = 1,
};

static void petalogix_s3adsp1800_machine_init(void)
{
    qemu_register_machine(&petalogix_s3adsp1800_machine);
}

machine_init(petalogix_s3adsp1800_machine_init);
