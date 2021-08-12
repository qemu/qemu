/*
 * SmartFusion2 SOM starter kit(from Emcraft) emulation.
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
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
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/arm/boot.h"
#include "hw/qdev-clock.h"
#include "exec/address-spaces.h"
#include "hw/arm/msf2-soc.h"

#define DDR_BASE_ADDRESS      0xA0000000
#define DDR_SIZE              (64 * MiB)

#define M2S010_ENVM_SIZE      (256 * KiB)
#define M2S010_ESRAM_SIZE     (64 * KiB)

static void emcraft_sf2_s2s010_init(MachineState *machine)
{
    DeviceState *dev;
    DeviceState *spi_flash;
    MSF2State *soc;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    DriveInfo *dinfo = drive_get_next(IF_MTD);
    qemu_irq cs_line;
    BusState *spi_bus;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ddr = g_new(MemoryRegion, 1);
    Clock *m3clk;

    if (strcmp(machine->cpu_type, mc->default_cpu_type) != 0) {
        error_report("This board can only be used with CPU %s",
                     mc->default_cpu_type);
        exit(1);
    }

    memory_region_init_ram(ddr, NULL, "ddr-ram", DDR_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, DDR_BASE_ADDRESS, ddr);

    dev = qdev_new(TYPE_MSF2_SOC);
    qdev_prop_set_string(dev, "part-name", "M2S010");
    qdev_prop_set_string(dev, "cpu-type", mc->default_cpu_type);

    qdev_prop_set_uint64(dev, "eNVM-size", M2S010_ENVM_SIZE);
    qdev_prop_set_uint64(dev, "eSRAM-size", M2S010_ESRAM_SIZE);

    /*
     * CPU clock and peripheral clocks(APB0, APB1)are configurable
     * in Libero. CPU clock is divided by APB0 and APB1 divisors for
     * peripherals. Emcraft's SoM kit comes with these settings by default.
     */
    /* This clock doesn't need migration because it is fixed-frequency */
    m3clk = clock_new(OBJECT(machine), "m3clk");
    clock_set_hz(m3clk, 142 * 1000000);
    qdev_connect_clock_in(dev, "m3clk", m3clk);
    qdev_prop_set_uint32(dev, "apb0div", 2);
    qdev_prop_set_uint32(dev, "apb1div", 2);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    soc = MSF2_SOC(dev);

    /* Attach SPI flash to SPI0 controller */
    spi_bus = qdev_get_child_bus(dev, "spi0");
    spi_flash = qdev_new("s25sl12801");
    qdev_prop_set_uint8(spi_flash, "spansion-cr2nv", 1);
    if (dinfo) {
        qdev_prop_set_drive_err(spi_flash, "drive",
                                blk_by_legacy_dinfo(dinfo), &error_fatal);
    }
    qdev_realize_and_unref(spi_flash, spi_bus, &error_fatal);
    cs_line = qdev_get_gpio_in_named(spi_flash, SSI_GPIO_CS, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&soc->spi[0]), 1, cs_line);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       soc->envm_size);
}

static void emcraft_sf2_machine_init(MachineClass *mc)
{
    mc->desc = "SmartFusion2 SOM kit from Emcraft (M2S010)";
    mc->init = emcraft_sf2_s2s010_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m3");
}

DEFINE_MACHINE("emcraft-sf2", emcraft_sf2_machine_init)
