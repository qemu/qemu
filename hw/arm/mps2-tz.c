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
 *  "mps2-an524" -- Dual Cortex-M33 as documented in Application Note AN524
 *  "mps2-an547" -- Single Cortex-M55 as documented in Application Note AN547
 *
 * Links to the TRM for the board itself and to the various Application
 * Notes which document the FPGA images can be found here:
 * https://developer.arm.com/products/system-design/development-boards/fpga-prototyping-boards/mps2
 *
 * Board TRM:
 * https://developer.arm.com/documentation/100112/latest/
 * Application Note AN505:
 * https://developer.arm.com/documentation/dai0505/latest/
 * Application Note AN521:
 * https://developer.arm.com/documentation/dai0521/latest/
 * Application Note AN524:
 * https://developer.arm.com/documentation/dai0524/latest/
 * Application Note AN547:
 * https://developer.arm.com/documentation/dai0547/latest/
 *
 * The AN505 defers to the Cortex-M33 processor ARMv8M IoT Kit FVP User Guide
 * (ARM ECM0601256) for the details of some of the device layout:
 *  https://developer.arm.com/documentation/ecm0601256/latest
 * Similarly, the AN521 and AN524 use the SSE-200, and the SSE-200 TRM defines
 * most of the device layout:
 *  https://developer.arm.com/documentation/101104/latest/
 * and the AN547 uses the SSE-300, whose layout is in the SSE-300 TRM:
 *  https://developer.arm.com/documentation/101773/latest/
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qmp/qlist.h"
#include "qemu/error-report.h"
#include "hw/arm/boot.h"
#include "hw/arm/armv7m.h"
#include "hw/or-irq.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "hw/misc/unimp.h"
#include "hw/char/cmsdk-apb-uart.h"
#include "hw/timer/cmsdk-apb-timer.h"
#include "hw/misc/mps2-scc.h"
#include "hw/misc/mps2-fpgaio.h"
#include "hw/misc/tz-mpc.h"
#include "hw/misc/tz-msc.h"
#include "hw/arm/armsse.h"
#include "hw/dma/pl080.h"
#include "hw/rtc/pl031.h"
#include "hw/ssi/pl022.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/net/lan9118.h"
#include "net/net.h"
#include "hw/core/split-irq.h"
#include "hw/qdev-clock.h"
#include "qom/object.h"
#include "hw/irq.h"

#define MPS2TZ_NUMIRQ_MAX 96
#define MPS2TZ_RAM_MAX 5

typedef enum MPS2TZFPGAType {
    FPGA_AN505,
    FPGA_AN521,
    FPGA_AN524,
    FPGA_AN547,
} MPS2TZFPGAType;

/*
 * Define the layout of RAM in a board, including which parts are
 * behind which MPCs.
 * mrindex specifies the index into mms->ram[] to use for the backing RAM;
 * -1 means "use the system RAM".
 */
typedef struct RAMInfo {
    const char *name;
    uint32_t base;
    uint32_t size;
    int mpc; /* MPC number, -1 for "not behind an MPC" */
    int mrindex;
    int flags;
} RAMInfo;

/*
 * Flag values:
 *  IS_ALIAS: this RAM area is an alias to the upstream end of the
 *    MPC specified by its .mpc value
 *  IS_ROM: this RAM area is read-only
 */
#define IS_ALIAS 1
#define IS_ROM 2

struct MPS2TZMachineClass {
    MachineClass parent;
    MPS2TZFPGAType fpga_type;
    uint32_t scc_id;
    uint32_t sysclk_frq; /* Main SYSCLK frequency in Hz */
    uint32_t apb_periph_frq; /* APB peripheral frequency in Hz */
    uint32_t len_oscclk;
    const uint32_t *oscclk;
    uint32_t fpgaio_num_leds; /* Number of LEDs in FPGAIO LED0 register */
    bool fpgaio_has_switches; /* Does FPGAIO have SWITCH register? */
    bool fpgaio_has_dbgctrl; /* Does FPGAIO have DBGCTRL register? */
    int numirq; /* Number of external interrupts */
    int uart_overflow_irq; /* number of the combined UART overflow IRQ */
    uint32_t init_svtor; /* init-svtor setting for SSE */
    uint32_t sram_addr_width; /* SRAM_ADDR_WIDTH setting for SSE */
    uint32_t cpu0_mpu_ns; /* CPU0_MPU_NS setting for SSE */
    uint32_t cpu0_mpu_s; /* CPU0_MPU_S setting for SSE */
    uint32_t cpu1_mpu_ns; /* CPU1_MPU_NS setting for SSE */
    uint32_t cpu1_mpu_s; /* CPU1_MPU_S setting for SSE */
    const RAMInfo *raminfo;
    const char *armsse_type;
    uint32_t boot_ram_size; /* size of ram at address 0; 0 == find in raminfo */
};

struct MPS2TZMachineState {
    MachineState parent;

    ARMSSE iotkit;
    MemoryRegion ram[MPS2TZ_RAM_MAX];
    MemoryRegion eth_usb_container;

    MPS2SCC scc;
    MPS2FPGAIO fpgaio;
    TZPPC ppc[5];
    TZMPC mpc[3];
    PL022State spi[5];
    ArmSbconI2CState i2c[5];
    UnimplementedDeviceState i2s_audio;
    UnimplementedDeviceState gpio[4];
    UnimplementedDeviceState gfx;
    UnimplementedDeviceState cldc;
    UnimplementedDeviceState usb;
    PL031State rtc;
    PL080State dma[4];
    TZMSC msc[4];
    CMSDKAPBUART uart[6];
    SplitIRQ sec_resp_splitter;
    OrIRQState uart_irq_orgate;
    DeviceState *lan9118;
    SplitIRQ cpu_irq_splitter[MPS2TZ_NUMIRQ_MAX];
    Clock *sysclk;
    Clock *s32kclk;

    bool remap;
    qemu_irq remap_irq;
};

