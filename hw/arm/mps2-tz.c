/*
 * ARM V2M MPS2 board emulation, trustzone aware FPGA images
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
 * This source file covers the following FPGA images, for TrustZone cores:
 *  "mps2-an505" -- Cortex-M33 as documented in ARM Application Note AN505
 *  "mps2-an521" -- Dual Cortex-M33 as documented in Application Note AN521
 *
 * Links to the TRM for the board itself and to the various Application
 * Notes which document the FPGA images can be found here:
 * https://developer.arm.com/products/system-design/development-boards/fpga-prototyping-boards/mps2
 *
 * Board TRM:
 * http://infocenter.arm.com/help/topic/com.arm.doc.100112_0200_06_en/versatile_express_cortex_m_prototyping_systems_v2m_mps2_and_v2m_mps2plus_technical_reference_100112_0200_06_en.pdf
 * Application Note AN505:
 * http://infocenter.arm.com/help/topic/com.arm.doc.dai0505b/index.html
 * Application Note AN521:
 * http://infocenter.arm.com/help/topic/com.arm.doc.dai0521c/index.html
 *
 * The AN505 defers to the Cortex-M33 processor ARMv8M IoT Kit FVP User Guide
 * (ARM ECM0601256) for the details of some of the device layout:
 *   http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 * Similarly, the AN521 uses the SSE-200, and the SSE-200 TRM defines
 * most of the device layout:
 *  http://infocenter.arm.com/help/topic/com.arm.doc.101104_0100_00_en/corelink_sse200_subsystem_for_embedded_technical_reference_manual_101104_0100_00_en.pdf
 *
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
#include "hw/misc/mps2-scc.h"
#include "hw/misc/mps2-fpgaio.h"
#include "hw/misc/tz-mpc.h"
#include "hw/misc/tz-msc.h"
#include "hw/arm/armsse.h"
#include "hw/dma/pl080.h"
#include "hw/ssi/pl022.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/net/lan9118.h"
#include "net/net.h"
#include "hw/core/split-irq.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"

#define MPS2TZ_NUMIRQ 92

typedef enum MPS2TZFPGAType {
    FPGA_AN505,
    FPGA_AN521,
} MPS2TZFPGAType;

struct MPS2TZMachineClass {
    MachineClass parent;
    MPS2TZFPGAType fpga_type;
    uint32_t scc_id;
    uint32_t sysclk_frq; /* Main SYSCLK frequency in Hz */
    const char *armsse_type;
};

struct MPS2TZMachineState {
    MachineState parent;

    ARMSSE iotkit;
    MemoryRegion ssram[3];
    MemoryRegion ssram1_m;
    MPS2SCC scc;
    MPS2FPGAIO fpgaio;
    TZPPC ppc[5];
    TZMPC ssram_mpc[3];
    PL022State spi[5];
    ArmSbconI2CState i2c[4];
    UnimplementedDeviceState i2s_audio;
    UnimplementedDeviceState gpio[4];
    UnimplementedDeviceState gfx;
    PL080State dma[4];
    TZMSC msc[4];
    CMSDKAPBUART uart[5];
    SplitIRQ sec_resp_splitter;
    qemu_or_irq uart_irq_orgate;
    DeviceState *lan9118;
    SplitIRQ cpu_irq_splitter[MPS2TZ_NUMIRQ];
    Clock *sysclk;
    Clock *s32kclk;
};

#define TYPE_MPS2TZ_MACHINE "mps2tz"
#define TYPE_MPS2TZ_AN505_MACHINE MACHINE_TYPE_NAME("mps2-an505")
#define TYPE_MPS2TZ_AN521_MACHINE MACHINE_TYPE_NAME("mps2-an521")

OBJECT_DECLARE_TYPE(MPS2TZMachineState, MPS2TZMachineClass, MPS2TZ_MACHINE)

/* Slow 32Khz S32KCLK frequency in Hz */
#define S32KCLK_FRQ (32 * 1000)

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

