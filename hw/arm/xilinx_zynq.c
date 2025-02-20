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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "net/net.h"
#include "system/system.h"
#include "hw/boards.h"
#include "hw/block/flash.h"
#include "hw/loader.h"
#include "hw/adc/zynq-xadc.h"
#include "hw/ssi/ssi.h"
#include "hw/usb/chipidea.h"
#include "qemu/error-report.h"
#include "hw/sd/sdhci.h"
#include "hw/char/cadence_uart.h"
#include "hw/net/cadence_gem.h"
#include "hw/cpu/a9mpcore.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "system/reset.h"
#include "qom/object.h"
#include "exec/tswap.h"
#include "target/arm/cpu-qom.h"
#include "qapi/visitor.h"

#define TYPE_ZYNQ_MACHINE MACHINE_TYPE_NAME("xilinx-zynq-a9")
OBJECT_DECLARE_SIMPLE_TYPE(ZynqMachineState, ZYNQ_MACHINE)

/* board base frequency: 33.333333 MHz */
#define PS_CLK_FREQUENCY (100 * 1000 * 1000 / 3)

#define NUM_SPI_FLASHES 4
#define NUM_QSPI_FLASHES 2
#define NUM_QSPI_BUSSES 2

#define FLASH_SIZE (64 * 1024 * 1024)
#define FLASH_SECTOR_SIZE (128 * 1024)

#define MPCORE_PERIPHBASE 0xF8F00000
#define ZYNQ_BOARD_MIDR 0x413FC090

#define GIC_EXT_IRQS 64 /* Zynq 7000 SoC */

static const int dma_irqs[8] = {
    46, 47, 48, 49, 72, 73, 74, 75
};

#define BOARD_SETUP_ADDR        0x100

#define SLCR_LOCK_OFFSET        0x004
#define SLCR_UNLOCK_OFFSET      0x008
#define SLCR_ARM_PLL_OFFSET     0x100

#define SLCR_XILINX_UNLOCK_KEY  0xdf0d
#define SLCR_XILINX_LOCK_KEY    0x767b

#define ZYNQ_SDHCI_CAPABILITIES 0x69ec0080  /* Datasheet: UG585 (v1.12.1) */

#define ARMV7_IMM16(x) (extract32((x),  0, 12) | \
                        extract32((x), 12,  4) << 16)

/* Write immediate val to address r0 + addr. r0 should contain base offset
 * of the SLCR block. Clobbers r1.
 */

#define SLCR_WRITE(addr, val) \
    0xe3001000 + ARMV7_IMM16(extract32((val),  0, 16)), /* movw r1 ... */ \
    0xe3401000 + ARMV7_IMM16(extract32((val), 16, 16)), /* movt r1 ... */ \
    0xe5801000 + (addr)

#define ZYNQ_MAX_CPUS 2

struct ZynqMachineState {
    MachineState parent;
    Clock *ps_clk;
    ARMCPU *cpu[ZYNQ_MAX_CPUS];
    uint8_t boot_mode;
};

static void zynq_write_board_setup(ARMCPU *cpu,
                                   const struct arm_boot_info *info)
{
    int n;
    uint32_t board_setup_blob[] = {
        0xe3a004f8, /* mov r0, #0xf8000000 */
        SLCR_WRITE(SLCR_UNLOCK_OFFSET, SLCR_XILINX_UNLOCK_KEY),
        SLCR_WRITE(SLCR_ARM_PLL_OFFSET, 0x00014008),
        SLCR_WRITE(SLCR_LOCK_OFFSET, SLCR_XILINX_LOCK_KEY),
        0xe12fff1e, /* bx lr */
    };
    for (n = 0; n < ARRAY_SIZE(board_setup_blob); n++) {
        board_setup_blob[n] = tswap32(board_setup_blob[n]);
    }
    rom_add_blob_fixed("board-setup", board_setup_blob,
                       sizeof(board_setup_blob), BOARD_SETUP_ADDR);
}

