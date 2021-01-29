/*
 * ARM V2M MPS2 board emulation.
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* The MPS2 and MPS2+ dev boards are FPGA based (the 2+ has a bigger
 * FPGA but is otherwise the same as the 2). Since the CPU itself
 * and most of the devices are in the FPGA, the details of the board
 * as seen by the guest depend significantly on the FPGA image.
 * We model the following FPGA images:
 *  "mps2-an385" -- Cortex-M3 as documented in ARM Application Note AN385
 *  "mps2-an386" -- Cortex-M4 as documented in ARM Application Note AN386
 *  "mps2-an500" -- Cortex-M7 as documented in ARM Application Note AN500
 *  "mps2-an511" -- Cortex-M3 'DesignStart' as documented in AN511
 *
 * Links to the TRM for the board itself and to the various Application
 * Notes which document the FPGA images can be found here:
 *   https://developer.arm.com/products/system-design/development-boards/cortex-m-prototyping-system
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/arm/boot.h"
#include "hw/arm/armv7m.h"
#include "hw/or-irq.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/misc/unimp.h"
#include "hw/char/cmsdk-apb-uart.h"
#include "hw/timer/cmsdk-apb-timer.h"
#include "hw/timer/cmsdk-apb-dualtimer.h"
#include "hw/misc/mps2-scc.h"
#include "hw/misc/mps2-fpgaio.h"
#include "hw/ssi/pl022.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/net/lan9118.h"
#include "net/net.h"
#include "hw/watchdog/cmsdk-apb-watchdog.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"

typedef enum MPS2FPGAType {
    FPGA_AN385,
    FPGA_AN386,
    FPGA_AN500,
    FPGA_AN511,
} MPS2FPGAType;

struct MPS2MachineClass {
    MachineClass parent;
    MPS2FPGAType fpga_type;
    uint32_t scc_id;
    bool has_block_ram;
    hwaddr ethernet_base;
    hwaddr psram_base;
};

struct MPS2MachineState {
    MachineState parent;

    ARMv7MState armv7m;
    MemoryRegion ssram1;
    MemoryRegion ssram1_m;
    MemoryRegion ssram23;
    MemoryRegion ssram23_m;
    MemoryRegion blockram;
    MemoryRegion blockram_m1;
    MemoryRegion blockram_m2;
    MemoryRegion blockram_m3;
    MemoryRegion sram;
    /* FPGA APB subsystem */
    MPS2SCC scc;
    MPS2FPGAIO fpgaio;
    /* CMSDK APB subsystem */
    CMSDKAPBDualTimer dualtimer;
    CMSDKAPBWatchdog watchdog;
    CMSDKAPBTimer timer[2];
    Clock *sysclk;
};

#define TYPE_MPS2_MACHINE "mps2"
#define TYPE_MPS2_AN385_MACHINE MACHINE_TYPE_NAME("mps2-an385")
#define TYPE_MPS2_AN386_MACHINE MACHINE_TYPE_NAME("mps2-an386")
#define TYPE_MPS2_AN500_MACHINE MACHINE_TYPE_NAME("mps2-an500")
#define TYPE_MPS2_AN511_MACHINE MACHINE_TYPE_NAME("mps2-an511")

OBJECT_DECLARE_TYPE(MPS2MachineState, MPS2MachineClass, MPS2_MACHINE)

/* Main SYSCLK frequency in Hz */
#define SYSCLK_FRQ 25000000

/* Initialize the auxiliary RAM region @mr and map it into
 * the memory map at @base.
 */