static qemu_irq get_sse_irq_in(MPS2TZMachineState *mms, int irqno)
{
    /* Return a qemu_irq which will signal IRQ n to all CPUs in the SSE. */
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);

    assert(irqno < MPS2TZ_NUMIRQ);

    switch (mmc->fpga_type) {
    case FPGA_AN505:
        return qdev_get_gpio_in_named(DEVICE(&mms->iotkit), "EXP_IRQ", irqno);
    case FPGA_AN521:
        return qdev_get_gpio_in(DEVICE(&mms->cpu_irq_splitter[irqno]), 0);
    default:
        g_assert_not_reached();
    }
}

/* Most of the devices in the AN505 FPGA image sit behind
 * Peripheral Protection Controllers. These data structures
 * define the layout of which devices sit behind which PPCs.
 * The devfn for each port is a function which creates, configures
 * and initializes the device, returning the MemoryRegion which
 * needs to be plugged into the downstream end of the PPC port.
 */
typedef MemoryRegion *MakeDevFn(MPS2TZMachineState *mms, void *opaque,
                                const char *name, hwaddr size);

typedef struct PPCPortInfo {
    const char *name;
    MakeDevFn *devfn;
    void *opaque;
    hwaddr addr;
    hwaddr size;
} PPCPortInfo;

typedef struct PPCInfo {
    const char *name;
    PPCPortInfo ports[TZ_NUM_PORTS];
} PPCInfo;

static MemoryRegion *make_unimp_dev(MPS2TZMachineState *mms,
                                       void *opaque,
                                       const char *name, hwaddr size)
{
    /* Initialize, configure and realize a TYPE_UNIMPLEMENTED_DEVICE,
     * and return a pointer to its MemoryRegion.
     */
    UnimplementedDeviceState *uds = opaque;

    object_initialize_child(OBJECT(mms), name, uds, TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(DEVICE(uds), "name", name);
    qdev_prop_set_uint64(DEVICE(uds), "size", size);
    sysbus_realize(SYS_BUS_DEVICE(uds), &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(uds), 0);
}

static MemoryRegion *make_uart(MPS2TZMachineState *mms, void *opaque,
                               const char *name, hwaddr size)
{
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);
    CMSDKAPBUART *uart = opaque;
    int i = uart - &mms->uart[0];
    int rxirqno = i * 2;
    int txirqno = i * 2 + 1;
    int combirqno = i + 10;
    SysBusDevice *s;
    DeviceState *orgate_dev = DEVICE(&mms->uart_irq_orgate);

    object_initialize_child(OBJECT(mms), name, uart, TYPE_CMSDK_APB_UART);
    qdev_prop_set_chr(DEVICE(uart), "chardev", serial_hd(i));
    qdev_prop_set_uint32(DEVICE(uart), "pclk-frq", mmc->sysclk_frq);
    sysbus_realize(SYS_BUS_DEVICE(uart), &error_fatal);
    s = SYS_BUS_DEVICE(uart);
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, txirqno));
    sysbus_connect_irq(s, 1, get_sse_irq_in(mms, rxirqno));
    sysbus_connect_irq(s, 2, qdev_get_gpio_in(orgate_dev, i * 2));
    sysbus_connect_irq(s, 3, qdev_get_gpio_in(orgate_dev, i * 2 + 1));
    sysbus_connect_irq(s, 4, get_sse_irq_in(mms, combirqno));
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(uart), 0);
}

static MemoryRegion *make_scc(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    MPS2SCC *scc = opaque;
    DeviceState *sccdev;
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);

    object_initialize_child(OBJECT(mms), "scc", scc, TYPE_MPS2_SCC);
    sccdev = DEVICE(scc);
    qdev_prop_set_uint32(sccdev, "scc-cfg4", 0x2);
    qdev_prop_set_uint32(sccdev, "scc-aid", 0x00200008);
    qdev_prop_set_uint32(sccdev, "scc-id", mmc->scc_id);
    /* This will need to be per-FPGA image eventually */
    qdev_prop_set_uint32(sccdev, "len-oscclk", 3);
    qdev_prop_set_uint32(sccdev, "oscclk[0]", 50000000);
    qdev_prop_set_uint32(sccdev, "oscclk[1]", 24576000);
    qdev_prop_set_uint32(sccdev, "oscclk[2]", 25000000);
    sysbus_realize(SYS_BUS_DEVICE(scc), &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(sccdev), 0);
}

