/*
 *  Copyright(c) 2020-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

    .text
    .type test_set_prio, @function
    .global test_set_prio

test_set_prio:
    r0 = #3
    r1 = #1
    p0 = cmp.eq(r0,r1)
    setprio(p0, r0)
    jumpr lr

    .size test_set_prio, . - test_set_prio