static void make_ram(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/* Create an alias of an entire original MemoryRegion @orig
 * located at @base in the memory map.
 */
static void make_ram_alias(MemoryRegion *mr, const char *name,
                           MemoryRegion *orig, hwaddr base)
{
    memory_region_init_alias(mr, NULL, name, orig, 0,
                             memory_region_size(orig));
    memory_region_add_subregion(get_system_memory(), base, mr);
}

static void mps2_common_init(MachineState *machine)
{
    MPS2MachineState *mms = MPS2_MACHINE(machine);
    MPS2MachineClass *mmc = MPS2_MACHINE_GET_CLASS(machine);
    MemoryRegion *system_memory = get_system_memory();
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    DeviceState *armv7m, *sccdev;
    int i;

    if (strcmp(machine->cpu_type, mc->default_cpu_type) != 0) {
        error_report("This board can only be used with CPU %s",
                     mc->default_cpu_type);
        exit(1);
    }

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    /* This clock doesn't need migration because it is fixed-frequency */
    mms->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(mms->sysclk, SYSCLK_FRQ);

    /* The FPGA images have an odd combination of different RAMs,
     * because in hardware they are different implementations and
     * connected to different buses, giving varying performance/size
     * tradeoffs. For QEMU they're all just RAM, though. We arbitrarily
     * call the 16MB our "system memory", as it's the largest lump.
     *
     * AN385/AN386/AN511:
     *  0x21000000 .. 0x21ffffff : PSRAM (16MB)
     * AN385/AN386/AN500:
     *  0x00000000 .. 0x003fffff : ZBT SSRAM1
     *  0x00400000 .. 0x007fffff : mirror of ZBT SSRAM1
     *  0x20000000 .. 0x203fffff : ZBT SSRAM 2&3
     *  0x20400000 .. 0x207fffff : mirror of ZBT SSRAM 2&3
     * AN385/AN386 only:
     *  0x01000000 .. 0x01003fff : block RAM (16K)
     *  0x01004000 .. 0x01007fff : mirror of above
     *  0x01008000 .. 0x0100bfff : mirror of above
     *  0x0100c000 .. 0x0100ffff : mirror of above
     * AN511 only:
     *  0x00000000 .. 0x0003ffff : FPGA block RAM
     *  0x00400000 .. 0x007fffff : ZBT SSRAM1
     *  0x20000000 .. 0x2001ffff : SRAM
     *  0x20400000 .. 0x207fffff : ZBT SSRAM 2&3
     * AN500 only:
     *  0x60000000 .. 0x60ffffff : PSRAM (16MB)
     *
     * The AN385/AN386 has a feature where the lowest 16K can be mapped
     * either to the bottom of the ZBT SSRAM1 or to the block RAM.
     * This is of no use for QEMU so we don't implement it (as if
     * zbt_boot_ctrl is always zero).
     */
    memory_region_add_subregion(system_memory, mmc->psram_base, machine->ram);

    if (mmc->has_block_ram) {
        make_ram(&mms->blockram, "mps.blockram", 0x01000000, 0x4000);
        make_ram_alias(&mms->blockram_m1, "mps.blockram_m1",
                       &mms->blockram, 0x01004000);
        make_ram_alias(&mms->blockram_m2, "mps.blockram_m2",
                       &mms->blockram, 0x01008000);
        make_ram_alias(&mms->blockram_m3, "mps.blockram_m3",
                       &mms->blockram, 0x0100c000);
    }

    switch (mmc->fpga_type) {
    case FPGA_AN385:
    case FPGA_AN386:
    case FPGA_AN500:
        make_ram(&mms->ssram1, "mps.ssram1", 0x0, 0x400000);
        make_ram_alias(&mms->ssram1_m, "mps.ssram1_m", &mms->ssram1, 0x400000);
        make_ram(&mms->ssram23, "mps.ssram23", 0x20000000, 0x400000);
        make_ram_alias(&mms->ssram23_m, "mps.ssram23_m",
                       &mms->ssram23, 0x20400000);
        break;
    case FPGA_AN511:
        make_ram(&mms->blockram, "mps.blockram", 0x0, 0x40000);
        make_ram(&mms->ssram1, "mps.ssram1", 0x00400000, 0x00800000);
        make_ram(&mms->sram, "mps.sram", 0x20000000, 0x20000);
        make_ram(&mms->ssram23, "mps.ssram23", 0x20400000, 0x400000);
        break;
    default:
        g_assert_not_reached();
    }

    object_initialize_child(OBJECT(mms), "armv7m", &mms->armv7m, TYPE_ARMV7M);
    armv7m = DEVICE(&mms->armv7m);
    switch (mmc->fpga_type) {
    case FPGA_AN385:
    case FPGA_AN386:
    case FPGA_AN500:
        qdev_prop_set_uint32(armv7m, "num-irq", 32);
        break;
    case FPGA_AN511:
        qdev_prop_set_uint32(armv7m, "num-irq", 64);
        break;
    default:
        g_assert_not_reached();
    }
    qdev_prop_set_string(armv7m, "cpu-type", machine->cpu_type);
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    object_property_set_link(OBJECT(&mms->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&mms->armv7m), &error_fatal);

    create_unimplemented_device("zbtsmram mirror", 0x00400000, 0x00400000);
    create_unimplemented_device("RESERVED 1", 0x00800000, 0x00800000);
    create_unimplemented_device("Block RAM", 0x01000000, 0x00010000);
    create_unimplemented_device("RESERVED 2", 0x01010000, 0x1EFF0000);
    create_unimplemented_device("RESERVED 3", 0x20800000, 0x00800000);
    create_unimplemented_device("PSRAM", 0x21000000, 0x01000000);
    /* These three ranges all cover multiple devices; we may implement
     * some of them below (in which case the real device takes precedence
     * over the unimplemented-region mapping).
     */
    create_unimplemented_device("CMSDK APB peripheral region @0x40000000",
                                0x40000000, 0x00010000);
    create_unimplemented_device("CMSDK AHB peripheral region @0x40010000",
                                0x40010000, 0x00010000);
    create_unimplemented_device("Extra peripheral region @0x40020000",
                                0x40020000, 0x00010000);

    create_unimplemented_device("RESERVED 4", 0x40030000, 0x001D0000);
    create_unimplemented_device("VGA", 0x41000000, 0x0200000);

    switch (mmc->fpga_type) {
    case FPGA_AN385:
    case FPGA_AN386:
    case FPGA_AN500:
    {
        /* The overflow IRQs for UARTs 0, 1 and 2 are ORed together.
         * Overflow for UARTs 4 and 5 doesn't trigger any interrupt.
         */
        Object *orgate;
        DeviceState *orgate_dev;

        orgate = object_new(TYPE_OR_IRQ);
        object_property_set_int(orgate, "num-lines", 6, &error_fatal);
        qdev_realize(DEVICE(orgate), NULL, &error_fatal);
        orgate_dev = DEVICE(orgate);
        qdev_connect_gpio_out(orgate_dev, 0, qdev_get_gpio_in(armv7m, 12));

        for (i = 0; i < 5; i++) {
            static const hwaddr uartbase[] = {0x40004000, 0x40005000,
                                              0x40006000, 0x40007000,
                                              0x40009000};
            /* RX irq number; TX irq is always one greater */
            static const int uartirq[] = {0, 2, 4, 18, 20};
            qemu_irq txovrint = NULL, rxovrint = NULL;

            if (i < 3) {
                txovrint = qdev_get_gpio_in(orgate_dev, i * 2);
                rxovrint = qdev_get_gpio_in(orgate_dev, i * 2 + 1);
            }

            cmsdk_apb_uart_create(uartbase[i],
                                  qdev_get_gpio_in(armv7m, uartirq[i] + 1),
                                  qdev_get_gpio_in(armv7m, uartirq[i]),
                                  txovrint, rxovrint,
                                  NULL,
                                  serial_hd(i), SYSCLK_FRQ);
        }
        break;
    }
    case FPGA_AN511:
    {
        /* The overflow IRQs for all UARTs are ORed together.
         * Tx and Rx IRQs for each UART are ORed together.
         */
        Object *orgate;
        DeviceState *orgate_dev;

        orgate = object_new(TYPE_OR_IRQ);
        object_property_set_int(orgate, "num-lines", 10, &error_fatal);
        qdev_realize(DEVICE(orgate), NULL, &error_fatal);
        orgate_dev = DEVICE(orgate);
        qdev_connect_gpio_out(orgate_dev, 0, qdev_get_gpio_in(armv7m, 12));

        for (i = 0; i < 5; i++) {
            /* system irq numbers for the combined tx/rx for each UART */
            static const int uart_txrx_irqno[] = {0, 2, 45, 46, 56};
            static const hwaddr uartbase[] = {0x40004000, 0x40005000,
                                              0x4002c000, 0x4002d000,
                                              0x4002e000};
            Object *txrx_orgate;
            DeviceState *txrx_orgate_dev;

            txrx_orgate = object_new(TYPE_OR_IRQ);
            object_property_set_int(txrx_orgate, "num-lines", 2, &error_fatal);
            qdev_realize(DEVICE(txrx_orgate), NULL, &error_fatal);
            txrx_orgate_dev = DEVICE(txrx_orgate);
            qdev_connect_gpio_out(txrx_orgate_dev, 0,
                                  qdev_get_gpio_in(armv7m, uart_txrx_irqno[i]));
            cmsdk_apb_uart_create(uartbase[i],
                                  qdev_get_gpio_in(txrx_orgate_dev, 0),
                                  qdev_get_gpio_in(txrx_orgate_dev, 1),
                                  qdev_get_gpio_in(orgate_dev, i * 2),
                                  qdev_get_gpio_in(orgate_dev, i * 2 + 1),
                                  NULL,
                                  serial_hd(i), SYSCLK_FRQ);
        }
        break;
    }
    default:
        g_assert_not_reached();
    }
    for (i = 0; i < 4; i++) {
        static const hwaddr gpiobase[] = {0x40010000, 0x40011000,
                                          0x40012000, 0x40013000};
        create_unimplemented_device("cmsdk-ahb-gpio", gpiobase[i], 0x1000);
    }

    /* CMSDK APB subsystem */
    for (i = 0; i < ARRAY_SIZE(mms->timer); i++) {
        g_autofree char *name = g_strdup_printf("timer%d", i);
        hwaddr base = 0x40000000 + i * 0x1000;
        int irqno = 8 + i;
        SysBusDevice *sbd;

        object_initialize_child(OBJECT(mms), name, &mms->timer[i],
                                TYPE_CMSDK_APB_TIMER);
        sbd = SYS_BUS_DEVICE(&mms->timer[i]);
        qdev_connect_clock_in(DEVICE(&mms->timer[i]), "pclk", mms->sysclk);
        sysbus_realize_and_unref(sbd, &error_fatal);
        sysbus_mmio_map(sbd, 0, base);
        sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(armv7m, irqno));
    }

    object_initialize_child(OBJECT(mms), "dualtimer", &mms->dualtimer,
                            TYPE_CMSDK_APB_DUALTIMER);
    qdev_connect_clock_in(DEVICE(&mms->dualtimer), "TIMCLK", mms->sysclk);
    sysbus_realize(SYS_BUS_DEVICE(&mms->dualtimer), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(&mms->dualtimer), 0,
                       qdev_get_gpio_in(armv7m, 10));
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->dualtimer), 0, 0x40002000);
    object_initialize_child(OBJECT(mms), "watchdog", &mms->watchdog,
                            TYPE_CMSDK_APB_WATCHDOG);
    qdev_connect_clock_in(DEVICE(&mms->watchdog), "WDOGCLK", mms->sysclk);
    sysbus_realize(SYS_BUS_DEVICE(&mms->watchdog), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(&mms->watchdog), 0,
                       qdev_get_gpio_in_named(armv7m, "NMI", 0));
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->watchdog), 0, 0x40008000);

    /* FPGA APB subsystem */
    object_initialize_child(OBJECT(mms), "scc", &mms->scc, TYPE_MPS2_SCC);
    sccdev = DEVICE(&mms->scc);
    qdev_prop_set_uint32(sccdev, "scc-cfg4", 0x2);
    qdev_prop_set_uint32(sccdev, "scc-aid", 0x00200008);
    qdev_prop_set_uint32(sccdev, "scc-id", mmc->scc_id);
    sysbus_realize(SYS_BUS_DEVICE(&mms->scc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(sccdev), 0, 0x4002f000);
    object_initialize_child(OBJECT(mms), "fpgaio",
                            &mms->fpgaio, TYPE_MPS2_FPGAIO);
    qdev_prop_set_uint32(DEVICE(&mms->fpgaio), "prescale-clk", 25000000);
    sysbus_realize(SYS_BUS_DEVICE(&mms->fpgaio), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->fpgaio), 0, 0x40028000);
    sysbus_create_simple(TYPE_PL022, 0x40025000,        /* External ADC */
                         qdev_get_gpio_in(armv7m, 22));
    for (i = 0; i < 2; i++) {
        static const int spi_irqno[] = {11, 24};
        static const hwaddr spibase[] = {0x40020000,    /* APB */
                                         0x40021000,    /* LCD */
                                         0x40026000,    /* Shield0 */
                                         0x40027000};   /* Shield1 */
        DeviceState *orgate_dev;
        Object *orgate;
        int j;

        orgate = object_new(TYPE_OR_IRQ);
        object_property_set_int(orgate, "num-lines", 2, &error_fatal);
        orgate_dev = DEVICE(orgate);
        qdev_realize(orgate_dev, NULL, &error_fatal);
        qdev_connect_gpio_out(orgate_dev, 0,
                              qdev_get_gpio_in(armv7m, spi_irqno[i]));
        for (j = 0; j < 2; j++) {
            sysbus_create_simple(TYPE_PL022, spibase[2 * i + j],
                                 qdev_get_gpio_in(orgate_dev, j));
        }
    }
    for (i = 0; i < 4; i++) {
        static const hwaddr i2cbase[] = {0x40022000,    /* Touch */
                                         0x40023000,    /* Audio */
                                         0x40029000,    /* Shield0 */
                                         0x4002a000};   /* Shield1 */
        sysbus_create_simple(TYPE_ARM_SBCON_I2C, i2cbase[i], NULL);
    }
    create_unimplemented_device("i2s", 0x40024000, 0x400);

    /* In hardware this is a LAN9220; the LAN9118 is software compatible
     * except that it doesn't support the checksum-offload feature.
     */
    lan9118_init(&nd_table[0], mmc->ethernet_base,
                 qdev_get_gpio_in(armv7m,
                                  mmc->fpga_type == FPGA_AN511 ? 47 : 13));

    system_clock_scale = NANOSECONDS_PER_SECOND / SYSCLK_FRQ;

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       0x400000);
}