static MemoryRegion *make_fpgaio(MPS2TZMachineState *mms, void *opaque,
                                 const char *name, hwaddr size)
{
    MPS2FPGAIO *fpgaio = opaque;

    object_initialize_child(OBJECT(mms), "fpgaio", fpgaio, TYPE_MPS2_FPGAIO);
    sysbus_realize(SYS_BUS_DEVICE(fpgaio), &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(fpgaio), 0);
}

static MemoryRegion *make_eth_dev(MPS2TZMachineState *mms, void *opaque,
                                  const char *name, hwaddr size)
{
    SysBusDevice *s;
    NICInfo *nd = &nd_table[0];

    /* In hardware this is a LAN9220; the LAN9118 is software compatible
     * except that it doesn't support the checksum-offload feature.
     */
    qemu_check_nic_model(nd, "lan9118");
    mms->lan9118 = qdev_new(TYPE_LAN9118);
    qdev_set_nic_properties(mms->lan9118, nd);

    s = SYS_BUS_DEVICE(mms->lan9118);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, 16));
    return sysbus_mmio_get_region(s, 0);
}

static MemoryRegion *make_mpc(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    TZMPC *mpc = opaque;
    int i = mpc - &mms->ssram_mpc[0];
    MemoryRegion *ssram = &mms->ssram[i];
    MemoryRegion *upstream;
    char *mpcname = g_strdup_printf("%s-mpc", name);
    static uint32_t ramsize[] = { 0x00400000, 0x00200000, 0x00200000 };
    static uint32_t rambase[] = { 0x00000000, 0x28000000, 0x28200000 };

    memory_region_init_ram(ssram, NULL, name, ramsize[i], &error_fatal);

    object_initialize_child(OBJECT(mms), mpcname, mpc, TYPE_TZ_MPC);
    object_property_set_link(OBJECT(mpc), "downstream", OBJECT(ssram),
                             &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(mpc), &error_fatal);
    /* Map the upstream end of the MPC into system memory */
    upstream = sysbus_mmio_get_region(SYS_BUS_DEVICE(mpc), 1);
    memory_region_add_subregion(get_system_memory(), rambase[i], upstream);
    /* and connect its interrupt to the IoTKit */
    qdev_connect_gpio_out_named(DEVICE(mpc), "irq", 0,
                                qdev_get_gpio_in_named(DEVICE(&mms->iotkit),
                                                       "mpcexp_status", i));

    /* The first SSRAM is a special case as it has an alias; accesses to
     * the alias region at 0x00400000 must also go to the MPC upstream.
     */
    if (i == 0) {
        make_ram_alias(&mms->ssram1_m, "mps.ssram1_m", upstream, 0x00400000);
    }

    g_free(mpcname);
    /* Return the register interface MR for our caller to map behind the PPC */
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(mpc), 0);
}

static MemoryRegion *make_dma(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    PL080State *dma = opaque;
    int i = dma - &mms->dma[0];
    SysBusDevice *s;
    char *mscname = g_strdup_printf("%s-msc", name);
    TZMSC *msc = &mms->msc[i];
    DeviceState *iotkitdev = DEVICE(&mms->iotkit);
    MemoryRegion *msc_upstream;
    MemoryRegion *msc_downstream;

    /*
     * Each DMA device is a PL081 whose transaction master interface
     * is guarded by a Master Security Controller. The downstream end of
     * the MSC connects to the IoTKit AHB Slave Expansion port, so the
     * DMA devices can see all devices and memory that the CPU does.
     */
    object_initialize_child(OBJECT(mms), mscname, msc, TYPE_TZ_MSC);
    msc_downstream = sysbus_mmio_get_region(SYS_BUS_DEVICE(&mms->iotkit), 0);
    object_property_set_link(OBJECT(msc), "downstream",
                             OBJECT(msc_downstream), &error_fatal);
    object_property_set_link(OBJECT(msc), "idau", OBJECT(mms), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(msc), &error_fatal);

    qdev_connect_gpio_out_named(DEVICE(msc), "irq", 0,
                                qdev_get_gpio_in_named(iotkitdev,
                                                       "mscexp_status", i));
    qdev_connect_gpio_out_named(iotkitdev, "mscexp_clear", i,
                                qdev_get_gpio_in_named(DEVICE(msc),
                                                       "irq_clear", 0));
    qdev_connect_gpio_out_named(iotkitdev, "mscexp_ns", i,
                                qdev_get_gpio_in_named(DEVICE(msc),
                                                       "cfg_nonsec", 0));
    qdev_connect_gpio_out(DEVICE(&mms->sec_resp_splitter),
                          ARRAY_SIZE(mms->ppc) + i,
                          qdev_get_gpio_in_named(DEVICE(msc),
                                                 "cfg_sec_resp", 0));
    msc_upstream = sysbus_mmio_get_region(SYS_BUS_DEVICE(msc), 0);

    object_initialize_child(OBJECT(mms), name, dma, TYPE_PL081);
    object_property_set_link(OBJECT(dma), "downstream", OBJECT(msc_upstream),
                             &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(dma), &error_fatal);

    s = SYS_BUS_DEVICE(dma);
    /* Wire up DMACINTR, DMACINTERR, DMACINTTC */
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, 58 + i * 3));
    sysbus_connect_irq(s, 1, get_sse_irq_in(mms, 56 + i * 3));
    sysbus_connect_irq(s, 2, get_sse_irq_in(mms, 57 + i * 3));

    g_free(mscname);
    return sysbus_mmio_get_region(s, 0);
}