static struct arm_boot_info zynq_binfo = {};

static void gem_init(uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_CADENCE_GEM);
    qemu_configure_nic_device(dev, true, NULL);
    object_property_set_int(OBJECT(dev), "phy-addr", 7, &error_abort);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

static inline int zynq_init_spi_flashes(uint32_t base_addr, qemu_irq irq,
                                        bool is_qspi, int unit0)
{
    int unit = unit0;
    DeviceState *dev;
    SysBusDevice *busdev;
    SSIBus *spi;
    DeviceState *flash_dev;
    int i, j;
    int num_busses =  is_qspi ? NUM_QSPI_BUSSES : 1;
    int num_ss = is_qspi ? NUM_QSPI_FLASHES : NUM_SPI_FLASHES;

    dev = qdev_new(is_qspi ? "xlnx.ps7-qspi" : "xlnx.ps7-spi");
    qdev_prop_set_uint8(dev, "num-txrx-bytes", is_qspi ? 4 : 1);
    qdev_prop_set_uint8(dev, "num-ss-bits", num_ss);
    qdev_prop_set_uint8(dev, "num-busses", num_busses);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
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
            DriveInfo *dinfo = drive_get(IF_MTD, 0, unit++);
            flash_dev = qdev_new("n25q128");
            if (dinfo) {
                qdev_prop_set_drive_err(flash_dev, "drive",
                                        blk_by_legacy_dinfo(dinfo),
                                        &error_fatal);
            }
            qdev_prop_set_uint8(flash_dev, "cs", j);
            qdev_realize_and_unref(flash_dev, BUS(spi), &error_fatal);

            cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
            sysbus_connect_irq(busdev, i * num_ss + j + 1, cs_line);
        }
    }

    return unit;
}

static void zynq_set_boot_mode(Object *obj, const char *str,
                                               Error **errp)
{
    ZynqMachineState *m = ZYNQ_MACHINE(obj);
    uint8_t mode = 0;

    if (!strncasecmp(str, "qspi", 4)) {
        mode = 1;
    } else if (!strncasecmp(str, "sd", 2)) {
        mode = 5;
    } else if (!strncasecmp(str, "nor", 3)) {
        mode = 2;
    } else if (!strncasecmp(str, "jtag", 4)) {
        mode = 0;
    } else {
        error_setg(errp, "%s boot mode not supported", str);
        return;
    }
    m->boot_mode = mode;
}

