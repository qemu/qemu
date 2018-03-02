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
 */

#ifndef IOTKIT_H
#define IOTKIT_H

#include "hw/sysbus.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/iotkit-secctl.h"
#include "hw/misc/tz-ppc.h"
#include "hw/timer/cmsdk-apb-timer.h"
#include "hw/misc/unimp.h"
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
    CMSDKAPBTIMER timer0;
    CMSDKAPBTIMER timer1;
    qemu_or_irq ppc_irq_orgate;
    SplitIRQ sec_resp_splitter;
    SplitIRQ ppc_irq_splitter[NUM_PPCS];

    UnimplementedDeviceState dualtimer;
    UnimplementedDeviceState s32ktimer;

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

    uint32_t nsccfg;

    /* Properties */
    MemoryRegion *board_memory;
    uint32_t exp_numirq;
    uint32_t mainclk_frq;
} IoTKit;

#endif