#define TYPE_MPS2TZ_MACHINE "mps2tz"
#define TYPE_MPS2TZ_AN505_MACHINE MACHINE_TYPE_NAME("mps2-an505")
#define TYPE_MPS2TZ_AN521_MACHINE MACHINE_TYPE_NAME("mps2-an521")
#define TYPE_MPS3TZ_AN524_MACHINE MACHINE_TYPE_NAME("mps3-an524")
#define TYPE_MPS3TZ_AN547_MACHINE MACHINE_TYPE_NAME("mps3-an547")

OBJECT_DECLARE_TYPE(MPS2TZMachineState, MPS2TZMachineClass, MPS2TZ_MACHINE)

/* Slow 32Khz S32KCLK frequency in Hz */
#define S32KCLK_FRQ (32 * 1000)

/*
 * The MPS3 DDR is 2GiB, but on a 32-bit host QEMU doesn't permit
 * emulation of that much guest RAM, so artificially make it smaller.
 */
#if HOST_LONG_BITS == 32
#define MPS3_DDR_SIZE (1 * GiB)
#else
#define MPS3_DDR_SIZE (2 * GiB)
#endif

/* For cpu{0,1}_mpu_{ns,s}, means "leave at SSE's default value" */
#define MPU_REGION_DEFAULT UINT32_MAX

static const uint32_t an505_oscclk[] = {
    40000000,
    24580000,
    25000000,
};

static const uint32_t an524_oscclk[] = {
    24000000,
    32000000,
    50000000,
    50000000,
    24576000,
    23750000,
};

static const RAMInfo an505_raminfo[] = { {
        .name = "ssram-0",
        .base = 0x00000000,
        .size = 0x00400000,
        .mpc = 0,
        .mrindex = 0,
    }, {
        .name = "ssram-1",
        .base = 0x28000000,
        .size = 0x00200000,
        .mpc = 1,
        .mrindex = 1,
    }, {
        .name = "ssram-2",
        .base = 0x28200000,
        .size = 0x00200000,
        .mpc = 2,
        .mrindex = 2,
    }, {
        .name = "ssram-0-alias",
        .base = 0x00400000,
        .size = 0x00400000,
        .mpc = 0,
        .mrindex = 3,
        .flags = IS_ALIAS,
    }, {
        /* Use the largest bit of contiguous RAM as our "system memory" */
        .name = "mps.ram",
        .base = 0x80000000,
        .size = 16 * MiB,
        .mpc = -1,
        .mrindex = -1,
    }, {
        .name = NULL,
    },
};

/*
 * Note that the addresses and MPC numbering here should match up
 * with those used in remap_memory(), which can swap the BRAM and QSPI.
 */
static const RAMInfo an524_raminfo[] = { {
        .name = "bram",
        .base = 0x00000000,
        .size = 512 * KiB,
        .mpc = 0,
        .mrindex = 0,
    }, {
        /* We don't model QSPI flash yet; for now expose it as simple ROM */
        .name = "QSPI",
        .base = 0x28000000,
        .size = 8 * MiB,
        .mpc = 1,
        .mrindex = 1,
        .flags = IS_ROM,
    }, {
        .name = "DDR",
        .base = 0x60000000,
        .size = MPS3_DDR_SIZE,
        .mpc = 2,
        .mrindex = -1,
    }, {
        .name = NULL,
    },
};

static const RAMInfo an547_raminfo[] = { {
        .name = "sram",
        .base = 0x01000000,
        .size = 2 * MiB,
        .mpc = 0,
        .mrindex = 1,
    }, {
        .name = "sram 2",
        .base = 0x21000000,
        .size = 4 * MiB,
        .mpc = -1,
        .mrindex = 3,
    }, {
        /* We don't model QSPI flash yet; for now expose it as simple ROM */
        .name = "QSPI",
        .base = 0x28000000,
        .size = 8 * MiB,
        .mpc = 1,
        .mrindex = 4,
        .flags = IS_ROM,
    }, {
        .name = "DDR",
        .base = 0x60000000,
        .size = MPS3_DDR_SIZE,
        .mpc = 2,
        .mrindex = -1,
    }, {
        .name = NULL,
    },
};

static const RAMInfo *find_raminfo_for_mpc(MPS2TZMachineState *mms, int mpc)
{
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);
    const RAMInfo *p;
    const RAMInfo *found = NULL;

    for (p = mmc->raminfo; p->name; p++) {
        if (p->mpc == mpc && !(p->flags & IS_ALIAS)) {
            /* There should only be one entry in the array for this MPC */
            g_assert(!found);
            found = p;
        }
    }
    /* if raminfo array doesn't have an entry for each MPC this is a bug */
    assert(found);
    return found;
}

static MemoryRegion *mr_for_raminfo(MPS2TZMachineState *mms,
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

    assert(raminfo->mrindex < MPS2TZ_RAM_MAX);
    ram = &mms->ram[raminfo->mrindex];

    memory_region_init_ram(ram, NULL, raminfo->name,
                           raminfo->size, &error_fatal);
    if (raminfo->flags & IS_ROM) {
        memory_region_set_readonly(ram, true);
    }
    return ram;
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

static qemu_irq get_sse_irq_in(MPS2TZMachineState *mms, int irqno)
{
    /*
     * Return a qemu_irq which will signal IRQ n to all CPUs in the
     * SSE.  The irqno should be as the CPU sees it, so the first
     * external-to-the-SSE interrupt is 32.
     */
    MachineClass *mc = MACHINE_GET_CLASS(mms);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);

    assert(irqno >= 32 && irqno < (mmc->numirq + 32));

    /*
     * Convert from "CPU irq number" (as listed in the FPGA image
     * documentation) to the SSE external-interrupt number.
     */
    irqno -= 32;

    if (mc->max_cpus > 1) {
        return qdev_get_gpio_in(DEVICE(&mms->cpu_irq_splitter[irqno]), 0);
    } else {
        return qdev_get_gpio_in_named(DEVICE(&mms->iotkit), "EXP_IRQ", irqno);
    }
}

