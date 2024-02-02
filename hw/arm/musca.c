/*
 * Arm Musca-B1 test chip board emulation
 *
 * Copyright (c) 2019 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * The Musca boards are a reference implementation of a system using
 * the SSE-200 subsystem for embedded:
 * https://developer.arm.com/products/system-design/development-boards/iot-test-chips-and-boards/musca-a-test-chip-board
 * https://developer.arm.com/products/system-design/development-boards/iot-test-chips-and-boards/musca-b-test-chip-board
 * We model the A and B1 variants of this board, as described in the TRMs:
 * https://developer.arm.com/documentation/101107/latest/
 * https://developer.arm.com/documentation/101312/latest/
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/arm/boot.h"
#include "hw/arm/armsse.h"
#include "hw/boards.h"
#include "hw/char/pl011.h"
#include "hw/core/split-irq.h"
#include "hw/misc/tz-mpc.h"
#include "hw/misc/tz-ppc.h"
#include "hw/misc/unimp.h"
#include "hw/rtc/pl031.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"

#define MUSCA_NUMIRQ_MAX 96
#define MUSCA_PPC_MAX 3
#define MUSCA_MPC_MAX 5

typedef struct MPCInfo MPCInfo;

typedef enum MuscaType {
    MUSCA_A,
    MUSCA_B1,
} MuscaType;

struct MuscaMachineClass {
    MachineClass parent;
    MuscaType type;
    uint32_t init_svtor;
    int sram_addr_width;
    int num_irqs;
    const MPCInfo *mpc_info;
    int num_mpcs;
};

struct MuscaMachineState {
    MachineState parent;

    ARMSSE sse;
    /* RAM and flash */
    MemoryRegion ram[MUSCA_MPC_MAX];
    SplitIRQ cpu_irq_splitter[MUSCA_NUMIRQ_MAX];
    SplitIRQ sec_resp_splitter;
    TZPPC ppc[MUSCA_PPC_MAX];
    MemoryRegion container;
    UnimplementedDeviceState eflash[2];
    UnimplementedDeviceState qspi;
    TZMPC mpc[MUSCA_MPC_MAX];
    UnimplementedDeviceState mhu[2];
    UnimplementedDeviceState pwm[3];
    UnimplementedDeviceState i2s;
    PL011State uart[2];
    UnimplementedDeviceState i2c[2];
    UnimplementedDeviceState spi;
    UnimplementedDeviceState scc;
    UnimplementedDeviceState timer;
    PL031State rtc;
    UnimplementedDeviceState pvt;
    UnimplementedDeviceState sdio;
    UnimplementedDeviceState gpio;
    UnimplementedDeviceState cryptoisland;
    Clock *sysclk;
    Clock *s32kclk;
};

#define TYPE_MUSCA_MACHINE "musca"
#define TYPE_MUSCA_A_MACHINE MACHINE_TYPE_NAME("musca-a")
#define TYPE_MUSCA_B1_MACHINE MACHINE_TYPE_NAME("musca-b1")

OBJECT_DECLARE_TYPE(MuscaMachineState, MuscaMachineClass, MUSCA_MACHINE)

/*
 * Main SYSCLK frequency in Hz
 * TODO this should really be different for the two cores, but we
 * don't model that in our SSE-200 model yet.
 */
#define SYSCLK_FRQ 40000000
/* Slow 32Khz S32KCLK frequency in Hz */
#define S32KCLK_FRQ (32 * 1000)

static qemu_irq get_sse_irq_in(MuscaMachineState *mms, int irqno)
{
    /* Return a qemu_irq which will signal IRQ n to all CPUs in the SSE. */
    assert(irqno < MUSCA_NUMIRQ_MAX);

    return qdev_get_gpio_in(DEVICE(&mms->cpu_irq_splitter[irqno]), 0);
}

/*
 * Most of the devices in the Musca board sit behind Peripheral Protection
 * Controllers. These data structures define the layout of which devices
 * sit behind which PPCs.
 * The devfn for each port is a function which creates, configures
 * and initializes the device, returning the MemoryRegion which
 * needs to be plugged into the downstream end of the PPC port.
 */