static void zynq_init(MachineState *machine)
{
    ZynqMachineState *zynq_machine = ZYNQ_MACHINE(machine);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ocm_ram = g_new(MemoryRegion, 1);
    DeviceState *dev, *slcr;
    SysBusDevice *busdev;
    qemu_irq pic[GIC_EXT_IRQS];
    int n;
    unsigned int smp_cpus = machine->smp.cpus;

    /* max 2GB ram */
    if (machine->ram_size > 2 * GiB) {
        error_report("RAM size more than 2 GiB is not supported");
        exit(EXIT_FAILURE);
    }

    for (n = 0; n < smp_cpus; n++) {
        Object *cpuobj = object_new(machine->cpu_type);

        object_property_set_int(cpuobj, "midr", ZYNQ_BOARD_MIDR,
                                &error_fatal);
        object_property_set_int(cpuobj, "reset-cbar", MPCORE_PERIPHBASE,
                                &error_fatal);

        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);

        zynq_machine->cpu[n] = ARM_CPU(cpuobj);
    }

    /* DDR remapped to address zero.  */
    memory_region_add_subregion(address_space_mem, 0, machine->ram);

    /* 256K of on-chip memory */
    memory_region_init_ram(ocm_ram, NULL, "zynq.ocm_ram", 256 * KiB,
                           &error_fatal);
    memory_region_add_subregion(address_space_mem, 0xFFFC0000, ocm_ram);

    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);

    /* AMD */
    pflash_cfi02_register(0xe2000000, "zynq.pflash", FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          FLASH_SECTOR_SIZE, 1,
                          1, 0x0066, 0x0022, 0x0000, 0x0000, 0x0555, 0x2aa,
                          0);

    /* Create the main clock source, and feed slcr with it */
    zynq_machine->ps_clk = CLOCK(object_new(TYPE_CLOCK));
    object_property_add_child(OBJECT(zynq_machine), "ps_clk",
                              OBJECT(zynq_machine->ps_clk));
    object_unref(OBJECT(zynq_machine->ps_clk));
    clock_set_hz(zynq_machine->ps_clk, PS_CLK_FREQUENCY);

    /* Create slcr, keep a pointer to connect clocks */
    slcr = qdev_new("xilinx-zynq_slcr");
    qdev_connect_clock_in(slcr, "ps_clk", zynq_machine->ps_clk);
    qdev_prop_set_uint8(slcr, "boot-mode", zynq_machine->boot_mode);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(slcr), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(slcr), 0, 0xF8000000);

    dev = qdev_new(TYPE_A9MPCORE_PRIV);
    qdev_prop_set_uint32(dev, "num-cpu", smp_cpus);
    qdev_prop_set_uint32(dev, "num-irq", GIC_EXT_IRQS + GIC_INTERNAL);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, MPCORE_PERIPHBASE);
    zynq_binfo.gic_cpu_if_addr = MPCORE_PERIPHBASE + 0x100;
    sysbus_create_varargs("l2x0", MPCORE_PERIPHBASE + 0x2000, NULL);
    for (n = 0; n < smp_cpus; n++) {
        /* See "hw/intc/arm_gic.h" for the IRQ line association */
        DeviceState *cpudev = DEVICE(zynq_machine->cpu[n]);
        sysbus_connect_irq(busdev, n,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(busdev, smp_cpus + n,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
    }

    for (n = 0; n < GIC_EXT_IRQS; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    n = zynq_init_spi_flashes(0xE0006000, pic[58 - GIC_INTERNAL], false, 0);
    n = zynq_init_spi_flashes(0xE0007000, pic[81 - GIC_INTERNAL], false, n);
    n = zynq_init_spi_flashes(0xE000D000, pic[51 - GIC_INTERNAL], true, n);

    sysbus_create_simple(TYPE_CHIPIDEA, 0xE0002000, pic[53 - GIC_INTERNAL]);
    sysbus_create_simple(TYPE_CHIPIDEA, 0xE0003000, pic[76 - GIC_INTERNAL]);

    dev = qdev_new(TYPE_CADENCE_UART);
    busdev = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    qdev_connect_clock_in(dev, "refclk",
                          qdev_get_clock_out(slcr, "uart0_ref_clk"));
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0xE0000000);
    sysbus_connect_irq(busdev, 0, pic[59 - GIC_INTERNAL]);
    dev = qdev_new(TYPE_CADENCE_UART);
    busdev = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", serial_hd(1));
    qdev_connect_clock_in(dev, "refclk",
                          qdev_get_clock_out(slcr, "uart1_ref_clk"));
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0xE0001000);
    sysbus_connect_irq(busdev, 0, pic[82 - GIC_INTERNAL]);

    sysbus_create_varargs("cadence_ttc", 0xF8001000,
            pic[42-GIC_INTERNAL], pic[43-GIC_INTERNAL], pic[44-GIC_INTERNAL], NULL);
    sysbus_create_varargs("cadence_ttc", 0xF8002000,
            pic[69-GIC_INTERNAL], pic[70-GIC_INTERNAL], pic[71-GIC_INTERNAL], NULL);

    gem_init(0xE000B000, pic[54 - GIC_INTERNAL]);
    gem_init(0xE000C000, pic[77 - GIC_INTERNAL]);

    for (n = 0; n < 2; n++) {
        int hci_irq = n ? 79 : 56;
        hwaddr hci_addr = n ? 0xE0101000 : 0xE0100000;
        DriveInfo *di;
        BlockBackend *blk;
        DeviceState *carddev;

        /* Compatible with:
         * - SD Host Controller Specification Version 2.0 Part A2
         * - SDIO Specification Version 2.0
         * - MMC Specification Version 3.31
         */
        dev = qdev_new(TYPE_SYSBUS_SDHCI);
        qdev_prop_set_uint8(dev, "sd-spec-version", 2);
        qdev_prop_set_uint64(dev, "capareg", ZYNQ_SDHCI_CAPABILITIES);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, hci_addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[hci_irq - GIC_INTERNAL]);

        di = drive_get(IF_SD, 0, n);
        blk = di ? blk_by_legacy_dinfo(di) : NULL;
        carddev = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
        qdev_realize_and_unref(carddev, qdev_get_child_bus(dev, "sd-bus"),
                               &error_fatal);
    }

    dev = qdev_new(TYPE_ZYNQ_XADC);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xF8007100);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[39-GIC_INTERNAL]);

    dev = qdev_new("pl330");
    object_property_set_link(OBJECT(dev), "memory",
                             OBJECT(address_space_mem),
                             &error_fatal);
    qdev_prop_set_uint8(dev, "num_chnls",  8);
    qdev_prop_set_uint8(dev, "num_periph_req",  4);
    qdev_prop_set_uint8(dev, "num_events",  16);

    qdev_prop_set_uint8(dev, "data_width",  64);
    qdev_prop_set_uint8(dev, "wr_cap",  8);
    qdev_prop_set_uint8(dev, "wr_q_dep",  16);
    qdev_prop_set_uint8(dev, "rd_cap",  8);
    qdev_prop_set_uint8(dev, "rd_q_dep",  16);
    qdev_prop_set_uint16(dev, "data_buffer_dep",  256);

    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0xF8003000);
    sysbus_connect_irq(busdev, 0, pic[45-GIC_INTERNAL]); /* abort irq line */
    for (n = 0; n < ARRAY_SIZE(dma_irqs); ++n) { /* event irqs */
        sysbus_connect_irq(busdev, n + 1, pic[dma_irqs[n] - GIC_INTERNAL]);
    }

    dev = qdev_new("xlnx.ps7-dev-cfg");
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, pic[40 - GIC_INTERNAL]);
    sysbus_mmio_map(busdev, 0, 0xF8007000);

    /*
     * Refer to the ug585-Zynq-7000-TRM manual B.3 (Module Summary) and
     * the zynq-7000.dtsi. Add placeholders for unimplemented devices.
     */
    create_unimplemented_device("zynq.i2c0", 0xE0004000, 4 * KiB);
    create_unimplemented_device("zynq.i2c1", 0xE0005000, 4 * KiB);
    create_unimplemented_device("zynq.can0", 0xE0008000, 4 * KiB);
    create_unimplemented_device("zynq.can1", 0xE0009000, 4 * KiB);
    create_unimplemented_device("zynq.gpio", 0xE000A000, 4 * KiB);
    create_unimplemented_device("zynq.smcc", 0xE000E000, 4 * KiB);

    /* Direct Memory Access Controller, PL330, Non-Secure Mode */
    create_unimplemented_device("zynq.dma_ns", 0xF8004000, 4 * KiB);

    /* System Watchdog Timer Registers */
    create_unimplemented_device("zynq.swdt", 0xF8005000, 4 * KiB);

    /* DDR memory controller */
    create_unimplemented_device("zynq.ddrc", 0xF8006000, 4 * KiB);

    /* AXI_HP Interface (AFI) */
    create_unimplemented_device("zynq.axi_hp0", 0xF8008000, 0x28);
    create_unimplemented_device("zynq.axi_hp1", 0xF8009000, 0x28);
    create_unimplemented_device("zynq.axi_hp2", 0xF800A000, 0x28);
    create_unimplemented_device("zynq.axi_hp3", 0xF800B000, 0x28);

    create_unimplemented_device("zynq.efuse", 0xF800d000, 0x20);

    /* Embedded Trace Buffer */
    create_unimplemented_device("zynq.etb", 0xF8801000, 4 * KiB);

    /* Cross Trigger Interface, ETB and TPIU */
    create_unimplemented_device("zynq.cti_etb_tpiu", 0xF8802000, 4 * KiB);

    /* Trace Port Interface Unit */
    create_unimplemented_device("zynq.tpiu", 0xF8803000, 4 * KiB);

    /* CoreSight Trace Funnel */
    create_unimplemented_device("zynq.funnel", 0xF8804000, 4 * KiB);

    /* Instrumentation Trace Macrocell */
    create_unimplemented_device("zynq.itm", 0xF8805000, 4 * KiB);

    /* Cross Trigger Interface, FTM */
    create_unimplemented_device("zynq.cti_ftm", 0xF8809000, 4 * KiB);

    /* Fabric Trace Macrocell */
    create_unimplemented_device("zynq.ftm", 0xF880B000, 4 * KiB);

    /* Cortex A9 Performance Monitoring Unit, CPU */
    create_unimplemented_device("cortex-a9.pmu0", 0xF8891000, 4 * KiB);
    create_unimplemented_device("cortex-a9.pmu1", 0xF8893000, 4 * KiB);

    /* Cross Trigger Interface, CPU */
    create_unimplemented_device("zynq.cpu_cti0", 0xF8898000, 4 * KiB);
    create_unimplemented_device("zynq.cpu_cti1", 0xF8899000, 4 * KiB);

    /* CoreSight PTM-A9, CPU */
    create_unimplemented_device("cortex-a9.ptm0", 0xF889c000, 4 * KiB);
    create_unimplemented_device("cortex-a9.ptm1", 0xF889d000, 4 * KiB);

    /* AMBA NIC301 TrustZone */
    create_unimplemented_device("zynq.trustZone", 0xF8900000, 0x20);

    /* AMBA Network Interconnect Advanced Quality of Service (QoS-301) */
    create_unimplemented_device("zynq.qos301_cpu", 0xF8946000, 0x130);
    create_unimplemented_device("zynq.qos301_dmac", 0xF8947000, 0x130);
    create_unimplemented_device("zynq.qos301_iou", 0xF8948000, 0x130);

    zynq_binfo.ram_size = machine->ram_size;
    zynq_binfo.board_id = 0xd32;
    zynq_binfo.loader_start = 0;
    zynq_binfo.board_setup_addr = BOARD_SETUP_ADDR;
    zynq_binfo.write_board_setup = zynq_write_board_setup;

    arm_load_kernel(zynq_machine->cpu[0], machine, &zynq_binfo);
}

static void zynq_machine_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a9"),
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);
    ObjectProperty *prop;
    mc->desc = "Xilinx Zynq 7000 Platform Baseboard for Cortex-A9";
    mc->init = zynq_init;
    mc->max_cpus = ZYNQ_MAX_CPUS;
    mc->ignore_memory_transaction_failures = true;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "zynq.ext_ram";
    prop = object_class_property_add_str(oc, "boot-mode", NULL,
                                         zynq_set_boot_mode);
    object_class_property_set_description(oc, "boot-mode",
                                          "Supported boot modes:"
                                          " jtag qspi sd nor");
    object_property_set_default_str(prop, "qspi");
}

static const TypeInfo zynq_machine_type = {
    .name = TYPE_ZYNQ_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = zynq_machine_class_init,
    .instance_size = sizeof(ZynqMachineState),
};

static void zynq_machine_register_types(void)
{
    type_register_static(&zynq_machine_type);
}

type_init(zynq_machine_register_types)