static void mps2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = mps2_common_init;
    mc->max_cpus = 1;
    mc->default_ram_size = 16 * MiB;
    mc->default_ram_id = "mps.ram";
}

static void mps2_an385_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2MachineClass *mmc = MPS2_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS2 with AN385 FPGA image for Cortex-M3";
    mmc->fpga_type = FPGA_AN385;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m3");
    mmc->scc_id = 0x41043850;
    mmc->psram_base = 0x21000000;
    mmc->ethernet_base = 0x40200000;
    mmc->has_block_ram = true;
}

static void mps2_an386_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2MachineClass *mmc = MPS2_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS2 with AN386 FPGA image for Cortex-M4";
    mmc->fpga_type = FPGA_AN386;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m4");
    mmc->scc_id = 0x41043860;
    mmc->psram_base = 0x21000000;
    mmc->ethernet_base = 0x40200000;
    mmc->has_block_ram = true;
}

static void mps2_an500_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2MachineClass *mmc = MPS2_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS2 with AN500 FPGA image for Cortex-M7";
    mmc->fpga_type = FPGA_AN500;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m7");
    mmc->scc_id = 0x41045000;
    mmc->psram_base = 0x60000000;
    mmc->ethernet_base = 0xa0000000;
    mmc->has_block_ram = false;
}

