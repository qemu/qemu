/*
 * Type definitions for GICv5
 *
 * This file is for type definitions that we want to share between
 * the GIC proper and the CPU interface.
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICv5_TYPES_H
#define HW_INTC_ARM_GICv5_TYPES_H

#include "hw/core/registerfields.h"

/*
 * The GICv5 has four physical Interrupt Domains. This numbering must
 * match the encoding used in IRS_IDR0.INT_DOM.
 */
typedef enum GICv5Domain {
    GICV5_ID_S = 0,
    GICV5_ID_NS = 1,
    GICV5_ID_EL3 = 2,
    GICV5_ID_REALM = 3,
} GICv5Domain;

#define NUM_GICV5_DOMAINS 4

/* Architected GICv5 PPIs (as listed in R_XDVCM) */
#define GICV5_PPI_S_DB_PPI 0
#define GICV5_PPI_RL_DB_PPI 1
#define GICV5_PPI_NS_DB_PPI 2
#define GICV5_PPI_SW_PPI 3
#define GICV5_PPI_HACDBSIRQ 15
#define GICV5_PPI_CNTHVS 19
#define GICV5_PPI_CNTHPS 20
#define GICV5_PPI_PMBIRQ 21
#define GICV5_PPI_COMMIRQ 22
#define GICV5_PPI_PMUIRQ 23
#define GICV5_PPI_CTIIRQ 24
#define GICV5_PPI_GICMNT 25
#define GICV5_PPI_CNTHP 26
#define GICV5_PPI_CNTV 27
#define GICV5_PPI_CNTHV 28
#define GICV5_PPI_CNTPS 29
#define GICV5_PPI_CNTP 30
#define GICV5_PPI_TRBIRQ 31

/*
 * Type of the interrupt; these values match the 3-bit format
 * specified in the GICv5 spec R_GYVWB.
 */
typedef enum GICv5IntType {
    GICV5_PPI = 1,
    GICV5_LPI = 2,
    GICV5_SPI = 3,
} GICv5IntType;

/* Interrupt handling mode (same encoding as L2_ISTE.HM) */
typedef enum GICv5HandlingMode {
    GICV5_EDGE = 0,
    GICV5_LEVEL = 1,
} GICv5HandlingMode;

/*
 * Interrupt routing mode (same encoding as L2_ISTE.IRM).
 * Note that 1-of-N support is option and QEMU does not implement it.
 */
typedef enum GICv5RoutingMode {
    GICV5_TARGETED = 0,
    GICV5_1OFN = 1,
} GICv5RoutingMode;

/*
 * Interrupt trigger mode (same encoding as IRS_SPI_CFGR.TM) Note that
 * this is not the same thing as handling mode, even though the two
 * possible states have the same names. Trigger mode applies only for
 * SPIs and tells the IRS what kinds of changes to the input signal
 * wire should make it generate SET and CLEAR events.  Handling mode
 * affects whether the pending state of an interrupt is cleared when
 * the interrupt is acknowledged, and applies to both SPIs and LPIs.
 */
typedef enum GICv5TriggerMode {
    GICV5_TRIGGER_EDGE = 0,
    GICV5_TRIGGER_LEVEL = 1,
} GICv5TriggerMode;

#define PRIO_IDLE 0xff

/*
 * We keep track of candidate highest possible pending interrupts
 * using this struct.
 *
 * Unlike GICv3, we don't need a separate NMI bool, because for GICv5
 * superpriority is signaled by @prio == 0.
 *
 * In this struct the intid includes the interrupt type in bits
 * [31:29] (i.e. it is in the form defined by R_TJPHS).
 *
 * "No pending interrupt" is represented by @prio == PRIO_IDLE.
 */
typedef struct GICv5PendingIrq {
    uint32_t intid;
    uint8_t prio;
} GICv5PendingIrq;

/* A GICv5PendingIrq struct initializer for "no pending interrupt" */
#define GICV5_PENDING_IRQ_NONE \
    ((GICv5PendingIrq) { .intid = 0, .prio = PRIO_IDLE })

/* Fields in a generic 32-bit INTID, per R_TJPHS */
FIELD(INTID, ID, 0, 24)
FIELD(INTID, TYPE, 29, 3)

#endif
