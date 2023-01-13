/*
 * ARM SSE (Subsystems for Embedded): IoTKit, SSE-200
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/*
 * This is a model of the Arm "Subsystems for Embedded" family of
 * hardware, which include the IoT Kit and the SSE-050, SSE-100 and
 * SSE-200. Currently we model:
 *  - the Arm IoT Kit which is documented in
 *    https://developer.arm.com/documentation/ecm0601256/latest
 *  - the SSE-200 which is documented in
 *    https://developer.arm.com/documentation/101104/latest/
 *
 * The IoTKit contains:
 *  a Cortex-M33
 *  the IDAU
 *  some timers and watchdogs
 *  two peripheral protection controllers
 *  a memory protection controller
 *  a security controller
 *  a bus fabric which arranges that some parts of the address
 *  space are secure and non-secure aliases of each other
 * The SSE-200 additionally contains:
 *  a second Cortex-M33
 *  two Message Handling Units (MHUs)
 *  an optional CryptoCell (which we do not model)
 *  more SRAM banks with associated MPCs
 *  multiple Power Policy Units (PPUs)
 *  a control interface for an icache for each CPU
 *  per-CPU identity and control register blocks
 *
 * QEMU interface:
 *  + Clock input "MAINCLK": clock for CPUs and most peripherals
 *  + Clock input "S32KCLK": slow 32KHz clock used for a few peripherals
 *  + QOM property "memory" is a MemoryRegion containing the devices provided
 *    by the board model.
 *  + QOM property "EXP_NUMIRQ" sets the number of expansion interrupts.
 *    (In hardware, the SSE-200 permits the number of expansion interrupts
 *    for the two CPUs to be configured separately, but we restrict it to
 *    being the same for both, to avoid having to have separate Property
 *    lists for different variants. This restriction can be relaxed later
 *    if necessary.)
 *  + QOM property "SRAM_ADDR_WIDTH" sets the number of bits used for the
 *    address of each SRAM bank (and thus the total amount of internal SRAM)
 *  + QOM property "init-svtor" sets the initial value of the CPU SVTOR register
 *    (where it expects to load the PC and SP from the vector table on reset)
 *  + QOM properties "CPU0_FPU", "CPU0_DSP", "CPU1_FPU" and "CPU1_DSP" which
 *    set whether the CPUs have the FPU and DSP features present. The default
 *    (matching the hardware) is that for CPU0 in an IoTKit and CPU1 in an
 *    SSE-200 both are present; CPU0 in an SSE-200 has neither.
 *    Since the IoTKit has only one CPU, it does not have the CPU1_* properties.
 *  + Named GPIO inputs "EXP_IRQ" 0..n are the expansion interrupts for CPU 0,
 *    which are wired to its NVIC lines 32 .. n+32
 *  + Named GPIO inputs "EXP_CPU1_IRQ" 0..n are the expansion interrupts for
 *    CPU 1, which are wired to its NVIC lines 32 .. n+32
 *  + sysbus MMIO region 0 is the "AHB Slave Expansion" which allows
 *    bus master devices in the board model to make transactions into
 *    all the devices and memory areas in the IoTKit
 * Controlling up to 4 AHB expansion PPBs which a system using the IoTKit
 * might provide:
 *  + named GPIO outputs apb_ppcexp{0,1,2,3}_nonsec[0..15]
 *  + named GPIO outputs apb_ppcexp{0,1,2,3}_ap[0..15]
 *  + named GPIO outputs apb_ppcexp{0,1,2,3}_irq_enable
 *  + named GPIO outputs apb_ppcexp{0,1,2,3}_irq_clear
 *  + named GPIO inputs apb_ppcexp{0,1,2,3}_irq_status
 * Controlling each of the 4 expansion AHB PPCs which a system using the IoTKit
 * might provide:
 *  + named GPIO outputs ahb_ppcexp{0,1,2,3}_nonsec[0..15]
 *  + named GPIO outputs ahb_ppcexp{0,1,2,3}_ap[0..15]
 *  + named GPIO outputs ahb_ppcexp{0,1,2,3}_irq_enable
 *  + named GPIO outputs ahb_ppcexp{0,1,2,3}_irq_clear
 *  + named GPIO inputs ahb_ppcexp{0,1,2,3}_irq_status
 * Controlling each of the 16 expansion MPCs which a system using the IoTKit
 * might provide:
 *  + named GPIO inputs mpcexp_status[0..15]
 * Controlling each of the 16 expansion MSCs which a system using the IoTKit
 * might provide:
 *  + named GPIO inputs mscexp_status[0..15]
 *  + named GPIO outputs mscexp_clear[0..15]
 *  + named GPIO outputs mscexp_ns[0..15]
 */

#ifndef ARMSSE_H
#define ARMSSE_H

