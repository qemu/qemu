/* SPDX-License-Identifier: MIT */
/*
 * Define RISC-V target-specific constraint sets.
 * Copyright (c) 2021 Linaro
 */

/*
 * C_On_Im(...) defines a constraint set with <n> outputs and <m> inputs.
 * Each operand should be a sequence of constraint letters as defined by
 * tcg-target-con-str.h; the constraint combination is inclusive or.
 */
C_O0_I1(r)
C_O0_I2(LZ, L)
C_O0_I2(rZ, r)
C_O0_I2(rZ, rZ)
C_O0_I3(LZ, L, L)
C_O0_I3(LZ, LZ, L)
C_O0_I4(LZ, LZ, L, L)
C_O0_I4(rZ, rZ, rZ, rZ)
C_O1_I1(r, L)
C_O1_I1(r, r)
C_O1_I2(r, L, L)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rI)
C_O1_I2(r, rZ, rN)
C_O1_I2(r, rZ, rZ)
C_O1_I4(r, rZ, rZ, rZ, rZ)
C_O2_I1(r, r, L)
C_O2_I2(r, r, L, L)
C_O2_I4(r, r, rZ, rZ, rM, rM)