typedef MemoryRegion *MakeDevFn(MuscaMachineState *mms, void *opaque,
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

static MemoryRegion *make_unimp_dev(MuscaMachineState *mms,
                                    void *opaque, const char *name, hwaddr size)
{
    /*
     * Initialize, configure and realize a TYPE_UNIMPLEMENTED_DEVICE,
     * and return a pointer to its MemoryRegion.
     */
    UnimplementedDeviceState *uds = opaque;

    object_initialize_child(OBJECT(mms), name, uds, TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(DEVICE(uds), "name", name);
    qdev_prop_set_uint64(DEVICE(uds), "size", size);
    sysbus_realize(SYS_BUS_DEVICE(uds), &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(uds), 0);
}

typedef enum MPCInfoType {
    MPC_RAM,
    MPC_ROM,
    MPC_CRYPTOISLAND,
} MPCInfoType;

struct MPCInfo {
    const char *name;
    hwaddr addr;
    hwaddr size;
    MPCInfoType type;
};

/* Order of the MPCs here must match the order of the bits in SECMPCINTSTATUS */
static const MPCInfo a_mpc_info[] = { {
        .name = "qspi",
        .type = MPC_ROM,
        .addr = 0x00200000,
        .size = 0x00800000,
    }, {
        .name = "sram",
        .type = MPC_RAM,
        .addr = 0x00000000,
        .size = 0x00200000,
    }
};

static const MPCInfo b1_mpc_info[] = { {
        .name = "qspi",
        .type = MPC_ROM,
        .addr = 0x00000000,
        .size = 0x02000000,
    }, {
        .name = "sram",
        .type = MPC_RAM,
        .addr = 0x0a400000,
        .size = 0x00080000,
    }, {
        .name = "eflash0",
        .type = MPC_ROM,
        .addr = 0x0a000000,
        .size = 0x00200000,
    }, {
        .name = "eflash1",
        .type = MPC_ROM,
        .addr = 0x0a200000,
        .size = 0x00200000,
    }, {
        .name = "cryptoisland",
        .type = MPC_CRYPTOISLAND,
        .addr = 0x0a000000,
        .size = 0x00200000,
    }
};

static MemoryRegion *make_mpc(MuscaMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    /*
     * Create an MPC and the RAM or flash behind it.
     * MPC 0: eFlash 0
     * MPC 1: eFlash 1
     * MPC 2: SRAM
     * MPC 3: QSPI flash
     * MPC 4: CryptoIsland
     * For now we implement the flash regions as ROM (ie not programmable)
     * (with their control interface memory regions being unimplemented
     * stubs behind the PPCs).
     * The whole CryptoIsland region behind its MPC is an unimplemented stub.
     */
    MuscaMachineClass *mmc = MUSCA_MACHINE_GET_CLASS(mms);
    TZMPC *mpc = opaque;
    int i = mpc - &mms->mpc[0];
    MemoryRegion *downstream;
    MemoryRegion *upstream;
    UnimplementedDeviceState *uds;
    char *mpcname;
    const MPCInfo *mpcinfo = mmc->mpc_info;

    mpcname = g_strdup_printf("%s-mpc", mpcinfo[i].name);

    switch (mpcinfo[i].type) {
    case MPC_ROM:
        downstream = &mms->ram[i];
        memory_region_init_rom(downstream, NULL, mpcinfo[i].name,
                               mpcinfo[i].size, &error_fatal);
        break;
    case MPC_RAM:
        downstream = &mms->ram[i];
        memory_region_init_ram(downstream, NULL, mpcinfo[i].name,
                               mpcinfo[i].size, &error_fatal);
        break;
    case MPC_CRYPTOISLAND:
        /* We don't implement the CryptoIsland yet */
        uds = &mms->cryptoisland;
        object_initialize_child(OBJECT(mms), name, uds,
                                TYPE_UNIMPLEMENTED_DEVICE);
        qdev_prop_set_string(DEVICE(uds), "name", mpcinfo[i].name);
        qdev_prop_set_uint64(DEVICE(uds), "size", mpcinfo[i].size);
        sysbus_realize(SYS_BUS_DEVICE(uds), &error_fatal);
        downstream = sysbus_mmio_get_region(SYS_BUS_DEVICE(uds), 0);
        break;
    default:
        g_assert_not_reached();
    }

    object_initialize_child(OBJECT(mms), mpcname, mpc, TYPE_TZ_MPC);
    object_property_set_link(OBJECT(mpc), "downstream", OBJECT(downstream),
                             &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(mpc), &error_fatal);
    /* Map the upstream end of the MPC into system memory */
    upstream = sysbus_mmio_get_region(SYS_BUS_DEVICE(mpc), 1);
    memory_region_add_subregion(get_system_memory(), mpcinfo[i].addr, upstream);
    /* and connect its interrupt to the SSE-200 */
    qdev_connect_gpio_out_named(DEVICE(mpc), "irq", 0,
                                qdev_get_gpio_in_named(DEVICE(&mms->sse),
                                                       "mpcexp_status", i));

    g_free(mpcname);
    /* Return the register interface MR for our caller to map behind the PPC */
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(mpc), 0);
}

static MemoryRegion *make_rtc(MuscaMachineState *mms, void *opaque,
                              const char *name, hwaddr size)
{
    PL031State *rtc = opaque;

    object_initialize_child(OBJECT(mms), name, rtc, TYPE_PL031);
    sysbus_realize(SYS_BUS_DEVICE(rtc), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(rtc), 0, get_sse_irq_in(mms, 39));
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(rtc), 0);
}