/* Union describing the device-specific extra data we pass to the devfn. */
typedef union PPCExtraData {
    bool i2c_internal;
} PPCExtraData;

/* Most of the devices in the AN505 FPGA image sit behind
 * Peripheral Protection Controllers. These data structures
 * define the layout of which devices sit behind which PPCs.
 * The devfn for each port is a function which creates, configures
 * and initializes the device, returning the MemoryRegion which
 * needs to be plugged into the downstream end of the PPC port.
 */
typedef MemoryRegion *MakeDevFn(MPS2TZMachineState *mms, void *opaque,
                                const char *name, hwaddr size,
                                const int *irqs,
                                const PPCExtraData *extradata);

typedef struct PPCPortInfo {
    const char *name;
    MakeDevFn *devfn;
    void *opaque;
    hwaddr addr;
    hwaddr size;
    int irqs[3]; /* currently no device needs more IRQ lines than this */
    PPCExtraData extradata; /* to pass device-specific info to the devfn */
} PPCPortInfo;

typedef struct PPCInfo {
    const char *name;
    PPCPortInfo ports[TZ_NUM_PORTS];
} PPCInfo;

static MemoryRegion *make_unimp_dev(MPS2TZMachineState *mms,
                                    void *opaque,
                                    const char *name, hwaddr size,
                                    const int *irqs,
                                    const PPCExtraData *extradata)
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
                               const char *name, hwaddr size,
                               const int *irqs, const PPCExtraData *extradata)
{
    /* The irq[] array is tx, rx, combined, in that order */
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);
    CMSDKAPBUART *uart = opaque;
    int i = uart - &mms->uart[0];
    SysBusDevice *s;
    DeviceState *orgate_dev = DEVICE(&mms->uart_irq_orgate);

    object_initialize_child(OBJECT(mms), name, uart, TYPE_CMSDK_APB_UART);
    qdev_prop_set_chr(DEVICE(uart), "chardev", serial_hd(i));
    qdev_prop_set_uint32(DEVICE(uart), "pclk-frq", mmc->apb_periph_frq);
    sysbus_realize(SYS_BUS_DEVICE(uart), &error_fatal);
    s = SYS_BUS_DEVICE(uart);
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, irqs[0]));
    sysbus_connect_irq(s, 1, get_sse_irq_in(mms, irqs[1]));
    sysbus_connect_irq(s, 2, qdev_get_gpio_in(orgate_dev, i * 2));
    sysbus_connect_irq(s, 3, qdev_get_gpio_in(orgate_dev, i * 2 + 1));
    sysbus_connect_irq(s, 4, get_sse_irq_in(mms, irqs[2]));
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(uart), 0);
}

static MemoryRegion *make_scc(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size,
                              const int *irqs, const PPCExtraData *extradata)
{
    MPS2SCC *scc = opaque;
    DeviceState *sccdev;
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);
    QList *oscclk;
    uint32_t i;

    object_initialize_child(OBJECT(mms), "scc", scc, TYPE_MPS2_SCC);
    sccdev = DEVICE(scc);
    qdev_prop_set_uint32(sccdev, "scc-cfg0", mms->remap ? 1 : 0);
    qdev_prop_set_uint32(sccdev, "scc-cfg4", 0x2);
    qdev_prop_set_uint32(sccdev, "scc-aid", 0x00200008);
    qdev_prop_set_uint32(sccdev, "scc-id", mmc->scc_id);

    oscclk = qlist_new();
    for (i = 0; i < mmc->len_oscclk; i++) {
        qlist_append_int(oscclk, mmc->oscclk[i]);
    }
    qdev_prop_set_array(sccdev, "oscclk", oscclk);

    sysbus_realize(SYS_BUS_DEVICE(scc), &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(sccdev), 0);
}

static MemoryRegion *make_fpgaio(MPS2TZMachineState *mms, void *opaque,
                                 const char *name, hwaddr size,
                                 const int *irqs, const PPCExtraData *extradata)
{
    MPS2FPGAIO *fpgaio = opaque;
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);

    object_initialize_child(OBJECT(mms), "fpgaio", fpgaio, TYPE_MPS2_FPGAIO);
    qdev_prop_set_uint32(DEVICE(fpgaio), "num-leds", mmc->fpgaio_num_leds);
    qdev_prop_set_bit(DEVICE(fpgaio), "has-switches", mmc->fpgaio_has_switches);
    qdev_prop_set_bit(DEVICE(fpgaio), "has-dbgctrl", mmc->fpgaio_has_dbgctrl);
    sysbus_realize(SYS_BUS_DEVICE(fpgaio), &error_fatal);
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(fpgaio), 0);
}

static MemoryRegion *make_eth_dev(MPS2TZMachineState *mms, void *opaque,
                                  const char *name, hwaddr size,
                                  const int *irqs,
                                  const PPCExtraData *extradata)
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
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, irqs[0]));
    return sysbus_mmio_get_region(s, 0);
}

static MemoryRegion *make_eth_usb(MPS2TZMachineState *mms, void *opaque,
                                  const char *name, hwaddr size,
                                  const int *irqs,
                                  const PPCExtraData *extradata)
{
    /*
     * The AN524 makes the ethernet and USB share a PPC port.
     * irqs[] is the ethernet IRQ.
     */
    SysBusDevice *s;
    NICInfo *nd = &nd_table[0];

    memory_region_init(&mms->eth_usb_container, OBJECT(mms),
                       "mps2-tz-eth-usb-container", 0x200000);

    /*
     * In hardware this is a LAN9220; the LAN9118 is software compatible
     * except that it doesn't support the checksum-offload feature.
     */
    qemu_check_nic_model(nd, "lan9118");
    mms->lan9118 = qdev_new(TYPE_LAN9118);
    qdev_set_nic_properties(mms->lan9118, nd);

    s = SYS_BUS_DEVICE(mms->lan9118);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, irqs[0]));

    memory_region_add_subregion(&mms->eth_usb_container,
                                0, sysbus_mmio_get_region(s, 0));

    /* The USB OTG controller is an ISP1763; we don't have a model of it. */
    object_initialize_child(OBJECT(mms), "usb-otg",
                            &mms->usb, TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(DEVICE(&mms->usb), "name", "usb-otg");
    qdev_prop_set_uint64(DEVICE(&mms->usb), "size", 0x100000);
    s = SYS_BUS_DEVICE(&mms->usb);
    sysbus_realize(s, &error_fatal);

    memory_region_add_subregion(&mms->eth_usb_container,
                                0x100000, sysbus_mmio_get_region(s, 0));

    return &mms->eth_usb_container;
}

