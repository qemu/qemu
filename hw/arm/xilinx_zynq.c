/*
 * Xilinx Zynq Baseboard System emulation.
 *
 * Copyright (c) 2010 Xilinx.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.croshtwaite@petalogix.com)
 * Copyright (c) 2012 Petalogix Pty Ltd.
 * Written by Haibing Ma
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "net/net.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/block/flash.h"
#include "sysemu/blockdev.h"
#include "hw/loader.h"
#include "hw/ssi.h"
#include "qemu/error-report.h"

#define NUM_SPI_FLASHES 4
#define NUM_QSPI_FLASHES 2
#define NUM_QSPI_BUSSES 2

#define FLASH_SIZE (64 * 1024 * 1024)
#define FLASH_SECTOR_SIZE (128 * 1024)

#define IRQ_OFFSET 32 /* pic interrupts start from index 32 */

#define MPCORE_PERIPHBASE 0xF8F00000
#define ZYNQ_BOARD_MIDR 0x413FC090

static const int dma_irqs[8] = {
    46, 47, 48, 49, 72, 73, 74, 75
};

static struct arm_boot_info zynq_binfo = {};

static void gem_init(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "cadence_gem");
    if (nd->used) {
        qemu_check_nic_model(nd, "cadence_gem");
        qdev_set_nic_properties(dev, nd);
    }
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

static inline void zynq_init_spi_flashes(uint32_t base_addr, qemu_irq irq,
                                         bool is_qspi)
{
    DeviceState *dev;
    SysBusDevice *busdev;
    SSIBus *spi;
    DeviceState *flash_dev;
    int i, j;
    int num_busses =  is_qspi ? NUM_QSPI_BUSSES : 1;
    int num_ss = is_qspi ? NUM_QSPI_FLASHES : NUM_SPI_FLASHES;

    dev = qdev_create(NULL, is_qspi ? "xlnx.ps7-qspi" : "xlnx.ps7-spi");
    qdev_prop_set_uint8(dev, "num-txrx-bytes", is_qspi ? 4 : 1);
    qdev_prop_set_uint8(dev, "num-ss-bits", num_ss);
    qdev_prop_set_uint8(dev, "num-busses", num_busses);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, base_addr);
    if (is_qspi) {
        sysbus_mmio_map(busdev, 1, 0xFC000000);
    }
    sysbus_connect_irq(busdev, 0, irq);

    for (i = 0; i < num_busses; ++i) {
        char bus_name[16];
        qemu_irq cs_line;

        snprintf(bus_name, 16, "spi%d", i);
        spi = (SSIBus *)qdev_get_child_bus(dev, bus_name);

        for (j = 0; j < num_ss; ++j) {
            flash_dev = ssi_create_slave(spi, "n25q128");

            cs_line = qdev_get_gpio_in(flash_dev, 0);
            sysbus_connect_irq(busdev, i * num_ss + j + 1, cs_line);
        }
    }

}