#include "hw/sysbus.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/iotkit-secctl.h"
#include "hw/misc/tz-ppc.h"
#include "hw/misc/tz-mpc.h"
#include "hw/timer/cmsdk-apb-timer.h"
#include "hw/timer/cmsdk-apb-dualtimer.h"
#include "hw/timer/sse-counter.h"
#include "hw/timer/sse-timer.h"
#include "hw/watchdog/cmsdk-apb-watchdog.h"
#include "hw/misc/iotkit-sysctl.h"
#include "hw/misc/iotkit-sysinfo.h"
#include "hw/misc/armsse-cpuid.h"
#include "hw/misc/armsse-mhu.h"
#include "hw/misc/armsse-cpu-pwrctrl.h"
#include "hw/misc/unimp.h"
#include "hw/or-irq.h"
#include "hw/clock.h"
#include "hw/core/split-irq.h"
#include "hw/cpu/cluster.h"
#include "qom/object.h"

#define TYPE_ARM_SSE "arm-sse"
OBJECT_DECLARE_TYPE(ARMSSE, ARMSSEClass,
                    ARM_SSE)

/*
 * These type names are for specific IoTKit subsystems; other than
 * instantiating them, code using these devices should always handle
 * them via the ARMSSE base class, so they have no IOTKIT() etc macros.
 */
#define TYPE_IOTKIT "iotkit"
#define TYPE_SSE200 "sse-200"
#define TYPE_SSE300 "sse-300"

/* We have an IRQ splitter and an OR gate input for each external PPC
 * and the 2 internal PPCs
 */
#define NUM_INTERNAL_PPCS 2
#define NUM_EXTERNAL_PPCS (IOTS_NUM_AHB_EXP_PPC + IOTS_NUM_APB_EXP_PPC)
#define NUM_PPCS (NUM_EXTERNAL_PPCS + NUM_INTERNAL_PPCS)

#define MAX_SRAM_BANKS 4
#if MAX_SRAM_BANKS > IOTS_NUM_MPC
#error Too many SRAM banks
#endif

#define SSE_MAX_CPUS 2

#define NUM_PPUS 8

/* Number of CPU IRQs used by the SSE itself */
#define NUM_SSE_IRQS 32

struct ARMSSE {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMv7MState armv7m[SSE_MAX_CPUS];
    CPUClusterState cluster[SSE_MAX_CPUS];
    IoTKitSecCtl secctl;
    TZPPC apb_ppc[NUM_INTERNAL_PPCS];
    TZMPC mpc[IOTS_NUM_MPC];
    CMSDKAPBTimer timer[3];
    OrIRQState ppc_irq_orgate;
    SplitIRQ sec_resp_splitter;
    SplitIRQ ppc_irq_splitter[NUM_PPCS];
    SplitIRQ mpc_irq_splitter[IOTS_NUM_EXP_MPC + IOTS_NUM_MPC];
    OrIRQState mpc_irq_orgate;
    OrIRQState nmi_orgate;

    SplitIRQ cpu_irq_splitter[NUM_SSE_IRQS];

    CMSDKAPBDualTimer dualtimer;

    CMSDKAPBWatchdog cmsdk_watchdog[3];

    SSECounter sse_counter;
    SSETimer sse_timer[4];

    IoTKitSysCtl sysctl;
    IoTKitSysCtl sysinfo;

    ARMSSEMHU mhu[2];
    UnimplementedDeviceState unimp[NUM_PPUS];
    UnimplementedDeviceState cachectrl[SSE_MAX_CPUS];
    UnimplementedDeviceState cpusecctrl[SSE_MAX_CPUS];

    ARMSSECPUID cpuid[SSE_MAX_CPUS];

    ARMSSECPUPwrCtrl cpu_pwrctrl[SSE_MAX_CPUS];

    /*
     * 'container' holds all devices seen by all CPUs.
     * 'cpu_container[i]' is the view that CPU i has: this has the
     * per-CPU devices of that CPU, plus as the background 'container'
     * (or an alias of it, since we can only use it directly once).
     * container_alias[i] is the alias of 'container' used by CPU i+1;
     * CPU 0 can use 'container' directly.
     */
    MemoryRegion container;
    MemoryRegion container_alias[SSE_MAX_CPUS - 1];
    MemoryRegion cpu_container[SSE_MAX_CPUS];
    MemoryRegion alias1;
    MemoryRegion alias2;
    MemoryRegion alias3[SSE_MAX_CPUS];
    MemoryRegion sram[MAX_SRAM_BANKS];
    MemoryRegion itcm;
    MemoryRegion dtcm;

    qemu_irq *exp_irqs[SSE_MAX_CPUS];
    qemu_irq ppc0_irq;
    qemu_irq ppc1_irq;
    qemu_irq sec_resp_cfg;
    qemu_irq sec_resp_cfg_in;
    qemu_irq nsc_cfg_in;

    qemu_irq irq_status_in[NUM_EXTERNAL_PPCS];
    qemu_irq mpcexp_status_in[IOTS_NUM_EXP_MPC];

    uint32_t nsccfg;

    Clock *mainclk;
    Clock *s32kclk;

    /* Properties */
    MemoryRegion *board_memory;
    uint32_t exp_numirq;
    uint32_t sram_addr_width;
    uint32_t init_svtor;
    bool cpu_fpu[SSE_MAX_CPUS];
    bool cpu_dsp[SSE_MAX_CPUS];
};

typedef struct ARMSSEInfo ARMSSEInfo;

struct ARMSSEClass {
    SysBusDeviceClass parent_class;
    const ARMSSEInfo *info;
};


#endif