static MemoryRegion *make_mpc(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size,
                              const int *irqs, const PPCExtraData *extradata)
{
    TZMPC *mpc = opaque;
    int i = mpc - &mms->mpc[0];
    MemoryRegion *upstream;
    const RAMInfo *raminfo = find_raminfo_for_mpc(mms, i);
    MemoryRegion *ram = mr_for_raminfo(mms, raminfo);

    object_initialize_child(OBJECT(mms), name, mpc, TYPE_TZ_MPC);
    object_property_set_link(OBJECT(mpc), "downstream", OBJECT(ram),
                             &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(mpc), &error_fatal);
    /* Map the upstream end of the MPC into system memory */
    upstream = sysbus_mmio_get_region(SYS_BUS_DEVICE(mpc), 1);
    memory_region_add_subregion(get_system_memory(), raminfo->base, upstream);
    /* and connect its interrupt to the IoTKit */
    qdev_connect_gpio_out_named(DEVICE(mpc), "irq", 0,
                                qdev_get_gpio_in_named(DEVICE(&mms->iotkit),
                                                       "mpcexp_status", i));

    /* Return the register interface MR for our caller to map behind the PPC */
    return sysbus_mmio_get_region(SYS_BUS_DEVICE(mpc), 0);
}

static hwaddr boot_mem_base(MPS2TZMachineState *mms)
{
    /*
     * Return the canonical address of the block which will be mapped
     * at address 0x0 (i.e. where the vector table is).
     * This is usually 0, but if the AN524 alternate memory map is
     * enabled it will be the base address of the QSPI block.
     */
    return mms->remap ? 0x28000000 : 0;
}

static void remap_memory(MPS2TZMachineState *mms, int map)
{
    /*
     * Remap the memory for the AN524. 'map' is the value of
     * SCC CFG_REG0 bit 0, i.e. 0 for the default map and 1
     * for the "option 1" mapping where QSPI is at address 0.
     *
     * Effectively we need to swap around the "upstream" ends of
     * MPC 0 and MPC 1.
     */
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);
    int i;

    if (mmc->fpga_type != FPGA_AN524) {
        return;
    }

    memory_region_transaction_begin();
    for (i = 0; i < 2; i++) {
        TZMPC *mpc = &mms->mpc[i];
        MemoryRegion *upstream = sysbus_mmio_get_region(SYS_BUS_DEVICE(mpc), 1);
        hwaddr addr = (i ^ map) ? 0x28000000 : 0;

        memory_region_set_address(upstream, addr);
    }
    memory_region_transaction_commit();
}

static void remap_irq_fn(void *opaque, int n, int level)
{
    MPS2TZMachineState *mms = opaque;

    remap_memory(mms, level);
}

static MemoryRegion *make_dma(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size,
                              const int *irqs, const PPCExtraData *extradata)
{
    /* The irq[] array is DMACINTR, DMACINTERR, DMACINTTC, in that order */
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
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, irqs[0]));
    sysbus_connect_irq(s, 1, get_sse_irq_in(mms, irqs[1]));
    sysbus_connect_irq(s, 2, get_sse_irq_in(mms, irqs[2]));

    g_free(mscname);
    return sysbus_mmio_get_region(s, 0);
}

static MemoryRegion *make_spi(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size,
                              const int *irqs, const PPCExtraData *extradata)
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
    SysBusDevice *s;

    object_initialize_child(OBJECT(mms), name, spi, TYPE_PL022);
    sysbus_realize(SYS_BUS_DEVICE(spi), &error_fatal);
    s = SYS_BUS_DEVICE(spi);
    sysbus_connect_irq(s, 0, get_sse_irq_in(mms, irqs[0]));
    return sysbus_mmio_get_region(s, 0);
}

static MemoryRegion *make_i2c(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size,
                              const int *irqs, const PPCExtraData *extradata)
{
    ArmSbconI2CState *i2c = opaque;
    SysBusDevice *s;

    object_initialize_child(OBJECT(mms), name, i2c, TYPE_ARM_SBCON_I2C);
    s = SYS_BUS_DEVICE(i2c);
    sysbus_realize(s, &error_fatal);

    /*
     * If this is an internal-use-only i2c bus, mark it full
     * so that user-created i2c devices are not plugged into it.
     * If we implement models of any on-board i2c devices that
     * plug in to one of the internal-use-only buses, then we will
     * need to create and plugging those in here before we mark the
     * bus as full.
     */
    if (extradata->i2c_internal) {
        BusState *qbus = qdev_get_child_bus(DEVICE(i2c), "i2c");
        qbus_mark_full(qbus);
    }

    return sysbus_mmio_get_region(s, 0);
}

static MemoryRegion *make_rtc(MPS2TZMachineState *mms, void *opaque,
                              const char *name, hwaddr size,
                              const int *irqs, const PPCExtraData *extradata)
{
    PL031State *pl031 = opaque;
    SysBusDevice *s;

    object_initialize_child(OBJECT(mms), name, pl031, TYPE_PL031);
    s = SYS_BUS_DEVICE(pl031);
    sysbus_realize(s, &error_fatal);
    /*
     * The board docs don't give an IRQ number for the PL031, so
     * presumably it is not connected.
     */
    return sysbus_mmio_get_region(s, 0);
}

