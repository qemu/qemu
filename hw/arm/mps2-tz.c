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
 *
 * Links to the TRM for the board itself and to the various Application
 * Notes which document the FPGA images can be found here:
 * https://developer.arm.com/products/system-design/development-boards/fpga-prototyping-boards/mps2
 *
 * Board TRM:
 * http://infocenter.arm.com/help/topic/com.arm.doc.100112_0200_06_en/versatile_express_cortex_m_prototyping_systems_v2m_mps2_and_v2m_mps2plus_technical_reference_100112_0200_06_en.pdf
 * Application Note AN505:
 * http://infocenter.arm.com/help/topic/com.arm.doc.dai0505b/index.html
 *
 * The AN505 defers to the Cortex-M33 processor ARMv8M IoT Kit FVP User Guide
 * (ARM ECM0601256) for the details of some of the device layout:
 *   http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/arm/arm.h"
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
#include "hw/arm/iotkit.h"
#include "hw/devices.h"
#include "net/net.h"
#include "hw/core/split-irq.h"

typedef enum MPS2TZFPGAType {
    FPGA_AN505,
} MPS2TZFPGAType;

typedef struct {
    MachineClass parent;
    MPS2TZFPGAType fpga_type;
    uint32_t scc_id;
} MPS2TZMachineClass;

typedef struct {
    MachineState parent;

    IoTKit iotkit;
    MemoryRegion psram;
    MemoryRegion ssram1;
    MemoryRegion ssram1_m;
    MemoryRegion ssram23;
    MPS2SCC scc;
    MPS2FPGAIO fpgaio;
    TZPPC ppc[5];
    UnimplementedDeviceState ssram_mpc[3];
    UnimplementedDeviceState spi[5];
    UnimplementedDeviceState i2c[4];
    UnimplementedDeviceState i2s_audio;
    UnimplementedDeviceState gpio[5];
    UnimplementedDeviceState dma[4];
    UnimplementedDeviceState gfx;
    CMSDKAPBUART uart[5];
    SplitIRQ sec_resp_splitter;
    qemu_or_irq uart_irq_orgate;
} MPS2TZMachineState;

#define TYPE_MPS2TZ_MACHINE "mps2tz"
#define TYPE_MPS2TZ_AN505_MACHINE MACHINE_TYPE_NAME("mps2-an505")

#define MPS2TZ_MACHINE(obj) \
    OBJECT_CHECK(MPS2TZMachineState, obj, TYPE_MPS2TZ_MACHINE)
#define MPS2TZ_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MPS2TZMachineClass, obj, TYPE_MPS2TZ_MACHINE)
#define MPS2TZ_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(MPS2TZMachineClass, klass, TYPE_MPS2TZ_MACHINE)

/* Main SYSCLK frequency in Hz */
#define SYSCLK_FRQ 20000000

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

static void init_sysbus_child(Object *parent, const char *childname,
                              void *child, size_t childsize,
                              const char *childtype)
{
    object_initialize(child, childsize, childtype);
    object_property_add_child(parent, childname, OBJECT(child), &error_abort);
    qdev_set_parent_bus(DEVICE(child), sysbus_get_default());

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

    init_sysbus_child(OBJECT(mms), name, uds,
                      sizeof(UnimplementedDeviceState),
                      TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(DEVICE(uds), "name", name);
    qdev_prop_set_uint64(DEVICE(uds), "size", size);
    object_property_set_bool(OBJECT(uds), true, "realized", &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(uds), 0);
}

static MemoryRegion *make_uart(MPS2TZMachineState *mms, void *opaque,
                               const char *name, hwaddr size)
{
    CMSDKAPBUART *uart = opaque;
    int i = uart - &mms->uart[0];
    Chardev *uartchr = i < MAX_SERIAL_PORTS ? serial_hds[i] : NULL;
    int rxirqno = i * 2;
    int txirqno = i * 2 + 1;
    int combirqno = i + 10;
    SysBusDevice *s;
    DeviceState *iotkitdev = DEVICE(&mms->iotkit);
    DeviceState *orgate_dev = DEVICE(&mms->uart_irq_orgate);

    init_sysbus_child(OBJECT(mms), name, uart,
                      sizeof(mms->uart[0]), TYPE_CMSDK_APB_UART);
    qdev_prop_set_chr(DEVICE(uart), "chardev", uartchr);
    qdev_prop_set_uint32(DEVICE(uart), "pclk-frq", SYSCLK_FRQ);
    object_property_set_bool(OBJECT(uart), true, "realized", &error_fatal);
    s = SYS_BUS_DEVICE(uart);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in_named(iotkitdev,
                                                    "EXP_IRQ", txirqno));
    sysbus_connect_irq(s, 1, qdev_get_gpio_in_named(iotkitdev,
                                                    "EXP_IRQ", rxirqno));
    sysbus_connect_irq(s, 2, qdev_get_gpio_in(orgate_dev, i * 2));
    sysbus_connect_irq(s, 3, qdev_get_gpio_in(orgate_dev, i * 2 + 1));
    sysbus_connect_irq(s, 4, qdev_get_gpio_in_named(iotkitdev,
                                                    "EXP_IRQ", combirqno));
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(uart), 0);
}

