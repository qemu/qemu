/*
 * Arm MPS3 board emulation for Cortex-R-based FPGA images.
 * (For M-profile images see mps2.c and mps2tz.c.)
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * The MPS3 is an FPGA based dev board. This file handles FPGA images
 * which use the Cortex-R CPUs. We model these separately from the
 * M-profile images, because on M-profile the FPGA image is based on
 * a "Subsystem for Embedded" which is similar to an SoC, whereas
 * the R-profile FPGA images don't have that abstraction layer.
 *
 * We model the following FPGA images here:
 *  "mps3-an536" -- dual Cortex-R52 as documented in Arm Application Note AN536
 *
 * Application Note AN536:
 * https://developer.arm.com/documentation/dai0536/latest/
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qobject/qlist.h"
#include "system/address-spaces.h"
#include "cpu.h"
#include "system/system.h"
#include "hw/boards.h"
#include "hw/or-irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/char/cmsdk-apb-uart.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/misc/mps2-scc.h"
#include "hw/misc/mps2-fpgaio.h"
#include "hw/misc/unimp.h"
#include "hw/net/lan9118.h"
#include "hw/rtc/pl031.h"
#include "hw/ssi/pl022.h"
#include "hw/timer/cmsdk-apb-dualtimer.h"
#include "hw/watchdog/cmsdk-apb-watchdog.h"

/* Define the layout of RAM and ROM in a board */
typedef struct RAMInfo {
    const char *name;
    hwaddr base;
    hwaddr size;
    int mrindex; /* index into rams[]; -1 for the system RAM block */
    int flags;
} RAMInfo;

/*
 * The MPS3 DDR is 3GiB, but on a 32-bit host QEMU doesn't permit
 * emulation of that much guest RAM, so artificially make it smaller.
 */
#if HOST_LONG_BITS == 32
#define MPS3_DDR_SIZE (1 * GiB)
#else
#define MPS3_DDR_SIZE (3 * GiB)
#endif

/*
 * Flag values:
 * IS_MAIN: this is the main machine RAM
 * IS_ROM: this area is read-only
 */
#define IS_MAIN 1
#define IS_ROM 2

#define MPS3R_RAM_MAX 9
#define MPS3R_CPU_MAX 2
#define MPS3R_UART_MAX 4 /* shared UART count */

#define PERIPHBASE 0xf0000000
#define NUM_SPIS 96

typedef enum MPS3RFPGAType {
    FPGA_AN536,
} MPS3RFPGAType;

struct MPS3RMachineClass {
    MachineClass parent;
    MPS3RFPGAType fpga_type;
    const RAMInfo *raminfo;
    hwaddr loader_start;
};

struct MPS3RMachineState {
    MachineState parent;
    struct arm_boot_info bootinfo;
    MemoryRegion ram[MPS3R_RAM_MAX];
    Object *cpu[MPS3R_CPU_MAX];
    MemoryRegion cpu_sysmem[MPS3R_CPU_MAX];
    MemoryRegion sysmem_alias[MPS3R_CPU_MAX];
    MemoryRegion cpu_ram[MPS3R_CPU_MAX];
    GICv3State gic;
    /* per-CPU UARTs followed by the shared UARTs */
    CMSDKAPBUART uart[MPS3R_CPU_MAX + MPS3R_UART_MAX];
    OrIRQState cpu_uart_oflow[MPS3R_CPU_MAX];
    OrIRQState uart_oflow;
    CMSDKAPBWatchdog watchdog;
    CMSDKAPBDualTimer dualtimer;
    ArmSbconI2CState i2c[5];
    PL022State spi[3];
    MPS2SCC scc;
    MPS2FPGAIO fpgaio;
    UnimplementedDeviceState i2s_audio;
    PL031State rtc;
    Clock *clk;
};

#define TYPE_MPS3R_MACHINE "mps3r"
#define TYPE_MPS3R_AN536_MACHINE MACHINE_TYPE_NAME("mps3-an536")

OBJECT_DECLARE_TYPE(MPS3RMachineState, MPS3RMachineClass, MPS3R_MACHINE)

/*
 * Main clock frequency CLK in Hz (50MHz). In the image there are also
 * ACLK, MCLK, GPUCLK and PERIPHCLK at the same frequency; for our
 * model we just roll them all into one.
 */
