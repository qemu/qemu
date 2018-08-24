/*
 * ARM IoT Kit
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/* This is a model of the Arm IoT Kit which is documented in
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ecm0601256/index.html
 * It contains:
 *  a Cortex-M33
 *  the IDAU
 *  some timers and watchdogs
 *  two peripheral protection controllers
 *  a memory protection controller
 *  a security controller
 *  a bus fabric which arranges that some parts of the address
 *  space are secure and non-secure aliases of each other
 *
 * QEMU interface:
 *  + QOM property "memory" is a MemoryRegion containing the devices provided
 *    by the board model.
 *  + QOM property "MAINCLK" is the frequency of the main system clock
 *  + QOM property "EXP_NUMIRQ" sets the number of expansion interrupts
 *  + Named GPIO inputs "EXP_IRQ" 0..n are the expansion interrupts, which
 *    are wired to the NVIC lines 32 .. n+32
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

#ifndef IOTKIT_H
#define IOTKIT_H

#include "hw/sysbus.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/iotkit-secctl.h"
#include "hw/misc/tz-ppc.h"
#include "hw/misc/tz-mpc.h"
#include "hw/timer/cmsdk-apb-timer.h"
#include "hw/timer/cmsdk-apb-dualtimer.h"
#include "hw/watchdog/cmsdk-apb-watchdog.h"
#include "hw/misc/iotkit-sysctl.h"
#include "hw/misc/iotkit-sysinfo.h"
#include "hw/or-irq.h"
#include "hw/core/split-irq.h"

#define TYPE_IOTKIT "iotkit"
#define IOTKIT(obj) OBJECT_CHECK(IoTKit, (obj), TYPE_IOTKIT)

/* We have an IRQ splitter and an OR gate input for each external PPC
 * and the 2 internal PPCs
 */
#define NUM_EXTERNAL_PPCS (IOTS_NUM_AHB_EXP_PPC + IOTS_NUM_APB_EXP_PPC)
#define NUM_PPCS (NUM_EXTERNAL_PPCS + 2)

typedef struct IoTKit {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMv7MState armv7m;
    IoTKitSecCtl secctl;
    TZPPC apb_ppc0;
    TZPPC apb_ppc1;
    TZMPC mpc;
    CMSDKAPBTIMER timer0;
    CMSDKAPBTIMER timer1;
    CMSDKAPBTIMER s32ktimer;
    qemu_or_irq ppc_irq_orgate;
    SplitIRQ sec_resp_splitter;
    SplitIRQ ppc_irq_splitter[NUM_PPCS];
    SplitIRQ mpc_irq_splitter[IOTS_NUM_EXP_MPC + IOTS_NUM_MPC];
    qemu_or_irq mpc_irq_orgate;
    qemu_or_irq nmi_orgate;

    CMSDKAPBDualTimer dualtimer;

    CMSDKAPBWatchdog s32kwatchdog;
    CMSDKAPBWatchdog nswatchdog;
    CMSDKAPBWatchdog swatchdog;

    IoTKitSysCtl sysctl;
    IoTKitSysCtl sysinfo;

    MemoryRegion container;
    MemoryRegion alias1;
    MemoryRegion alias2;
    MemoryRegion alias3;
    MemoryRegion sram0;

    qemu_irq *exp_irqs;
    qemu_irq ppc0_irq;
    qemu_irq ppc1_irq;
    qemu_irq sec_resp_cfg;
    qemu_irq sec_resp_cfg_in;
    qemu_irq nsc_cfg_in;

    qemu_irq irq_status_in[NUM_EXTERNAL_PPCS];
    qemu_irq mpcexp_status_in[IOTS_NUM_EXP_MPC];

    uint32_t nsccfg;

    /* Properties */
    MemoryRegion *board_memory;
    uint32_t exp_numirq;
    uint32_t mainclk_frq;
} IoTKit;

#endif