static MemoryRegion *make_scc(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    MPS2SCC *scc = opaque;
    DeviceState *sccdev;
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);

    object_initialize(scc, sizeof(mms->scc), TYPE_MPS2_SCC);
    sccdev = DEVICE(scc);
    qdev_set_parent_bus(sccdev, sysbus_get_default());
    qdev_prop_set_uint32(sccdev, "scc-cfg4", 0x2);
    qdev_prop_set_uint32(sccdev, "scc-aid", 0x02000008);
    qdev_prop_set_uint32(sccdev, "scc-id", mmc->scc_id);
    object_property_set_bool(OBJECT(scc), true, "realized", &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(sccdev), 0);
}

static MemoryRegion *make_fpgaio(MPS2TZMachineState *mms, void *opaque,
                                 const char *name, hwaddr size)
{
    MPS2FPGAIO *fpgaio = opaque;

    object_initialize(fpgaio, sizeof(mms->fpgaio), TYPE_MPS2_FPGAIO);
    qdev_set_parent_bus(DEVICE(fpgaio), sysbus_get_default());
    object_property_set_bool(OBJECT(fpgaio), true, "realized", &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(fpgaio), 0);
}

static void mps2tz_common_init(MachineState *machine)
{
    MPS2TZMachineState *mms = MPS2TZ_MACHINE(machine);
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

    init_sysbus_child(OBJECT(machine), "iotkit", &mms->iotkit,
                      sizeof(mms->iotkit), TYPE_IOTKIT);
    iotkitdev = DEVICE(&mms->iotkit);
    object_property_set_link(OBJECT(&mms->iotkit), OBJECT(system_memory),
                             "memory", &error_abort);
    qdev_prop_set_uint32(iotkitdev, "EXP_NUMIRQ", 92);
    qdev_prop_set_uint32(iotkitdev, "MAINCLK", SYSCLK_FRQ);
    object_property_set_bool(OBJECT(&mms->iotkit), true, "realized",
                             &error_fatal);

    /* The sec_resp_cfg output from the IoTKit must be split into multiple
     * lines, one for each of the PPCs we create here.
     */
    object_initialize(&mms->sec_resp_splitter, sizeof(mms->sec_resp_splitter),
                      TYPE_SPLIT_IRQ);
    object_property_add_child(OBJECT(machine), "sec-resp-splitter",
                              OBJECT(&mms->sec_resp_splitter), &error_abort);
    object_property_set_int(OBJECT(&mms->sec_resp_splitter), 5,
                            "num-lines", &error_fatal);
    object_property_set_bool(OBJECT(&mms->sec_resp_splitter), true,
                             "realized", &error_fatal);
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
    memory_region_allocate_system_memory(&mms->psram,
                                         NULL, "mps.ram", 0x01000000);
    memory_region_add_subregion(system_memory, 0x80000000, &mms->psram);

    /* The SSRAM memories should all be behind Memory Protection Controllers,
     * but we don't implement that yet.
     */
    make_ram(&mms->ssram1, "mps.ssram1", 0x00000000, 0x00400000);
    make_ram_alias(&mms->ssram1_m, "mps.ssram1_m", &mms->ssram1, 0x00400000);

    make_ram(&mms->ssram23, "mps.ssram23", 0x28000000, 0x00400000);

    /* The overflow IRQs for all UARTs are ORed together.
     * Tx, Rx and "combined" IRQs are sent to the NVIC separately.
     * Create the OR gate for this.
     */
    object_initialize(&mms->uart_irq_orgate, sizeof(mms->uart_irq_orgate),
                      TYPE_OR_IRQ);
    object_property_add_child(OBJECT(mms), "uart-irq-orgate",
                              OBJECT(&mms->uart_irq_orgate), &error_abort);
    object_property_set_int(OBJECT(&mms->uart_irq_orgate), 10, "num-lines",
                            &error_fatal);
    object_property_set_bool(OBJECT(&mms->uart_irq_orgate), true,
                             "realized", &error_fatal);
    qdev_connect_gpio_out(DEVICE(&mms->uart_irq_orgate), 0,
                          qdev_get_gpio_in_named(iotkitdev, "EXP_IRQ", 15));

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
                { "ssram-mpc0", make_unimp_dev, &mms->ssram_mpc[0],
                  0x58007000, 0x1000 },
                { "ssram-mpc1", make_unimp_dev, &mms->ssram_mpc[1],
                  0x58008000, 0x1000 },
                { "ssram-mpc2", make_unimp_dev, &mms->ssram_mpc[2],
                  0x58009000, 0x1000 },
            },
        }, {
            .name = "apb_ppcexp1",
            .ports = {
                { "spi0", make_unimp_dev, &mms->spi[0], 0x40205000, 0x1000 },
                { "spi1", make_unimp_dev, &mms->spi[1], 0x40206000, 0x1000 },
                { "spi2", make_unimp_dev, &mms->spi[2], 0x40209000, 0x1000 },
                { "spi3", make_unimp_dev, &mms->spi[3], 0x4020a000, 0x1000 },
                { "spi4", make_unimp_dev, &mms->spi[4], 0x4020b000, 0x1000 },
                { "uart0", make_uart, &mms->uart[0], 0x40200000, 0x1000 },
                { "uart1", make_uart, &mms->uart[1], 0x40201000, 0x1000 },
                { "uart2", make_uart, &mms->uart[2], 0x40202000, 0x1000 },
                { "uart3", make_uart, &mms->uart[3], 0x40203000, 0x1000 },
                { "uart4", make_uart, &mms->uart[4], 0x40204000, 0x1000 },
                { "i2c0", make_unimp_dev, &mms->i2c[0], 0x40207000, 0x1000 },
                { "i2c1", make_unimp_dev, &mms->i2c[1], 0x40208000, 0x1000 },
                { "i2c2", make_unimp_dev, &mms->i2c[2], 0x4020c000, 0x1000 },
                { "i2c3", make_unimp_dev, &mms->i2c[3], 0x4020d000, 0x1000 },
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
                { "gpio4", make_unimp_dev, &mms->gpio[4], 0x40104000, 0x1000 },
            },
        }, {
            .name = "ahb_ppcexp1",
            .ports = {
                { "dma0", make_unimp_dev, &mms->dma[0], 0x40110000, 0x1000 },
                { "dma1", make_unimp_dev, &mms->dma[1], 0x40111000, 0x1000 },
                { "dma2", make_unimp_dev, &mms->dma[2], 0x40112000, 0x1000 },
                { "dma3", make_unimp_dev, &mms->dma[3], 0x40113000, 0x1000 },
            },
        },
    };

    for (i = 0; i < ARRAY_SIZE(ppcs); i++) {
        const PPCInfo *ppcinfo = &ppcs[i];
        TZPPC *ppc = &mms->ppc[i];
        DeviceState *ppcdev;
        int port;
        char *gpioname;

        init_sysbus_child(OBJECT(machine), ppcinfo->name, ppc,
                          sizeof(TZPPC), TYPE_TZ_PPC);
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
            object_property_set_link(OBJECT(ppc), OBJECT(mr),
                                     portname, &error_fatal);
            g_free(portname);
        }

        object_property_set_bool(OBJECT(ppc), true, "realized", &error_fatal);

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

    /* In hardware this is a LAN9220; the LAN9118 is software compatible
     * except that it doesn't support the checksum-offload feature.
     * The ethernet controller is not behind a PPC.
     */
    lan9118_init(&nd_table[0], 0x42000000,
                 qdev_get_gpio_in_named(iotkitdev, "EXP_IRQ", 16));

    create_unimplemented_device("FPGA NS PC", 0x48007000, 0x1000);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename, 0x400000);
}

static void mps2tz_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = mps2tz_common_init;
    mc->max_cpus = 1;
}

static void mps2tz_an505_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS2 with AN505 FPGA image for Cortex-M33";
    mmc->fpga_type = FPGA_AN505;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m33");
    mmc->scc_id = 0x41040000 | (505 << 4);
}

static const TypeInfo mps2tz_info = {
    .name = TYPE_MPS2TZ_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(MPS2TZMachineState),
    .class_size = sizeof(MPS2TZMachineClass),
    .class_init = mps2tz_class_init,
};

static const TypeInfo mps2tz_an505_info = {
    .name = TYPE_MPS2TZ_AN505_MACHINE,
    .parent = TYPE_MPS2TZ_MACHINE,
    .class_init = mps2tz_an505_class_init,
};

static void mps2tz_machine_init(void)
{
    type_register_static(&mps2tz_info);
    type_register_static(&mps2tz_an505_info);
}

type_init(mps2tz_machine_init);