#define CLK_FRQ 50000000

static const RAMInfo an536_raminfo[] = {
    {
        .name = "ATCM",
        .base = 0x00000000,
        .size = 0x00008000,
        .mrindex = 0,
    }, {
        /* We model the QSPI flash as simple ROM for now */
        .name = "QSPI",
        .base = 0x08000000,
        .size = 0x00800000,
        .flags = IS_ROM,
        .mrindex = 1,
    }, {
        .name = "BRAM",
        .base = 0x10000000,
        .size = 0x00080000,
        .mrindex = 2,
    }, {
        .name = "DDR",
        .base = 0x20000000,
        .size = MPS3_DDR_SIZE,
        .mrindex = -1,
    }, {
        .name = "ATCM0",
        .base = 0xee000000,
        .size = 0x00008000,
        .mrindex = 3,
    }, {
        .name = "BTCM0",
        .base = 0xee100000,
        .size = 0x00008000,
        .mrindex = 4,
    }, {
        .name = "CTCM0",
        .base = 0xee200000,
        .size = 0x00008000,
        .mrindex = 5,
    }, {
        .name = "ATCM1",
        .base = 0xee400000,
        .size = 0x00008000,
        .mrindex = 6,
    }, {
        .name = "BTCM1",
        .base = 0xee500000,
        .size = 0x00008000,
        .mrindex = 7,
    }, {
        .name = "CTCM1",
        .base = 0xee600000,
        .size = 0x00008000,
        .mrindex = 8,
    }, {
        .name = NULL,
    }
};

static const int an536_oscclk[] = {
    24000000, /* 24MHz reference for RTC and timers */
    50000000, /* 50MHz ACLK */
    50000000, /* 50MHz MCLK */
    50000000, /* 50MHz GPUCLK */
    24576000, /* 24.576MHz AUDCLK */
    23750000, /* 23.75MHz HDLCDCLK */
    100000000, /* 100MHz DDR4_REF_CLK */
};

static MemoryRegion *mr_for_raminfo(MPS3RMachineState *mms,
                                    const RAMInfo *raminfo)
{
    /* Return an initialized MemoryRegion for the RAMInfo. */
    MemoryRegion *ram;

    if (raminfo->mrindex < 0) {
        /* Means this RAMInfo is for QEMU's "system memory" */
        MachineState *machine = MACHINE(mms);
        assert(!(raminfo->flags & IS_ROM));
        return machine->ram;
    }

    assert(raminfo->mrindex < MPS3R_RAM_MAX);
    ram = &mms->ram[raminfo->mrindex];

    memory_region_init_ram(ram, NULL, raminfo->name,
                           raminfo->size, &error_fatal);
    if (raminfo->flags & IS_ROM) {
        memory_region_set_readonly(ram, true);
    }
    return ram;
}

/*
 * There is no defined secondary boot protocol for Linux for the AN536,
 * because real hardware has a restriction that atomic operations between
 * the two CPUs do not function correctly, and so true SMP is not
 * possible. Therefore for cases where the user is directly booting
 * a kernel, we treat the system as essentially uniprocessor, and
 * put the secondary CPU into power-off state (as if the user on the
 * real hardware had configured the secondary to be halted via the
 * SCC config registers).
 *
 * Note that the default secondary boot code would not work here anyway
 * as it assumes a GICv2, and we have a GICv3.
 */
static void mps3r_write_secondary_boot(ARMCPU *cpu,
                                       const struct arm_boot_info *info)
{
    /*
     * Power the secondary CPU off. This means we don't need to write any
     * boot code into guest memory. Note that the 'cpu' argument to this
     * function is the primary CPU we passed to arm_load_kernel(), not
     * the secondary. Loop around all the other CPUs, as the boot.c
     * code does for the "disable secondaries if PSCI is enabled" case.
     */
    for (CPUState *cs = first_cpu; cs; cs = CPU_NEXT(cs)) {
        if (cs != first_cpu) {
            object_property_set_bool(OBJECT(cs), "start-powered-off", true,
                                     &error_abort);
        }
    }
}

static void mps3r_secondary_cpu_reset(ARMCPU *cpu,
                                      const struct arm_boot_info *info)
{
    /* We don't need to do anything here because the CPU will be off */
}