static void mps2_an511_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2MachineClass *mmc = MPS2_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS2 with AN511 DesignStart FPGA image for Cortex-M3";
    mmc->fpga_type = FPGA_AN511;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m3");
    mmc->scc_id = 0x41045110;
    mmc->psram_base = 0x21000000;
    mmc->ethernet_base = 0x40200000;
    mmc->has_block_ram = false;
}

static const TypeInfo mps2_info = {
    .name = TYPE_MPS2_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(MPS2MachineState),
    .class_size = sizeof(MPS2MachineClass),
    .class_init = mps2_class_init,
};

static const TypeInfo mps2_an385_info = {
    .name = TYPE_MPS2_AN385_MACHINE,
    .parent = TYPE_MPS2_MACHINE,
    .class_init = mps2_an385_class_init,
};

static const TypeInfo mps2_an386_info = {
    .name = TYPE_MPS2_AN386_MACHINE,
    .parent = TYPE_MPS2_MACHINE,
    .class_init = mps2_an386_class_init,
};

static const TypeInfo mps2_an500_info = {
    .name = TYPE_MPS2_AN500_MACHINE,
    .parent = TYPE_MPS2_MACHINE,
    .class_init = mps2_an500_class_init,
};

static const TypeInfo mps2_an511_info = {
    .name = TYPE_MPS2_AN511_MACHINE,
    .parent = TYPE_MPS2_MACHINE,
    .class_init = mps2_an511_class_init,
};

static void mps2_machine_init(void)
{
    type_register_static(&mps2_info);
    type_register_static(&mps2_an385_info);
    type_register_static(&mps2_an386_info);
    type_register_static(&mps2_an500_info);
    type_register_static(&mps2_an511_info);
}

type_init(mps2_machine_init);