static void create_non_mpc_ram(MPS2TZMachineState *mms)
{
    /*
     * Handle the RAMs which are either not behind MPCs or which are
     * aliases to another MPC.
     */
    const RAMInfo *p;
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);

    for (p = mmc->raminfo; p->name; p++) {
        if (p->flags & IS_ALIAS) {
            SysBusDevice *mpc_sbd = SYS_BUS_DEVICE(&mms->mpc[p->mpc]);
            MemoryRegion *upstream = sysbus_mmio_get_region(mpc_sbd, 1);
            make_ram_alias(&mms->ram[p->mrindex], p->name, upstream, p->base);
        } else if (p->mpc == -1) {
            /* RAM not behind an MPC */
            MemoryRegion *mr = mr_for_raminfo(mms, p);
            memory_region_add_subregion(get_system_memory(), p->base, mr);
        }
    }
}

static uint32_t boot_ram_size(MPS2TZMachineState *mms)
{
    /* Return the size of the RAM block at guest address zero */
    const RAMInfo *p;
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);

    /*
     * Use a per-board specification (for when the boot RAM is in
     * the SSE and so doesn't have a RAMInfo list entry)
     */
    if (mmc->boot_ram_size) {
        return mmc->boot_ram_size;
    }

    for (p = mmc->raminfo; p->name; p++) {
        if (p->base == boot_mem_base(mms)) {
            return p->size;
        }
    }
    g_assert_not_reached();
}