static void create_gic(MPS3RMachineState *mms, MemoryRegion *sysmem)
{
    MachineState *machine = MACHINE(mms);
    DeviceState *gicdev;
    QList *redist_region_count;

    object_initialize_child(OBJECT(mms), "gic", &mms->gic, TYPE_ARM_GICV3);
    gicdev = DEVICE(&mms->gic);
    qdev_prop_set_uint32(gicdev, "num-cpu", machine->smp.cpus);
    qdev_prop_set_uint32(gicdev, "num-irq", NUM_SPIS + GIC_INTERNAL);
    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, machine->smp.cpus);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);
    object_property_set_link(OBJECT(&mms->gic), "sysmem",
                             OBJECT(sysmem), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&mms->gic), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->gic), 0, PERIPHBASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->gic), 1, PERIPHBASE + 0x100000);
    /*
     * Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (int i = 0; i < machine->smp.cpus; i++) {
        DeviceState *cpudev = DEVICE(mms->cpu[i]);
        SysBusDevice *gicsbd = SYS_BUS_DEVICE(&mms->gic);
        int intidbase = NUM_SPIS + i * GIC_INTERNAL;
        int irq;
        /*
         * Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs used for this board. This isn't a BSA board,
         * but it uses the standard convention for the PPI numbers.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(gicdev,
                                                   intidbase + timer_irq[irq]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(gicdev,
                                                     intidbase + ARCH_GIC_MAINT_IRQ));

        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(gicdev,
                                                     intidbase + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicsbd, i,
                           qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicsbd, i + machine->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicsbd, i + 2 * machine->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicsbd, i + 3 * machine->smp.cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }
}

/*
 * Create UART uartno, and map it into the MemoryRegion mem at address baseaddr.
 * The qemu_irq arguments are where we connect the various IRQs from the UART.
 */
static void create_uart(MPS3RMachineState *mms, int uartno, MemoryRegion *mem,
                        hwaddr baseaddr, qemu_irq txirq, qemu_irq rxirq,
                        qemu_irq txoverirq, qemu_irq rxoverirq,
                        qemu_irq combirq)
{
    g_autofree char *s = g_strdup_printf("uart%d", uartno);
    SysBusDevice *sbd;

    assert(uartno < ARRAY_SIZE(mms->uart));
    object_initialize_child(OBJECT(mms), s, &mms->uart[uartno],
                            TYPE_CMSDK_APB_UART);
    qdev_prop_set_uint32(DEVICE(&mms->uart[uartno]), "pclk-frq", CLK_FRQ);
    qdev_prop_set_chr(DEVICE(&mms->uart[uartno]), "chardev", serial_hd(uartno));
    sbd = SYS_BUS_DEVICE(&mms->uart[uartno]);
    sysbus_realize(sbd, &error_fatal);
    memory_region_add_subregion(mem, baseaddr,
                                sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0, txirq);
    sysbus_connect_irq(sbd, 1, rxirq);
    sysbus_connect_irq(sbd, 2, txoverirq);
    sysbus_connect_irq(sbd, 3, rxoverirq);
    sysbus_connect_irq(sbd, 4, combirq);
}