static void zynq_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    ObjectClass *cpu_oc;
    ARMCPU *cpu;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ext_ram = g_new(MemoryRegion, 1);
    MemoryRegion *ocm_ram = g_new(MemoryRegion, 1);
    DeviceState *dev;
    SysBusDevice *busdev;
    qemu_irq pic[64];
    Error *err = NULL;
    int n;

    if (!cpu_model) {
        cpu_model = "cortex-a9";
    }
    cpu_oc = cpu_class_by_name(TYPE_ARM_CPU, cpu_model);

    cpu = ARM_CPU(object_new(object_class_get_name(cpu_oc)));

    object_property_set_int(OBJECT(cpu), ZYNQ_BOARD_MIDR, "midr", &err);
    if (err) {
        error_report("%s", error_get_pretty(err));
        exit(1);
    }

    object_property_set_int(OBJECT(cpu), MPCORE_PERIPHBASE, "reset-cbar", &err);
    if (err) {
        error_report("%s", error_get_pretty(err));
        exit(1);
    }
    object_property_set_bool(OBJECT(cpu), true, "realized", &err);
    if (err) {
        error_report("%s", error_get_pretty(err));
        exit(1);
    }

    /* max 2GB ram */
    if (ram_size > 0x80000000) {
        ram_size = 0x80000000;
    }

    /* DDR remapped to address zero.  */
    memory_region_init_ram(ext_ram, NULL, "zynq.ext_ram", ram_size);
    vmstate_register_ram_global(ext_ram);
    memory_region_add_subregion(address_space_mem, 0, ext_ram);

    /* 256K of on-chip memory */
    memory_region_init_ram(ocm_ram, NULL, "zynq.ocm_ram", 256 << 10);
    vmstate_register_ram_global(ocm_ram);
    memory_region_add_subregion(address_space_mem, 0xFFFC0000, ocm_ram);

    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);

    /* AMD */
    pflash_cfi02_register(0xe2000000, NULL, "zynq.pflash", FLASH_SIZE,
                          dinfo ? dinfo->bdrv : NULL, FLASH_SECTOR_SIZE,
                          FLASH_SIZE/FLASH_SECTOR_SIZE, 1,
                          1, 0x0066, 0x0022, 0x0000, 0x0000, 0x0555, 0x2aa,
                              0);

    dev = qdev_create(NULL, "xilinx,zynq_slcr");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xF8000000);

    dev = qdev_create(NULL, "a9mpcore_priv");
    qdev_prop_set_uint32(dev, "num-cpu", 1);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, MPCORE_PERIPHBASE);
    sysbus_connect_irq(busdev, 0,
                       qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));

    for (n = 0; n < 64; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    zynq_init_spi_flashes(0xE0006000, pic[58-IRQ_OFFSET], false);
    zynq_init_spi_flashes(0xE0007000, pic[81-IRQ_OFFSET], false);
    zynq_init_spi_flashes(0xE000D000, pic[51-IRQ_OFFSET], true);

    sysbus_create_simple("xlnx,ps7-usb", 0xE0002000, pic[53-IRQ_OFFSET]);
    sysbus_create_simple("xlnx,ps7-usb", 0xE0003000, pic[76-IRQ_OFFSET]);

    sysbus_create_simple("cadence_uart", 0xE0000000, pic[59-IRQ_OFFSET]);
    sysbus_create_simple("cadence_uart", 0xE0001000, pic[82-IRQ_OFFSET]);

    sysbus_create_varargs("cadence_ttc", 0xF8001000,
            pic[42-IRQ_OFFSET], pic[43-IRQ_OFFSET], pic[44-IRQ_OFFSET], NULL);
    sysbus_create_varargs("cadence_ttc", 0xF8002000,
            pic[69-IRQ_OFFSET], pic[70-IRQ_OFFSET], pic[71-IRQ_OFFSET], NULL);

    gem_init(&nd_table[0], 0xE000B000, pic[54-IRQ_OFFSET]);
    gem_init(&nd_table[1], 0xE000C000, pic[77-IRQ_OFFSET]);

    dev = qdev_create(NULL, "generic-sdhci");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xE0100000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[56-IRQ_OFFSET]);

    dev = qdev_create(NULL, "generic-sdhci");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xE0101000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[79-IRQ_OFFSET]);

    dev = qdev_create(NULL, "pl330");
    qdev_prop_set_uint8(dev, "num_chnls",  8);
    qdev_prop_set_uint8(dev, "num_periph_req",  4);
    qdev_prop_set_uint8(dev, "num_events",  16);

    qdev_prop_set_uint8(dev, "data_width",  64);
    qdev_prop_set_uint8(dev, "wr_cap",  8);
    qdev_prop_set_uint8(dev, "wr_q_dep",  16);
    qdev_prop_set_uint8(dev, "rd_cap",  8);
    qdev_prop_set_uint8(dev, "rd_q_dep",  16);
    qdev_prop_set_uint16(dev, "data_buffer_dep",  256);

    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xF8003000);
    sysbus_connect_irq(busdev, 0, pic[45-IRQ_OFFSET]); /* abort irq line */
    for (n = 0; n < 8; ++n) { /* event irqs */
        sysbus_connect_irq(busdev, n + 1, pic[dma_irqs[n] - IRQ_OFFSET]);
    }

    zynq_binfo.ram_size = ram_size;
    zynq_binfo.kernel_filename = kernel_filename;
    zynq_binfo.kernel_cmdline = kernel_cmdline;
    zynq_binfo.initrd_filename = initrd_filename;
    zynq_binfo.nb_cpus = 1;
    zynq_binfo.board_id = 0xd32;
    zynq_binfo.loader_start = 0;
    arm_load_kernel(ARM_CPU(first_cpu), &zynq_binfo);
}

static QEMUMachine zynq_machine = {
    .name = "xilinx-zynq-a9",
    .desc = "Xilinx Zynq Platform Baseboard for Cortex-A9",
    .init = zynq_init,
    .block_default_type = IF_SCSI,
    .max_cpus = 1,
    .no_sdcard = 1,
};

static void zynq_machine_init(void)
{
    qemu_register_machine(&zynq_machine);
}

machine_init(zynq_machine_init);