static MemoryRegion *make_spi(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    /*
     * The AN505 has five PL022 SPI controllers.
     * One of these should have the LCD controller behind it; the others
     * are connected only to the FPGA's "general purpose SPI connector"
     * or "shield" expansion connectors.
     * Note that if we do implement devices behind SPI, the chip select
     * lines are set via the "MISC" register in the MPS2 FPGAIO device.
     */
    PL022State *spi = opaque;
    int i = spi - &mms->spi[0];
    SysBusDevice *s;

    object_initialize_child(OBJECT(mms), name, spi, TYPE_PL022);
    sysbus_realize(SYS_BUS_DEVICE(spi), &error_fatal);
    s = SYS_BUS_DEVICE(spi);
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, 51 + i));
    return sysbus_mmio_get_region(s, 0);
}

static MemoryRegion *make_i2c(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    ArmSbconI2CState *i2c = opaque;
    SysBusDevice *s;

    object_initialize_child(OBJECT(mms), name, i2c, TYPE_ARM_SBCON_I2C);
    s = SYS_BUS_DEVICE(i2c);
    sysbus_realize(s, &error_fatal);
    return sysbus_mmio_get_region(s, 0);
}

static void mps2tz_common_init(MachineState *machine)
{
    MPS2TZMachineState *mms = MPS2TZ_MACHINE(machine);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *iotkitdev;
    DeviceState *dev_splitter;
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

    /* These clocks don't need migration because they are fixed-frequency */
    mms->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(mms->sysclk, mmc->sysclk_frq);
    mms->s32kclk = clock_new(OBJECT(machine), "S32KCLK");
    clock_set_hz(mms->s32kclk, S32KCLK_FRQ);

    object_initialize_child(OBJECT(machine), TYPE_IOTKIT, &mms->iotkit,
                            mmc->armsse_type);
    iotkitdev = DEVICE(&mms->iotkit);
    object_property_set_link(OBJECT(&mms->iotkit), "memory",
                             OBJECT(system_memory), &error_abort);
    qdev_prop_set_uint32(iotkitdev, "EXP_NUMIRQ", MPS2TZ_NUMIRQ);
    qdev_connect_clock_in(iotkitdev, "MAINCLK", mms->sysclk);
    qdev_connect_clock_in(iotkitdev, "S32KCLK", mms->s32kclk);
    sysbus_realize(SYS_BUS_DEVICE(&mms->iotkit), &error_fatal);

    /*
     * The AN521 needs us to create splitters to feed the IRQ inputs
     * for each CPU in the SSE-200 from each device in the board.
     */
    if (mmc->fpga_type == FPGA_AN521) {
        for (i = 0; i < MPS2TZ_NUMIRQ; i++) {
            char *name = g_strdup_printf("mps2-irq-splitter%d", i);
            SplitIRQ *splitter = &mms->cpu_irq_splitter[i];

            object_initialize_child_with_props(OBJECT(machine), name,
                                               splitter, sizeof(*splitter),
                                               TYPE_SPLIT_IRQ, &error_fatal,
                                               NULL);
            g_free(name);

            object_property_set_int(OBJECT(splitter), "num-lines", 2,
                                    &error_fatal);
            qdev_realize(DEVICE(splitter), NULL, &error_fatal);
            qdev_connect_gpio_out(DEVICE(splitter), 0,
                                  qdev_get_gpio_in_named(DEVICE(&mms->iotkit),
                                                         "EXP_IRQ", i));
            qdev_connect_gpio_out(DEVICE(splitter), 1,
                                  qdev_get_gpio_in_named(DEVICE(&mms->iotkit),
                                                         "EXP_CPU1_IRQ", i));
        }
    }

    /* The sec_resp_cfg output from the IoTKit must be split into multiple
     * lines, one for each of the PPCs we create here, plus one per MSC.
     */
    object_initialize_child(OBJECT(machine), "sec-resp-splitter",
                            &mms->sec_resp_splitter, TYPE_SPLIT_IRQ);
    object_property_set_int(OBJECT(&mms->sec_resp_splitter), "num-lines",
                            ARRAY_SIZE(mms->ppc) + ARRAY_SIZE(mms->msc),
                            &error_fatal);
    qdev_realize(DEVICE(&mms->sec_resp_splitter), NULL, &error_fatal);
    dev_splitter = DEVICE(&mms->sec_resp_splitter);
    qdev_connect_gpio_out_named(iotkitdev, "sec_resp_cfg", 0,
                                qdev_get_gpio_in(dev_splitter, 0));

    /* The IoTKit sets up much of the memory layout, including
     * the aliases between secure and non-secure regions in the
     * address space. The FPGA itself contains:
     *
     * 0x00000000..0x003fffff  SSRAM1
     * 0x00400000..0x007fffff  alias of SSRAM1
     * 0x28000000..0x283fffff  4MB SSRAM2 + SSRAM3
     * 0x40100000..0x4fffffff  AHB Master Expansion 1 interface devices
     * 0x80000000..0x80ffffff  16MB PSRAM
     */

    /* The FPGA images have an odd combination of different RAMs,
     * because in hardware they are different implementations and
     * connected to different buses, giving varying performance/size
     * tradeoffs. For QEMU they're all just RAM, though. We arbitrarily
     * call the 16MB our "system memory", as it's the largest lump.
     */
    memory_region_add_subregion(system_memory, 0x80000000, machine->ram);

    /* The overflow IRQs for all UARTs are ORed together.
     * Tx, Rx and "combined" IRQs are sent to the NVIC separately.
     * Create the OR gate for this.
     */
    object_initialize_child(OBJECT(mms), "uart-irq-orgate",
                            &mms->uart_irq_orgate, TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&mms->uart_irq_orgate), "num-lines", 10,
                            &error_fatal);
    qdev_realize(DEVICE(&mms->uart_irq_orgate), NULL, &error_fatal);
    qdev_connect_gpio_out(DEVICE(&mms->uart_irq_orgate), 0,
                          get_sse_irq_in(mms, 15));

    /* Most of the devices in the FPGA are behind Peripheral Protection
     * Controllers. The required order for initializing things is:
     *  + initialize the PPC
     *  + initialize, configure and realize downstream devices
     *  + connect downstream device MemoryRegions to the PPC
     *  + realize the PPC
     *  + map the PPC's MemoryRegions to the places in the address map
     *    where the downstream devices should appear
     *  + wire up the PPC's control lines to the IoTKit object
     */

    const PPCInfo ppcs[] = { {
            .name = "apb_ppcexp0",
            .ports = {
                { "ssram-0", make_mpc, &mms->ssram_mpc[0], 0x58007000, 0x1000 },
                { "ssram-1", make_mpc, &mms->ssram_mpc[1], 0x58008000, 0x1000 },
                { "ssram-2", make_mpc, &mms->ssram_mpc[2], 0x58009000, 0x1000 },
            },
        }, {
            .name = "apb_ppcexp1",
            .ports = {
                { "spi0", make_spi, &mms->spi[0], 0x40205000, 0x1000 },
                { "spi1", make_spi, &mms->spi[1], 0x40206000, 0x1000 },
                { "spi2", make_spi, &mms->spi[2], 0x40209000, 0x1000 },
                { "spi3", make_spi, &mms->spi[3], 0x4020a000, 0x1000 },
                { "spi4", make_spi, &mms->spi[4], 0x4020b000, 0x1000 },
                { "uart0", make_uart, &mms->uart[0], 0x40200000, 0x1000 },
                { "uart1", make_uart, &mms->uart[1], 0x40201000, 0x1000 },
                { "uart2", make_uart, &mms->uart[2], 0x40202000, 0x1000 },
                { "uart3", make_uart, &mms->uart[3], 0x40203000, 0x1000 },
                { "uart4", make_uart, &mms->uart[4], 0x40204000, 0x1000 },
                { "i2c0", make_i2c, &mms->i2c[0], 0x40207000, 0x1000 },
                { "i2c1", make_i2c, &mms->i2c[1], 0x40208000, 0x1000 },
                { "i2c2", make_i2c, &mms->i2c[2], 0x4020c000, 0x1000 },
                { "i2c3", make_i2c, &mms->i2c[3], 0x4020d000, 0x1000 },
            },
        }, {
            .name = "apb_ppcexp2",
            .ports = {
                { "scc", make_scc, &mms->scc, 0x40300000, 0x1000 },
                { "i2s-audio", make_unimp_dev, &mms->i2s_audio,
                  0x40301000, 0x1000 },
                { "fpgaio", make_fpgaio, &mms->fpgaio, 0x40302000, 0x1000 },
            },
        }, {
            .name = "ahb_ppcexp0",
            .ports = {
                { "gfx", make_unimp_dev, &mms->gfx, 0x41000000, 0x140000 },
                { "gpio0", make_unimp_dev, &mms->gpio[0], 0x40100000, 0x1000 },
                { "gpio1", make_unimp_dev, &mms->gpio[1], 0x40101000, 0x1000 },
                { "gpio2", make_unimp_dev, &mms->gpio[2], 0x40102000, 0x1000 },
                { "gpio3", make_unimp_dev, &mms->gpio[3], 0x40103000, 0x1000 },
                { "eth", make_eth_dev, NULL, 0x42000000, 0x100000 },
            },
        }, {
            .name = "ahb_ppcexp1",
            .ports = {
                { "dma0", make_dma, &mms->dma[0], 0x40110000, 0x1000 },
                { "dma1", make_dma, &mms->dma[1], 0x40111000, 0x1000 },
                { "dma2", make_dma, &mms->dma[2], 0x40112000, 0x1000 },
                { "dma3", make_dma, &mms->dma[3], 0x40113000, 0x1000 },
            },
        },
    };

    for (i = 0; i < ARRAY_SIZE(ppcs); i++) {
        const PPCInfo *ppcinfo = &ppcs[i];
        TZPPC *ppc = &mms->ppc[i];
        DeviceState *ppcdev;
        int port;
        char *gpioname;

        object_initialize_child(OBJECT(machine), ppcinfo->name, ppc,
                                TYPE_TZ_PPC);
        ppcdev = DEVICE(ppc);

        for (port = 0; port < TZ_NUM_PORTS; port++) {
            const PPCPortInfo *pinfo = &ppcinfo->ports[port];
            MemoryRegion *mr;
            char *portname;

            if (!pinfo->devfn) {
                continue;
            }

            mr = pinfo->devfn(mms, pinfo->opaque, pinfo->name, pinfo->size);
            portname = g_strdup_printf("port[%d]", port);
            object_property_set_link(OBJECT(ppc), portname, OBJECT(mr),
                                     &error_fatal);
            g_free(portname);
        }

        sysbus_realize(SYS_BUS_DEVICE(ppc), &error_fatal);

        for (port = 0; port < TZ_NUM_PORTS; port++) {
            const PPCPortInfo *pinfo = &ppcinfo->ports[port];

            if (!pinfo->devfn) {
                continue;
            }
            sysbus_mmio_map(SYS_BUS_DEVICE(ppc), port, pinfo->addr);

            gpioname = g_strdup_printf("%s_nonsec", ppcinfo->name);
            qdev_connect_gpio_out_named(iotkitdev, gpioname, port,
                                        qdev_get_gpio_in_named(ppcdev,
                                                               "cfg_nonsec",
                                                               port));
            g_free(gpioname);
            gpioname = g_strdup_printf("%s_ap", ppcinfo->name);
            qdev_connect_gpio_out_named(iotkitdev, gpioname, port,
                                        qdev_get_gpio_in_named(ppcdev,
                                                               "cfg_ap", port));
            g_free(gpioname);
        }

        gpioname = g_strdup_printf("%s_irq_enable", ppcinfo->name);
        qdev_connect_gpio_out_named(iotkitdev, gpioname, 0,
                                    qdev_get_gpio_in_named(ppcdev,
                                                           "irq_enable", 0));
        g_free(gpioname);
        gpioname = g_strdup_printf("%s_irq_clear", ppcinfo->name);
        qdev_connect_gpio_out_named(iotkitdev, gpioname, 0,
                                    qdev_get_gpio_in_named(ppcdev,
                                                           "irq_clear", 0));
        g_free(gpioname);
        gpioname = g_strdup_printf("%s_irq_status", ppcinfo->name);
        qdev_connect_gpio_out_named(ppcdev, "irq", 0,
                                    qdev_get_gpio_in_named(iotkitdev,
                                                           gpioname, 0));
        g_free(gpioname);

        qdev_connect_gpio_out(dev_splitter, i,
                              qdev_get_gpio_in_named(ppcdev,
                                                     "cfg_sec_resp", 0));
    }

    create_unimplemented_device("FPGA NS PC", 0x48007000, 0x1000);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename, 0x400000);
}