static void mps3r_common_init(MachineState *machine)
{
    MPS3RMachineState *mms = MPS3R_MACHINE(machine);
    MPS3RMachineClass *mmc = MPS3R_MACHINE_GET_CLASS(mms);
    MemoryRegion *sysmem = get_system_memory();
    DeviceState *gicdev;
    QList *oscclk;

    mms->clk = clock_new(OBJECT(machine), "CLK");
    clock_set_hz(mms->clk, CLK_FRQ);

    for (const RAMInfo *ri = mmc->raminfo; ri->name; ri++) {
        MemoryRegion *mr = mr_for_raminfo(mms, ri);
        memory_region_add_subregion(sysmem, ri->base, mr);
    }

    assert(machine->smp.cpus <= MPS3R_CPU_MAX);
    for (int i = 0; i < machine->smp.cpus; i++) {
        g_autofree char *sysmem_name = g_strdup_printf("cpu-%d-memory", i);
        g_autofree char *ramname = g_strdup_printf("cpu-%d-memory", i);
        g_autofree char *alias_name = g_strdup_printf("sysmem-alias-%d", i);

        /*
         * Each CPU has some private RAM/peripherals, so create the container
         * which will house those, with the whole-machine system memory being
         * used where there's no CPU-specific device. Note that we need the
         * sysmem_alias aliases because we can't put one MR (the original
         * 'sysmem') into more than one other MR.
         */
        memory_region_init(&mms->cpu_sysmem[i], OBJECT(machine),
                           sysmem_name, UINT64_MAX);
        memory_region_init_alias(&mms->sysmem_alias[i], OBJECT(machine),
                                 alias_name, sysmem, 0, UINT64_MAX);
        memory_region_add_subregion_overlap(&mms->cpu_sysmem[i], 0,
                                            &mms->sysmem_alias[i], -1);

        mms->cpu[i] = object_new(machine->cpu_type);
        object_property_set_link(mms->cpu[i], "memory",
                                 OBJECT(&mms->cpu_sysmem[i]), &error_abort);
        object_property_set_int(mms->cpu[i], "reset-cbar",
                                PERIPHBASE, &error_abort);
        qdev_realize(DEVICE(mms->cpu[i]), NULL, &error_fatal);
        object_unref(mms->cpu[i]);

        /* Per-CPU RAM */
        memory_region_init_ram(&mms->cpu_ram[i], NULL, ramname,
                               0x1000, &error_fatal);
        memory_region_add_subregion(&mms->cpu_sysmem[i], 0xe7c01000,
                                    &mms->cpu_ram[i]);
    }

    create_gic(mms, sysmem);
    gicdev = DEVICE(&mms->gic);

    /*
     * UARTs 0 and 1 are per-CPU; their interrupts are wired to
     * the relevant CPU's PPI 0..3, aka INTID 16..19
     */
    for (int i = 0; i < machine->smp.cpus; i++) {
        int intidbase = NUM_SPIS + i * GIC_INTERNAL;
        g_autofree char *s = g_strdup_printf("cpu-uart-oflow-orgate%d", i);
        DeviceState *orgate;

        /* The two overflow IRQs from the UART are ORed together into PPI 3 */
        object_initialize_child(OBJECT(mms), s, &mms->cpu_uart_oflow[i],
                                TYPE_OR_IRQ);
        orgate = DEVICE(&mms->cpu_uart_oflow[i]);
        qdev_prop_set_uint32(orgate, "num-lines", 2);
        qdev_realize(orgate, NULL, &error_fatal);
        qdev_connect_gpio_out(orgate, 0,
                              qdev_get_gpio_in(gicdev, intidbase + 19));

        create_uart(mms, i, &mms->cpu_sysmem[i], 0xe7c00000,
                    qdev_get_gpio_in(gicdev, intidbase + 17), /* tx */
                    qdev_get_gpio_in(gicdev, intidbase + 16), /* rx */
                    qdev_get_gpio_in(orgate, 0), /* txover */
                    qdev_get_gpio_in(orgate, 1), /* rxover */
                    qdev_get_gpio_in(gicdev, intidbase + 18) /* combined */);
    }
    /*
     * UARTs 2 to 5 are whole-system; all overflow IRQs are ORed
     * together into IRQ 17
     */
    object_initialize_child(OBJECT(mms), "uart-oflow-orgate",
                            &mms->uart_oflow, TYPE_OR_IRQ);
    qdev_prop_set_uint32(DEVICE(&mms->uart_oflow), "num-lines",
                         MPS3R_UART_MAX * 2);
    qdev_realize(DEVICE(&mms->uart_oflow), NULL, &error_fatal);
    qdev_connect_gpio_out(DEVICE(&mms->uart_oflow), 0,
                          qdev_get_gpio_in(gicdev, 17));

    for (int i = 0; i < MPS3R_UART_MAX; i++) {
        hwaddr baseaddr = 0xe0205000 + i * 0x1000;
        int rxirq = 5 + i * 2, txirq = 6 + i * 2, combirq = 13 + i;

        create_uart(mms, i + MPS3R_CPU_MAX, sysmem, baseaddr,
                    qdev_get_gpio_in(gicdev, txirq),
                    qdev_get_gpio_in(gicdev, rxirq),
                    qdev_get_gpio_in(DEVICE(&mms->uart_oflow), i * 2),
                    qdev_get_gpio_in(DEVICE(&mms->uart_oflow), i * 2 + 1),
                    qdev_get_gpio_in(gicdev, combirq));
    }

    for (int i = 0; i < 4; i++) {
        /* CMSDK GPIO controllers */
        g_autofree char *s = g_strdup_printf("gpio%d", i);
        create_unimplemented_device(s, 0xe0000000 + i * 0x1000, 0x1000);
    }

    object_initialize_child(OBJECT(mms), "watchdog", &mms->watchdog,
                            TYPE_CMSDK_APB_WATCHDOG);
    qdev_connect_clock_in(DEVICE(&mms->watchdog), "WDOGCLK", mms->clk);
    sysbus_realize(SYS_BUS_DEVICE(&mms->watchdog), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(&mms->watchdog), 0,
                       qdev_get_gpio_in(gicdev, 0));
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->watchdog), 0, 0xe0100000);

    object_initialize_child(OBJECT(mms), "dualtimer", &mms->dualtimer,
                            TYPE_CMSDK_APB_DUALTIMER);
    qdev_connect_clock_in(DEVICE(&mms->dualtimer), "TIMCLK", mms->clk);
    sysbus_realize(SYS_BUS_DEVICE(&mms->dualtimer), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(&mms->dualtimer), 0,
                       qdev_get_gpio_in(gicdev, 3));
    sysbus_connect_irq(SYS_BUS_DEVICE(&mms->dualtimer), 1,
                       qdev_get_gpio_in(gicdev, 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&mms->dualtimer), 2,
                       qdev_get_gpio_in(gicdev, 2));
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->dualtimer), 0, 0xe0101000);

    for (int i = 0; i < ARRAY_SIZE(mms->i2c); i++) {
        static const hwaddr i2cbase[] = {0xe0102000,    /* Touch */
                                         0xe0103000,    /* Audio */
                                         0xe0107000,    /* Shield0 */
                                         0xe0108000,    /* Shield1 */
                                         0xe0109000};   /* DDR4 EEPROM */
        g_autofree char *s = g_strdup_printf("i2c%d", i);

        object_initialize_child(OBJECT(mms), s, &mms->i2c[i],
                                TYPE_ARM_SBCON_I2C);
        sysbus_realize(SYS_BUS_DEVICE(&mms->i2c[i]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&mms->i2c[i]), 0, i2cbase[i]);
        if (i != 2 && i != 3) {
            /*
             * internal-only bus: mark it full to avoid user-created
             * i2c devices being plugged into it.
             */
            qbus_mark_full(qdev_get_child_bus(DEVICE(&mms->i2c[i]), "i2c"));
        }
    }

    for (int i = 0; i < ARRAY_SIZE(mms->spi); i++) {
        g_autofree char *s = g_strdup_printf("spi%d", i);
        hwaddr baseaddr = 0xe0104000 + i * 0x1000;

        object_initialize_child(OBJECT(mms), s, &mms->spi[i], TYPE_PL022);
        sysbus_realize(SYS_BUS_DEVICE(&mms->spi[i]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&mms->spi[i]), 0, baseaddr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&mms->spi[i]), 0,
                           qdev_get_gpio_in(gicdev, 22 + i));
    }

    object_initialize_child(OBJECT(mms), "scc", &mms->scc, TYPE_MPS2_SCC);
    qdev_prop_set_uint32(DEVICE(&mms->scc), "scc-cfg0", 0);
    qdev_prop_set_uint32(DEVICE(&mms->scc), "scc-cfg4", 0x2);
    qdev_prop_set_uint32(DEVICE(&mms->scc), "scc-aid", 0x00200008);
    qdev_prop_set_uint32(DEVICE(&mms->scc), "scc-id", 0x41055360);
    oscclk = qlist_new();
    for (int i = 0; i < ARRAY_SIZE(an536_oscclk); i++) {
        qlist_append_int(oscclk, an536_oscclk[i]);
    }
    qdev_prop_set_array(DEVICE(&mms->scc), "oscclk", oscclk);
    sysbus_realize(SYS_BUS_DEVICE(&mms->scc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->scc), 0, 0xe0200000);

    create_unimplemented_device("i2s-audio", 0xe0201000, 0x1000);

    object_initialize_child(OBJECT(mms), "fpgaio", &mms->fpgaio,
                            TYPE_MPS2_FPGAIO);
    qdev_prop_set_uint32(DEVICE(&mms->fpgaio), "prescale-clk", an536_oscclk[1]);
    qdev_prop_set_uint32(DEVICE(&mms->fpgaio), "num-leds", 10);
    qdev_prop_set_bit(DEVICE(&mms->fpgaio), "has-switches", true);
    qdev_prop_set_bit(DEVICE(&mms->fpgaio), "has-dbgctrl", false);
    sysbus_realize(SYS_BUS_DEVICE(&mms->fpgaio), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->fpgaio), 0, 0xe0202000);

    create_unimplemented_device("clcd", 0xe0209000, 0x1000);

    object_initialize_child(OBJECT(mms), "rtc", &mms->rtc, TYPE_PL031);
    sysbus_realize(SYS_BUS_DEVICE(&mms->rtc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&mms->rtc), 0, 0xe020a000);
    sysbus_connect_irq(SYS_BUS_DEVICE(&mms->rtc), 0,
                       qdev_get_gpio_in(gicdev, 4));

    /*
     * In hardware this is a LAN9220; the LAN9118 is software compatible
     * except that it doesn't support the checksum-offload feature.
     */
    lan9118_init(0xe0300000,
                 qdev_get_gpio_in(gicdev, 18));

    create_unimplemented_device("usb", 0xe0301000, 0x1000);
    create_unimplemented_device("qspi-write-config", 0xe0600000, 0x1000);

    mms->bootinfo.ram_size = machine->ram_size;
    mms->bootinfo.board_id = -1;
    mms->bootinfo.loader_start = mmc->loader_start;
    mms->bootinfo.write_secondary_boot = mps3r_write_secondary_boot;
    mms->bootinfo.secondary_cpu_reset_hook = mps3r_secondary_cpu_reset;
    arm_load_kernel(ARM_CPU(mms->cpu[0]), machine, &mms->bootinfo);
}

