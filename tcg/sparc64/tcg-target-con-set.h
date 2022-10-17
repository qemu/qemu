/* SPDX-License-Identifier: MIT */
/*
 * Define Sparc target-specific constraint sets.
 * Copyright (c) 2021 Linaro
 */

/*
 * C_On_Im(...) defines a constraint set with <n> outputs and <m> inputs.
 * Each operand should be a sequence of constraint letters as defined by
 * tcg-target-con-str.h; the constraint combination is inclusive or.
 */
C_O0_I1(r)
C_O0_I2(rZ, r)
C_O0_I2(rZ, rJ)
C_O0_I2(sZ, s)
C_O1_I1(r, s)
C_O1_I1(r, r)
C_O1_I2(r, r, r)
C_O1_I2(r, rZ, rJ)
C_O1_I4(r, rZ, rJ, rI, 0)
C_O2_I2(r, r, rZ, rJ)
C_O2_I4(r, r, rZ, rZ, rJ, rJ)