static void mps2_tz_idau_check(IDAUInterface *ii, uint32_t address,
                               int *iregion, bool *exempt, bool *ns, bool *nsc)
{
    /*
     * The MPS2 TZ FPGA images have IDAUs in them which are connected to
     * the Master Security Controllers. Thes have the same logic as
     * is used by the IoTKit for the IDAU connected to the CPU, except
     * that MSCs don't care about the NSC attribute.
     */
    int region = extract32(address, 28, 4);

    *ns = !(region & 1);
    *nsc = false;
    /* 0xe0000000..0xe00fffff and 0xf0000000..0xf00fffff are exempt */
    *exempt = (address & 0xeff00000) == 0xe0000000;
    *iregion = region;
}

static void mps2tz_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    IDAUInterfaceClass *iic = IDAU_INTERFACE_CLASS(oc);

    mc->init = mps2tz_common_init;
    iic->check = mps2_tz_idau_check;
    mc->default_ram_size = 16 * MiB;
    mc->default_ram_id = "mps.ram";
}

static void mps2tz_an505_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS2 with AN505 FPGA image for Cortex-M33";
    mc->default_cpus = 1;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mmc->fpga_type = FPGA_AN505;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m33");
    mmc->scc_id = 0x41045050;
    mmc->sysclk_frq = 20 * 1000 * 1000; /* 20MHz */
    mmc->armsse_type = TYPE_IOTKIT;
}

