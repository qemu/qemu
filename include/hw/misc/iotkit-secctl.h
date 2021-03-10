/*
 * ARM IoT Kit security controller
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/* This is a model of the security controller which is part of the
 * Arm IoT Kit and documented in
 * https://developer.arm.com/documentation/ecm0601256/latest
 *
 * QEMU interface:
 *  + sysbus MMIO region 0 is the "secure privilege control block" registers
 *  + sysbus MMIO region 1 is the "non-secure privilege control block" registers
 *  + named GPIO output "sec_resp_cfg" indicating whether blocked accesses
 *    should RAZ/WI or bus error
 *  + named GPIO output "nsc_cfg" whose value tracks the NSCCFG register value
 *  + named GPIO output "msc_irq" for the combined IRQ line from the MSCs
 * Controlling the 2 APB PPCs in the IoTKit:
 *  + named GPIO outputs apb_ppc0_nonsec[0..2] and apb_ppc1_nonsec
 *  + named GPIO outputs apb_ppc0_ap[0..2] and apb_ppc1_ap
 *  + named GPIO outputs apb_ppc{0,1}_irq_enable
 *  + named GPIO outputs apb_ppc{0,1}_irq_clear
 *  + named GPIO inputs apb_ppc{0,1}_irq_status
 * Controlling each of the 4 expansion APB PPCs which a system using the IoTKit
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
 * Controlling the (up to) 4 MPCs in the IoTKit/SSE:
 *  + named GPIO inputs mpc_status[0..3]
 * Controlling each of the 16 expansion MPCs which a system using the IoTKit
 * might provide:
 *  + named GPIO inputs mpcexp_status[0..15]
 * Controlling each of the 16 expansion MSCs which a system using the IoTKit
 * might provide:
 *  + named GPIO inputs mscexp_status[0..15]
 *  + named GPIO outputs mscexp_clear[0..15]
 *  + named GPIO outputs mscexp_ns[0..15]
 */

#ifndef IOTKIT_SECCTL_H
#define IOTKIT_SECCTL_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IOTKIT_SECCTL "iotkit-secctl"
OBJECT_DECLARE_SIMPLE_TYPE(IoTKitSecCtl, IOTKIT_SECCTL)

#define IOTS_APB_PPC0_NUM_PORTS 3
#define IOTS_APB_PPC1_NUM_PORTS 1
#define IOTS_PPC_NUM_PORTS 16
#define IOTS_NUM_APB_PPC 2
#define IOTS_NUM_APB_EXP_PPC 4
#define IOTS_NUM_AHB_EXP_PPC 4
#define IOTS_NUM_EXP_MPC 16
#define IOTS_NUM_MPC 4
#define IOTS_NUM_EXP_MSC 16


/* State and IRQ lines relating to a PPC. For the
 * PPCs in the IoTKit not all the IRQ lines are used.
 */
typedef struct IoTKitSecCtlPPC {
    qemu_irq nonsec[IOTS_PPC_NUM_PORTS];
    qemu_irq ap[IOTS_PPC_NUM_PORTS];
    qemu_irq irq_enable;
    qemu_irq irq_clear;

    uint32_t ns;
    uint32_t sp;
    uint32_t nsp;

    /* Number of ports actually present */
    int numports;
    /* Offset of this PPC's interrupt bits in SECPPCINTSTAT */
    int irq_bit_offset;
    IoTKitSecCtl *parent;
} IoTKitSecCtlPPC;

struct IoTKitSecCtl {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    qemu_irq sec_resp_cfg;
    qemu_irq nsc_cfg_irq;

    MemoryRegion s_regs;
    MemoryRegion ns_regs;

    uint32_t secppcintstat;
    uint32_t secppcinten;
    uint32_t secrespcfg;
    uint32_t nsccfg;
    uint32_t brginten;
    uint32_t mpcintstatus;

    uint32_t secmscintstat;
    uint32_t secmscinten;
    uint32_t nsmscexp;
    qemu_irq mscexp_clear[IOTS_NUM_EXP_MSC];
    qemu_irq mscexp_ns[IOTS_NUM_EXP_MSC];
    qemu_irq msc_irq;

    IoTKitSecCtlPPC apb[IOTS_NUM_APB_PPC];
    IoTKitSecCtlPPC apbexp[IOTS_NUM_APB_EXP_PPC];
    IoTKitSecCtlPPC ahbexp[IOTS_NUM_APB_EXP_PPC];

    uint32_t sse_version;
};

#endif
