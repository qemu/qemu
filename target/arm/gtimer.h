/*
 * ARM generic timer definitions for Arm A-class CPU
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef TARGET_ARM_GTIMER_H
#define TARGET_ARM_GTIMER_H

enum {
    GTIMER_PHYS     = 0, /* CNTP_* ; EL1 physical timer */
    GTIMER_VIRT     = 1, /* CNTV_* ; EL1 virtual timer */
    GTIMER_HYP      = 2, /* CNTHP_* ; EL2 physical timer */
    GTIMER_SEC      = 3, /* CNTPS_* ; EL3 physical timer */
    GTIMER_HYPVIRT  = 4, /* CNTHV_* ; EL2 virtual timer ; only if FEAT_VHE */
    GTIMER_S_EL2_PHYS = 5, /* CNTHPS_* ; only if FEAT_SEL2 */
    GTIMER_S_EL2_VIRT = 6, /* CNTHVS_* ; only if FEAT_SEL2 */
#define NUM_GTIMERS   7
};

#endif
