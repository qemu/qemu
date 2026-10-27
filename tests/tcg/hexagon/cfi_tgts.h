/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CFI_TGTS_H
#define CFI_TGTS_H

#define CFI_TEST_CASES 13

void run_test_case(int valid, int test_case);
void run_callr_rb_lr_test(int valid);
void run_jumpr_rb_lr_test(void);
void run_jumpr_rb_nop_rb_test(void);
void run_trap0_target_test(void);
void run_allocframe_test(void);

#endif