static void mps2tz_common_init(MachineState *machine)
{
    MPS2TZMachineState *mms = MPS2TZ_MACHINE(machine);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_GET_CLASS(mms);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *iotkitdev;
    DeviceState *dev_splitter;
    const PPCInfo *ppcs;
    int num_ppcs;
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
    qdev_prop_set_uint32(iotkitdev, "EXP_NUMIRQ", mmc->numirq);
    qdev_prop_set_uint32(iotkitdev, "init-svtor", mmc->init_svtor);
    if (mmc->cpu0_mpu_ns != MPU_REGION_DEFAULT) {
        qdev_prop_set_uint32(iotkitdev, "CPU0_MPU_NS", mmc->cpu0_mpu_ns);
    }
    if (mmc->cpu0_mpu_s != MPU_REGION_DEFAULT) {
        qdev_prop_set_uint32(iotkitdev, "CPU0_MPU_S", mmc->cpu0_mpu_s);
    }
    if (object_property_find(OBJECT(iotkitdev), "CPU1_MPU_NS")) {
        if (mmc->cpu1_mpu_ns != MPU_REGION_DEFAULT) {
            qdev_prop_set_uint32(iotkitdev, "CPU1_MPU_NS", mmc->cpu1_mpu_ns);
        }
        if (mmc->cpu1_mpu_s != MPU_REGION_DEFAULT) {
            qdev_prop_set_uint32(iotkitdev, "CPU1_MPU_S", mmc->cpu1_mpu_s);
        }
    }
    qdev_prop_set_uint32(iotkitdev, "SRAM_ADDR_WIDTH", mmc->sram_addr_width);
    qdev_connect_clock_in(iotkitdev, "MAINCLK", mms->sysclk);
    qdev_connect_clock_in(iotkitdev, "S32KCLK", mms->s32kclk);
    sysbus_realize(SYS_BUS_DEVICE(&mms->iotkit), &error_fatal);

    /*
     * If this board has more than one CPU, then we need to create splitters
     * to feed the IRQ inputs for each CPU in the SSE from each device in the
     * board. If there is only one CPU, we can just wire the device IRQ
     * directly to the SSE's IRQ input.
     */
    assert(mmc->numirq <= MPS2TZ_NUMIRQ_MAX);
    if (mc->max_cpus > 1) {
        for (i = 0; i < mmc->numirq; i++) {
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

    /*
     * The IoTKit sets up much of the memory layout, including
     * the aliases between secure and non-secure regions in the
     * address space, and also most of the devices in the system.
     * The FPGA itself contains various RAMs and some additional devices.
     * The FPGA images have an odd combination of different RAMs,
     * because in hardware they are different implementations and
     * connected to different buses, giving varying performance/size
     * tradeoffs. For QEMU they're all just RAM, though. We arbitrarily
     * call the largest lump our "system memory".
     */

    /*
     * The overflow IRQs for all UARTs are ORed together.
     * Tx, Rx and "combined" IRQs are sent to the NVIC separately.
     * Create the OR gate for this: it has one input for the TX overflow
     * and one for the RX overflow for each UART we might have.
     * (If the board has fewer than the maximum possible number of UARTs
     * those inputs are never wired up and are treated as always-zero.)
     */
    object_initialize_child(OBJECT(mms), "uart-irq-orgate",
                            &mms->uart_irq_orgate, TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&mms->uart_irq_orgate), "num-lines",
                            2 * ARRAY_SIZE(mms->uart),
                            &error_fatal);
    qdev_realize(DEVICE(&mms->uart_irq_orgate), NULL, &error_fatal);
    qdev_connect_gpio_out(DEVICE(&mms->uart_irq_orgate), 0,
                          get_sse_irq_in(mms, mmc->uart_overflow_irq));

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

    const PPCInfo an505_ppcs[] = { {
            .name = "apb_ppcexp0",
            .ports = {
                { "ssram-0-mpc", make_mpc, &mms->mpc[0], 0x58007000, 0x1000 },
                { "ssram-1-mpc", make_mpc, &mms->mpc[1], 0x58008000, 0x1000 },
                { "ssram-2-mpc", make_mpc, &mms->mpc[2], 0x58009000, 0x1000 },
            },
        }, {
            .name = "apb_ppcexp1",
            .ports = {
                { "spi0", make_spi, &mms->spi[0], 0x40205000, 0x1000, { 51 } },
                { "spi1", make_spi, &mms->spi[1], 0x40206000, 0x1000, { 52 } },
                { "spi2", make_spi, &mms->spi[2], 0x40209000, 0x1000, { 53 } },
                { "spi3", make_spi, &mms->spi[3], 0x4020a000, 0x1000, { 54 } },
                { "spi4", make_spi, &mms->spi[4], 0x4020b000, 0x1000, { 55 } },
                { "uart0", make_uart, &mms->uart[0], 0x40200000, 0x1000, { 32, 33, 42 } },
                { "uart1", make_uart, &mms->uart[1], 0x40201000, 0x1000, { 34, 35, 43 } },
                { "uart2", make_uart, &mms->uart[2], 0x40202000, 0x1000, { 36, 37, 44 } },
                { "uart3", make_uart, &mms->uart[3], 0x40203000, 0x1000, { 38, 39, 45 } },
                { "uart4", make_uart, &mms->uart[4], 0x40204000, 0x1000, { 40, 41, 46 } },
                { "i2c0", make_i2c, &mms->i2c[0], 0x40207000, 0x1000, {},
                  { .i2c_internal = true /* touchscreen */ } },
                { "i2c1", make_i2c, &mms->i2c[1], 0x40208000, 0x1000, {},
                  { .i2c_internal = true /* audio conf */ } },
                { "i2c2", make_i2c, &mms->i2c[2], 0x4020c000, 0x1000, {},
                  { .i2c_internal = false /* shield 0 */ } },
                { "i2c3", make_i2c, &mms->i2c[3], 0x4020d000, 0x1000, {},
                  { .i2c_internal = false /* shield 1 */ } },
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
                { "eth", make_eth_dev, NULL, 0x42000000, 0x100000, { 48 } },
            },
        }, {
            .name = "ahb_ppcexp1",
            .ports = {
                { "dma0", make_dma, &mms->dma[0], 0x40110000, 0x1000, { 58, 56, 57 } },
                { "dma1", make_dma, &mms->dma[1], 0x40111000, 0x1000, { 61, 59, 60 } },
                { "dma2", make_dma, &mms->dma[2], 0x40112000, 0x1000, { 64, 62, 63 } },
                { "dma3", make_dma, &mms->dma[3], 0x40113000, 0x1000, { 67, 65, 66 } },
            },
        },
    };

    const PPCInfo an524_ppcs[] = { {
            .name = "apb_ppcexp0",
            .ports = {
                { "bram-mpc", make_mpc, &mms->mpc[0], 0x58007000, 0x1000 },
                { "qspi-mpc", make_mpc, &mms->mpc[1], 0x58008000, 0x1000 },
                { "ddr-mpc", make_mpc, &mms->mpc[2], 0x58009000, 0x1000 },
            },
        }, {
            .name = "apb_ppcexp1",
            .ports = {
                { "i2c0", make_i2c, &mms->i2c[0], 0x41200000, 0x1000, {},
                  { .i2c_internal = true /* touchscreen */ } },
                { "i2c1", make_i2c, &mms->i2c[1], 0x41201000, 0x1000, {},
                  { .i2c_internal = true /* audio conf */ } },
                { "spi0", make_spi, &mms->spi[0], 0x41202000, 0x1000, { 52 } },
                { "spi1", make_spi, &mms->spi[1], 0x41203000, 0x1000, { 53 } },
                { "spi2", make_spi, &mms->spi[2], 0x41204000, 0x1000, { 54 } },
                { "i2c2", make_i2c, &mms->i2c[2], 0x41205000, 0x1000, {},
                  { .i2c_internal = false /* shield 0 */ } },
                { "i2c3", make_i2c, &mms->i2c[3], 0x41206000, 0x1000, {},
                  { .i2c_internal = false /* shield 1 */ } },
                { /* port 7 reserved */ },
                { "i2c4", make_i2c, &mms->i2c[4], 0x41208000, 0x1000, {},
                  { .i2c_internal = true /* DDR4 EEPROM */ } },
            },
        }, {
            .name = "apb_ppcexp2",
            .ports = {
                { "scc", make_scc, &mms->scc, 0x41300000, 0x1000 },
                { "i2s-audio", make_unimp_dev, &mms->i2s_audio,
                  0x41301000, 0x1000 },
                { "fpgaio", make_fpgaio, &mms->fpgaio, 0x41302000, 0x1000 },
                { "uart0", make_uart, &mms->uart[0], 0x41303000, 0x1000, { 32, 33, 42 } },
                { "uart1", make_uart, &mms->uart[1], 0x41304000, 0x1000, { 34, 35, 43 } },
                { "uart2", make_uart, &mms->uart[2], 0x41305000, 0x1000, { 36, 37, 44 } },
                { "uart3", make_uart, &mms->uart[3], 0x41306000, 0x1000, { 38, 39, 45 } },
                { "uart4", make_uart, &mms->uart[4], 0x41307000, 0x1000, { 40, 41, 46 } },
                { "uart5", make_uart, &mms->uart[5], 0x41308000, 0x1000, { 124, 125, 126 } },

                { /* port 9 reserved */ },
                { "clcd", make_unimp_dev, &mms->cldc, 0x4130a000, 0x1000 },
                { "rtc", make_rtc, &mms->rtc, 0x4130b000, 0x1000 },
            },
        }, {
            .name = "ahb_ppcexp0",
            .ports = {
                { "gpio0", make_unimp_dev, &mms->gpio[0], 0x41100000, 0x1000 },
                { "gpio1", make_unimp_dev, &mms->gpio[1], 0x41101000, 0x1000 },
                { "gpio2", make_unimp_dev, &mms->gpio[2], 0x41102000, 0x1000 },
                { "gpio3", make_unimp_dev, &mms->gpio[3], 0x41103000, 0x1000 },
                { "eth-usb", make_eth_usb, NULL, 0x41400000, 0x200000, { 48 } },
            },
        },
    };

    const PPCInfo an547_ppcs[] = { {
            .name = "apb_ppcexp0",
            .ports = {
                { "ssram-mpc", make_mpc, &mms->mpc[0], 0x57000000, 0x1000 },
                { "qspi-mpc", make_mpc, &mms->mpc[1], 0x57001000, 0x1000 },
                { "ddr-mpc", make_mpc, &mms->mpc[2], 0x57002000, 0x1000 },
            },
        }, {
            .name = "apb_ppcexp1",
            .ports = {
                { "i2c0", make_i2c, &mms->i2c[0], 0x49200000, 0x1000, {},
                  { .i2c_internal = true /* touchscreen */ } },
                { "i2c1", make_i2c, &mms->i2c[1], 0x49201000, 0x1000, {},
                  { .i2c_internal = true /* audio conf */ } },
                { "spi0", make_spi, &mms->spi[0], 0x49202000, 0x1000, { 53 } },
                { "spi1", make_spi, &mms->spi[1], 0x49203000, 0x1000, { 54 } },
                { "spi2", make_spi, &mms->spi[2], 0x49204000, 0x1000, { 55 } },
                { "i2c2", make_i2c, &mms->i2c[2], 0x49205000, 0x1000, {},
                  { .i2c_internal = false /* shield 0 */ } },
                { "i2c3", make_i2c, &mms->i2c[3], 0x49206000, 0x1000, {},
                  { .i2c_internal = false /* shield 1 */ } },
                { /* port 7 reserved */ },
                { "i2c4", make_i2c, &mms->i2c[4], 0x49208000, 0x1000, {},
                  { .i2c_internal = true /* DDR4 EEPROM */ } },
            },
        }, {
            .name = "apb_ppcexp2",
            .ports = {
                { "scc", make_scc, &mms->scc, 0x49300000, 0x1000 },
                { "i2s-audio", make_unimp_dev, &mms->i2s_audio, 0x49301000, 0x1000 },
                { "fpgaio", make_fpgaio, &mms->fpgaio, 0x49302000, 0x1000 },
                { "uart0", make_uart, &mms->uart[0], 0x49303000, 0x1000, { 33, 34, 43 } },
                { "uart1", make_uart, &mms->uart[1], 0x49304000, 0x1000, { 35, 36, 44 } },
                { "uart2", make_uart, &mms->uart[2], 0x49305000, 0x1000, { 37, 38, 45 } },
                { "uart3", make_uart, &mms->uart[3], 0x49306000, 0x1000, { 39, 40, 46 } },
                { "uart4", make_uart, &mms->uart[4], 0x49307000, 0x1000, { 41, 42, 47 } },
                { "uart5", make_uart, &mms->uart[5], 0x49308000, 0x1000, { 125, 126, 127 } },

                { /* port 9 reserved */ },
                { "clcd", make_unimp_dev, &mms->cldc, 0x4930a000, 0x1000 },
                { "rtc", make_rtc, &mms->rtc, 0x4930b000, 0x1000 },
            },
        }, {
            .name = "ahb_ppcexp0",
            .ports = {
                { "gpio0", make_unimp_dev, &mms->gpio[0], 0x41100000, 0x1000 },
                { "gpio1", make_unimp_dev, &mms->gpio[1], 0x41101000, 0x1000 },
                { "gpio2", make_unimp_dev, &mms->gpio[2], 0x41102000, 0x1000 },
                { "gpio3", make_unimp_dev, &mms->gpio[3], 0x41103000, 0x1000 },
                { /* port 4 USER AHB interface 0 */ },
                { /* port 5 USER AHB interface 1 */ },
                { /* port 6 USER AHB interface 2 */ },
                { /* port 7 USER AHB interface 3 */ },
                { "eth-usb", make_eth_usb, NULL, 0x41400000, 0x200000, { 49 } },
            },
        },
    };

    switch (mmc->fpga_type) {
    case FPGA_AN505:
    case FPGA_AN521:
        ppcs = an505_ppcs;
        num_ppcs = ARRAY_SIZE(an505_ppcs);
        break;
    case FPGA_AN524:
        ppcs = an524_ppcs;
        num_ppcs = ARRAY_SIZE(an524_ppcs);
        break;
    case FPGA_AN547:
        ppcs = an547_ppcs;
        num_ppcs = ARRAY_SIZE(an547_ppcs);
        break;
    default:
        g_assert_not_reached();
    }

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

            mr = pinfo->devfn(mms, pinfo->opaque, pinfo->name, pinfo->size,
                              pinfo->irqs, &pinfo->extradata);
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

    if (mmc->fpga_type == FPGA_AN547) {
        create_unimplemented_device("U55 timing adapter 0", 0x48102000, 0x1000);
        create_unimplemented_device("U55 timing adapter 1", 0x48103000, 0x1000);
    }

    create_non_mpc_ram(mms);

    if (mmc->fpga_type == FPGA_AN524) {
        /*
         * Connect the line from the SCC so that we can remap when the
         * guest updates that register.
         */
        mms->remap_irq = qemu_allocate_irq(remap_irq_fn, mms, 0);
        qdev_connect_gpio_out_named(DEVICE(&mms->scc), "remap", 0,
                                    mms->remap_irq);
    }

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       0, boot_ram_size(mms));
}