static MemoryRegion *make_uart(MuscaMachineState *mms, void *opaque,
                               const char *name, hwaddr size)
{
    PL011State *uart = opaque;
    int i = uart - &mms->uart[0];
    int irqbase = 7 + i * 6;
    SysBusDevice *s;

    object_initialize_child(OBJECT(mms), name, uart, TYPE_PL011);
    qdev_prop_set_chr(DEVICE(uart), "chardev", serial_hd(i));
    sysbus_realize(SYS_BUS_DEVICE(uart), &error_fatal);
    s = SYS_BUS_DEVICE(uart);
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, irqbase + 5)); /* combined */
    sysbus_connect_irq(s, 1, get_sse_irq_in(mms, irqbase + 0)); /* RX */
    sysbus_connect_irq(s, 2, get_sse_irq_in(mms, irqbase + 1)); /* TX */
    sysbus_connect_irq(s, 3, get_sse_irq_in(mms, irqbase + 2)); /* RT */
    sysbus_connect_irq(s, 4, get_sse_irq_in(mms, irqbase + 3)); /* MS */
    sysbus_connect_irq(s, 5, get_sse_irq_in(mms, irqbase + 4)); /* E */
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(uart), 0);
}

static MemoryRegion *make_musca_a_devs(MuscaMachineState *mms, void *opaque,
                                       const char *name, hwaddr size)
{
    /*
     * Create the container MemoryRegion for all the devices that live
     * behind the Musca-A PPC's single port. These devices don't have a PPC
     * port each, but we use the PPCPortInfo struct as a convenient way
     * to describe them. Note that addresses here are relative to the base
     * address of the PPC port region: 0x40100000, and devices appear both
     * at the 0x4... NS region and the 0x5... S region.
     */
    int i;
    MemoryRegion *container = &mms->container;

    const PPCPortInfo devices[] = {
        { "uart0", make_uart, &mms->uart[0], 0x1000, 0x1000 },
        { "uart1", make_uart, &mms->uart[1], 0x2000, 0x1000 },
        { "spi", make_unimp_dev, &mms->spi, 0x3000, 0x1000 },
        { "i2c0", make_unimp_dev, &mms->i2c[0], 0x4000, 0x1000 },
        { "i2c1", make_unimp_dev, &mms->i2c[1], 0x5000, 0x1000 },
        { "i2s", make_unimp_dev, &mms->i2s, 0x6000, 0x1000 },
        { "pwm0", make_unimp_dev, &mms->pwm[0], 0x7000, 0x1000 },
        { "rtc", make_rtc, &mms->rtc, 0x8000, 0x1000 },
        { "qspi", make_unimp_dev, &mms->qspi, 0xa000, 0x1000 },
        { "timer", make_unimp_dev, &mms->timer, 0xb000, 0x1000 },
        { "scc", make_unimp_dev, &mms->scc, 0xc000, 0x1000 },
        { "pwm1", make_unimp_dev, &mms->pwm[1], 0xe000, 0x1000 },
        { "pwm2", make_unimp_dev, &mms->pwm[2], 0xf000, 0x1000 },
        { "gpio", make_unimp_dev, &mms->gpio, 0x10000, 0x1000 },
        { "mpc0", make_mpc, &mms->mpc[0], 0x12000, 0x1000 },
        { "mpc1", make_mpc, &mms->mpc[1], 0x13000, 0x1000 },
    };

    memory_region_init(container, OBJECT(mms), "musca-device-container", size);

    for (i = 0; i < ARRAY_SIZE(devices); i++) {
        const PPCPortInfo *pinfo = &devices[i];
        MemoryRegion *mr;

        mr = pinfo->devfn(mms, pinfo->opaque, pinfo->name, pinfo->size);
        memory_region_add_subregion(container, pinfo->addr, mr);
    }

    return &mms->container;
}