static void mps3r_set_default_ram_info(MPS3RMachineClass *mmc)
{
    /*
     * Set mc->default_ram_size and default_ram_id from the
     * information in mmc->raminfo.
     */
    MachineClass *mc = MACHINE_CLASS(mmc);
    const RAMInfo *p;

    for (p = mmc->raminfo; p->name; p++) {
        if (p->mrindex < 0) {
            /* Found the entry for "system memory" */
            mc->default_ram_size = p->size;
            mc->default_ram_id = p->name;
            mmc->loader_start = p->base;
            return;
        }
    }
    g_assert_not_reached();
}

static void mps3r_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = mps3r_common_init;
}

static void mps3r_an536_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS3RMachineClass *mmc = MPS3R_MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-r52"),
        NULL
    };

    mc->desc = "ARM MPS3 with AN536 FPGA image for Cortex-R52";
    /*
     * In the real FPGA image there are always two cores, but the standard
     * initial setting for the SCC SYSCON 0x000 register is 0x21, meaning
     * that the second core is held in reset and halted. Many images built for
     * the board do not expect the second core to run at startup (especially
     * since on the real FPGA image it is not possible to use LDREX/STREX
     * in RAM between the two cores, so a true SMP setup isn't supported).
     *
     * As QEMU's equivalent of this, we support both -smp 1 and -smp 2,
     * with the default being -smp 1. This seems a more intuitive UI for
     * QEMU users than, for instance, having a machine property to allow
     * the user to set the initial value of the SYSCON 0x000 register.
     */
    mc->default_cpus = 1;
    mc->min_cpus = 1;
    mc->max_cpus = 2;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-r52");
    mc->valid_cpu_types = valid_cpu_types;
    mmc->raminfo = an536_raminfo;
    mps3r_set_default_ram_info(mmc);
}

static const TypeInfo mps3r_machine_types[] = {
    {
        .name = TYPE_MPS3R_MACHINE,
        .parent = TYPE_MACHINE,
        .abstract = true,
        .instance_size = sizeof(MPS3RMachineState),
        .class_size = sizeof(MPS3RMachineClass),
        .class_init = mps3r_class_init,
    }, {
        .name = TYPE_MPS3R_AN536_MACHINE,
        .parent = TYPE_MPS3R_MACHINE,
        .class_init = mps3r_an536_class_init,
    },
};

DEFINE_TYPES(mps3r_machine_types);