static void mps2_tz_idau_check(IDAUInterface *ii, uint32_t address,
                               int *iregion, bool *exempt, bool *ns, bool *nsc)
{
    /*
     * The MPS2 TZ FPGA images have IDAUs in them which are connected to
     * the Master Security Controllers. These have the same logic as
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

static char *mps2_get_remap(Object *obj, Error **errp)
{
    MPS2TZMachineState *mms = MPS2TZ_MACHINE(obj);
    const char *val = mms->remap ? "QSPI" : "BRAM";
    return g_strdup(val);
}

static void mps2_set_remap(Object *obj, const char *value, Error **errp)
{
    MPS2TZMachineState *mms = MPS2TZ_MACHINE(obj);

    if (!strcmp(value, "BRAM")) {
        mms->remap = false;
    } else if (!strcmp(value, "QSPI")) {
        mms->remap = true;
    } else {
        error_setg(errp, "Invalid remap value");
        error_append_hint(errp, "Valid values are BRAM and QSPI.\n");
    }
}

static void mps2_machine_reset(MachineState *machine, ShutdownCause reason)
{
    MPS2TZMachineState *mms = MPS2TZ_MACHINE(machine);

    /*
     * Set the initial memory mapping before triggering the reset of
     * the rest of the system, so that the guest image loader and CPU
     * reset see the correct mapping.
     */
    remap_memory(mms, mms->remap);
    qemu_devices_reset(reason);
}