static void musca_init(MachineState *machine)
{
    MuscaMachineState *mms = MUSCA_MACHINE(machine);
    MuscaMachineClass *mmc = MUSCA_MACHINE_GET_CLASS(mms);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *ssedev;
    DeviceState *dev_splitter;
    const PPCInfo *ppcs;
    int num_ppcs;
    int i;

    assert(mmc->num_irqs <= MUSCA_NUMIRQ_MAX);
    assert(mmc->num_mpcs <= MUSCA_MPC_MAX);

    mms->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(mms->sysclk, SYSCLK_FRQ);
    mms->s32kclk = clock_new(OBJECT(machine), "S32KCLK");
    clock_set_hz(mms->s32kclk, S32KCLK_FRQ);

    object_initialize_child(OBJECT(machine), "sse-200", &mms->sse,
                            TYPE_SSE200);
    ssedev = DEVICE(&mms->sse);
    object_property_set_link(OBJECT(&mms->sse), "memory",
                             OBJECT(system_memory), &error_fatal);
    qdev_prop_set_uint32(ssedev, "EXP_NUMIRQ", mmc->num_irqs);
    qdev_prop_set_uint32(ssedev, "init-svtor", mmc->init_svtor);
    qdev_prop_set_uint32(ssedev, "SRAM_ADDR_WIDTH", mmc->sram_addr_width);
    qdev_connect_clock_in(ssedev, "MAINCLK", mms->sysclk);
    qdev_connect_clock_in(ssedev, "S32KCLK", mms->s32kclk);
    /*
     * Musca-A takes the default SSE-200 FPU/DSP settings (ie no for
     * CPU0 and yes for CPU1); Musca-B1 explicitly enables them for CPU0.
     */
    if (mmc->type == MUSCA_B1) {
        qdev_prop_set_bit(ssedev, "CPU0_FPU", true);
        qdev_prop_set_bit(ssedev, "CPU0_DSP", true);
    }
    sysbus_realize(SYS_BUS_DEVICE(&mms->sse), &error_fatal);

    /*
     * We need to create splitters to feed the IRQ inputs
     * for each CPU in the SSE-200 from each device in the board.
     */
    for (i = 0; i < mmc->num_irqs; i++) {
        char *name = g_strdup_printf("musca-irq-splitter%d", i);
        SplitIRQ *splitter = &mms->cpu_irq_splitter[i];

        object_initialize_child_with_props(OBJECT(machine), name, splitter,
                                           sizeof(*splitter), TYPE_SPLIT_IRQ,
                                           &error_fatal, NULL);
        g_free(name);

        object_property_set_int(OBJECT(splitter), "num-lines", 2,
                                &error_fatal);
        qdev_realize(DEVICE(splitter), NULL, &error_fatal);
        qdev_connect_gpio_out(DEVICE(splitter), 0,
                              qdev_get_gpio_in_named(ssedev, "EXP_IRQ", i));
        qdev_connect_gpio_out(DEVICE(splitter), 1,
                              qdev_get_gpio_in_named(ssedev,
                                                     "EXP_CPU1_IRQ", i));
    }

    /*
     * The sec_resp_cfg output from the SSE-200 must be split into multiple
     * lines, one for each of the PPCs we create here.
     */
    object_initialize_child_with_props(OBJECT(machine), "sec-resp-splitter",
                                       &mms->sec_resp_splitter,
                                       sizeof(mms->sec_resp_splitter),
                                       TYPE_SPLIT_IRQ, &error_fatal, NULL);

    object_property_set_int(OBJECT(&mms->sec_resp_splitter), "num-lines",
                            ARRAY_SIZE(mms->ppc), &error_fatal);
    qdev_realize(DEVICE(&mms->sec_resp_splitter), NULL, &error_fatal);
    dev_splitter = DEVICE(&mms->sec_resp_splitter);
    qdev_connect_gpio_out_named(ssedev, "sec_resp_cfg", 0,
                                qdev_get_gpio_in(dev_splitter, 0));

    /*
     * Most of the devices in the board are behind Peripheral Protection
     * Controllers. The required order for initializing things is:
     *  + initialize the PPC
     *  + initialize, configure and realize downstream devices
     *  + connect downstream device MemoryRegions to the PPC
     *  + realize the PPC
     *  + map the PPC's MemoryRegions to the places in the address map
     *    where the downstream devices should appear
     *  + wire up the PPC's control lines to the SSE object
     *
     * The PPC mapping differs for the -A and -B1 variants; the -A version
     * is much simpler, using only a single port of a single PPC and putting
     * all the devices behind that.
     */
    const PPCInfo a_ppcs[] = { {
            .name = "ahb_ppcexp0",
            .ports = {
                { "musca-devices", make_musca_a_devs, 0, 0x40100000, 0x100000 },
            },
        },
    };

    /*
     * Devices listed with an 0x4.. address appear in both the NS 0x4.. region
     * and the 0x5.. S region. Devices listed with an 0x5.. address appear
     * only in the S region.
     */
    const PPCInfo b1_ppcs[] = { {
            .name = "apb_ppcexp0",
            .ports = {
                { "eflash0", make_unimp_dev, &mms->eflash[0],
                  0x52400000, 0x1000 },
                { "eflash1", make_unimp_dev, &mms->eflash[1],
                  0x52500000, 0x1000 },
                { "qspi", make_unimp_dev, &mms->qspi, 0x42800000, 0x100000 },
                { "mpc0", make_mpc, &mms->mpc[0], 0x52000000, 0x1000 },
                { "mpc1", make_mpc, &mms->mpc[1], 0x52100000, 0x1000 },
                { "mpc2", make_mpc, &mms->mpc[2], 0x52200000, 0x1000 },
                { "mpc3", make_mpc, &mms->mpc[3], 0x52300000, 0x1000 },
                { "mhu0", make_unimp_dev, &mms->mhu[0], 0x42600000, 0x100000 },
                { "mhu1", make_unimp_dev, &mms->mhu[1], 0x42700000, 0x100000 },
                { }, /* port 9: unused */
                { }, /* port 10: unused */
                { }, /* port 11: unused */
                { }, /* port 12: unused */
                { }, /* port 13: unused */
                { "mpc4", make_mpc, &mms->mpc[4], 0x52e00000, 0x1000 },
            },
        }, {
            .name = "apb_ppcexp1",
            .ports = {
                { "pwm0", make_unimp_dev, &mms->pwm[0], 0x40101000, 0x1000 },
                { "pwm1", make_unimp_dev, &mms->pwm[1], 0x40102000, 0x1000 },
                { "pwm2", make_unimp_dev, &mms->pwm[2], 0x40103000, 0x1000 },
                { "i2s", make_unimp_dev, &mms->i2s, 0x40104000, 0x1000 },
                { "uart0", make_uart, &mms->uart[0], 0x40105000, 0x1000 },
                { "uart1", make_uart, &mms->uart[1], 0x40106000, 0x1000 },
                { "i2c0", make_unimp_dev, &mms->i2c[0], 0x40108000, 0x1000 },
                { "i2c1", make_unimp_dev, &mms->i2c[1], 0x40109000, 0x1000 },
                { "spi", make_unimp_dev, &mms->spi, 0x4010a000, 0x1000 },
                { "scc", make_unimp_dev, &mms->scc, 0x5010b000, 0x1000 },
                { "timer", make_unimp_dev, &mms->timer, 0x4010c000, 0x1000 },
                { "rtc", make_rtc, &mms->rtc, 0x4010d000, 0x1000 },
                { "pvt", make_unimp_dev, &mms->pvt, 0x4010e000, 0x1000 },
                { "sdio", make_unimp_dev, &mms->sdio, 0x4010f000, 0x1000 },
            },
        }, {
            .name = "ahb_ppcexp0",
            .ports = {
                { }, /* port 0: unused */
                { "gpio", make_unimp_dev, &mms->gpio, 0x41000000, 0x1000 },
            },
        },
    };

    switch (mmc->type) {
    case MUSCA_A:
        ppcs = a_ppcs;
        num_ppcs = ARRAY_SIZE(a_ppcs);
        break;
    case MUSCA_B1:
        ppcs = b1_ppcs;
        num_ppcs = ARRAY_SIZE(b1_ppcs);
        break;
    default:
        g_assert_not_reached();
    }
    assert(num_ppcs <= MUSCA_PPC_MAX);

    for (i = 0; i < num_ppcs; i++) {
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
            qdev_connect_gpio_out_named(ssedev, gpioname, port,
                                        qdev_get_gpio_in_named(ppcdev,
                                                               "cfg_nonsec",
                                                               port));
            g_free(gpioname);
            gpioname = g_strdup_printf("%s_ap", ppcinfo->name);
            qdev_connect_gpio_out_named(ssedev, gpioname, port,
                                        qdev_get_gpio_in_named(ppcdev,
                                                               "cfg_ap", port));
            g_free(gpioname);
        }

        gpioname = g_strdup_printf("%s_irq_enable", ppcinfo->name);
        qdev_connect_gpio_out_named(ssedev, gpioname, 0,
                                    qdev_get_gpio_in_named(ppcdev,
                                                           "irq_enable", 0));
        g_free(gpioname);
        gpioname = g_strdup_printf("%s_irq_clear", ppcinfo->name);
        qdev_connect_gpio_out_named(ssedev, gpioname, 0,
                                    qdev_get_gpio_in_named(ppcdev,
                                                           "irq_clear", 0));
        g_free(gpioname);
        gpioname = g_strdup_printf("%s_irq_status", ppcinfo->name);
        qdev_connect_gpio_out_named(ppcdev, "irq", 0,
                                    qdev_get_gpio_in_named(ssedev,
                                                           gpioname, 0));
        g_free(gpioname);

        qdev_connect_gpio_out(dev_splitter, i,
                              qdev_get_gpio_in_named(ppcdev,
                                                     "cfg_sec_resp", 0));
    }

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       0, 0x2000000);
}

