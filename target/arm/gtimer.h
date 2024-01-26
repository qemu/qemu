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
    GTIMER_PHYS     = 0,
    GTIMER_VIRT     = 1,
    GTIMER_HYP      = 2,
    GTIMER_SEC      = 3,
    GTIMER_HYPVIRT  = 4,
#define NUM_GTIMERS   5
};

#endif