static void mps2tz_an521_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS2 with AN521 FPGA image for dual Cortex-M33";
    mc->default_cpus = 2;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mmc->fpga_type = FPGA_AN521;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m33");
    mmc->scc_id = 0x41045210;
    mmc->sysclk_frq = 20 * 1000 * 1000; /* 20MHz */
    mmc->armsse_type = TYPE_SSE200;
}

static const TypeInfo mps2tz_info = {
    .name = TYPE_MPS2TZ_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(MPS2TZMachineState),
    .class_size = sizeof(MPS2TZMachineClass),
    .class_init = mps2tz_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_IDAU_INTERFACE },
        { }
    },
};

static const TypeInfo mps2tz_an505_info = {
    .name = TYPE_MPS2TZ_AN505_MACHINE,
    .parent = TYPE_MPS2TZ_MACHINE,
    .class_init = mps2tz_an505_class_init,
};

static const TypeInfo mps2tz_an521_info = {
    .name = TYPE_MPS2TZ_AN521_MACHINE,
    .parent = TYPE_MPS2TZ_MACHINE,
    .class_init = mps2tz_an521_class_init,
};

static void mps2tz_machine_init(void)
{
    type_register_static(&mps2tz_info);
    type_register_static(&mps2tz_an505_info);
    type_register_static(&mps2tz_an521_info);
}

type_init(mps2tz_machine_init);