static void mps2tz_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    IDAUInterfaceClass *iic = IDAU_INTERFACE_CLASS(oc);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_CLASS(oc);

    mc->init = mps2tz_common_init;
    mc->reset = mps2_machine_reset;
    iic->check = mps2_tz_idau_check;

    /* Most machines leave these at the SSE defaults */
    mmc->cpu0_mpu_ns = MPU_REGION_DEFAULT;
    mmc->cpu0_mpu_s = MPU_REGION_DEFAULT;
    mmc->cpu1_mpu_ns = MPU_REGION_DEFAULT;
    mmc->cpu1_mpu_s = MPU_REGION_DEFAULT;
}

static void mps2tz_set_default_ram_info(MPS2TZMachineClass *mmc)
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
            return;
        }
    }
    g_assert_not_reached();
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
    mmc->apb_periph_frq = mmc->sysclk_frq;
    mmc->oscclk = an505_oscclk;
    mmc->len_oscclk = ARRAY_SIZE(an505_oscclk);
    mmc->fpgaio_num_leds = 2;
    mmc->fpgaio_has_switches = false;
    mmc->fpgaio_has_dbgctrl = false;
    mmc->numirq = 92;
    mmc->uart_overflow_irq = 47;
    mmc->init_svtor = 0x10000000;
    mmc->sram_addr_width = 15;
    mmc->raminfo = an505_raminfo;
    mmc->armsse_type = TYPE_IOTKIT;
    mmc->boot_ram_size = 0;
    mps2tz_set_default_ram_info(mmc);
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
    mmc->apb_periph_frq = mmc->sysclk_frq;
    mmc->oscclk = an505_oscclk; /* AN521 is the same as AN505 here */
    mmc->len_oscclk = ARRAY_SIZE(an505_oscclk);
    mmc->fpgaio_num_leds = 2;
    mmc->fpgaio_has_switches = false;
    mmc->fpgaio_has_dbgctrl = false;
    mmc->numirq = 92;
    mmc->uart_overflow_irq = 47;
    mmc->init_svtor = 0x10000000;
    mmc->sram_addr_width = 15;
    mmc->raminfo = an505_raminfo; /* AN521 is the same as AN505 here */
    mmc->armsse_type = TYPE_SSE200;
    mmc->boot_ram_size = 0;
    mps2tz_set_default_ram_info(mmc);
}

static void mps3tz_an524_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS3 with AN524 FPGA image for dual Cortex-M33";
    mc->default_cpus = 2;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mmc->fpga_type = FPGA_AN524;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m33");
    mmc->scc_id = 0x41045240;
    mmc->sysclk_frq = 32 * 1000 * 1000; /* 32MHz */
    mmc->apb_periph_frq = mmc->sysclk_frq;
    mmc->oscclk = an524_oscclk;
    mmc->len_oscclk = ARRAY_SIZE(an524_oscclk);
    mmc->fpgaio_num_leds = 10;
    mmc->fpgaio_has_switches = true;
    mmc->fpgaio_has_dbgctrl = false;
    mmc->numirq = 95;
    mmc->uart_overflow_irq = 47;
    mmc->init_svtor = 0x10000000;
    mmc->sram_addr_width = 15;
    mmc->raminfo = an524_raminfo;
    mmc->armsse_type = TYPE_SSE200;
    mmc->boot_ram_size = 0;
    mps2tz_set_default_ram_info(mmc);

    object_class_property_add_str(oc, "remap", mps2_get_remap, mps2_set_remap);
    object_class_property_set_description(oc, "remap",
                                          "Set memory mapping. Valid values "
                                          "are BRAM (default) and QSPI.");
}

static void mps3tz_an547_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS2TZMachineClass *mmc = MPS2TZ_MACHINE_CLASS(oc);

    mc->desc = "ARM MPS3 with AN547 FPGA image for Cortex-M55";
    mc->default_cpus = 1;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mmc->fpga_type = FPGA_AN547;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m55");
    mmc->scc_id = 0x41055470;
    mmc->sysclk_frq = 32 * 1000 * 1000; /* 32MHz */
    mmc->apb_periph_frq = 25 * 1000 * 1000; /* 25MHz */
    mmc->oscclk = an524_oscclk; /* same as AN524 */
    mmc->len_oscclk = ARRAY_SIZE(an524_oscclk);
    mmc->fpgaio_num_leds = 10;
    mmc->fpgaio_has_switches = true;
    mmc->fpgaio_has_dbgctrl = true;
    mmc->numirq = 96;
    mmc->uart_overflow_irq = 48;
    mmc->init_svtor = 0x00000000;
    mmc->cpu0_mpu_s = mmc->cpu0_mpu_ns = 16;
    mmc->sram_addr_width = 21;
    mmc->raminfo = an547_raminfo;
    mmc->armsse_type = TYPE_SSE300;
    mmc->boot_ram_size = 512 * KiB;
    mps2tz_set_default_ram_info(mmc);
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

static const TypeInfo mps3tz_an524_info = {
    .name = TYPE_MPS3TZ_AN524_MACHINE,
    .parent = TYPE_MPS2TZ_MACHINE,
    .class_init = mps3tz_an524_class_init,
};

static const TypeInfo mps3tz_an547_info = {
    .name = TYPE_MPS3TZ_AN547_MACHINE,
    .parent = TYPE_MPS2TZ_MACHINE,
    .class_init = mps3tz_an547_class_init,
};

static void mps2tz_machine_init(void)
{
    type_register_static(&mps2tz_info);
    type_register_static(&mps2tz_an505_info);
    type_register_static(&mps2tz_an521_info);
    type_register_static(&mps3tz_an524_info);
    type_register_static(&mps3tz_an547_info);
}

type_init(mps2tz_machine_init);