static void musca_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m33"),
        NULL
    };

    mc->default_cpus = 2;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->valid_cpu_types = valid_cpu_types;
    mc->init = musca_init;
}

static void musca_a_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MuscaMachineClass *mmc = MUSCA_MACHINE_CLASS(oc);

    mc->desc = "ARM Musca-A board (dual Cortex-M33)";
    mmc->type = MUSCA_A;
    mmc->init_svtor = 0x10200000;
    mmc->sram_addr_width = 15;
    mmc->num_irqs = 64;
    mmc->mpc_info = a_mpc_info;
    mmc->num_mpcs = ARRAY_SIZE(a_mpc_info);
}

static void musca_b1_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MuscaMachineClass *mmc = MUSCA_MACHINE_CLASS(oc);

    mc->desc = "ARM Musca-B1 board (dual Cortex-M33)";
    mmc->type = MUSCA_B1;
    /*
     * This matches the DAPlink firmware which boots from QSPI. There
     * is also a firmware blob which boots from the eFlash, which
     * uses init_svtor = 0x1A000000. QEMU doesn't currently support that,
     * though we could in theory expose a machine property on the command
     * line to allow the user to request eFlash boot.
     */
    mmc->init_svtor = 0x10000000;
    mmc->sram_addr_width = 17;
    mmc->num_irqs = 96;
    mmc->mpc_info = b1_mpc_info;
    mmc->num_mpcs = ARRAY_SIZE(b1_mpc_info);
}

static const TypeInfo musca_info = {
    .name = TYPE_MUSCA_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(MuscaMachineState),
    .class_size = sizeof(MuscaMachineClass),
    .class_init = musca_class_init,
};

static const TypeInfo musca_a_info = {
    .name = TYPE_MUSCA_A_MACHINE,
    .parent = TYPE_MUSCA_MACHINE,
    .class_init = musca_a_class_init,
};

static const TypeInfo musca_b1_info = {
    .name = TYPE_MUSCA_B1_MACHINE,
    .parent = TYPE_MUSCA_MACHINE,
    .class_init = musca_b1_class_init,
};

static void musca_machine_init(void)
{
    type_register_static(&musca_info);
    type_register_static(&musca_a_info);
    type_register_static(&musca_b1_info);
}

type_init(musca_machine_init);
