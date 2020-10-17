/* SPDX-License-Identifier: MIT */
/*
 * Define PowerPC target-specific constraint sets.
 * Copyright (c) 2021 Linaro
 */

/*
 * C_On_Im(...) defines a constraint set with <n> outputs and <m> inputs.
 * Each operand should be a sequence of constraint letters as defined by
 * tcg-target-con-str.h; the constraint combination is inclusive or.
 */
C_O0_I1(r)
C_O0_I2(r, r)
C_O0_I2(r, ri)
C_O0_I2(S, S)
C_O0_I2(v, r)
C_O0_I3(S, S, S)
C_O0_I4(r, r, ri, ri)
C_O0_I4(S, S, S, S)
C_O1_I1(r, L)
C_O1_I1(r, r)
C_O1_I1(v, r)
C_O1_I1(v, v)
C_O1_I1(v, vr)
C_O1_I2(r, 0, rZ)
C_O1_I2(r, L, L)
C_O1_I2(r, rI, ri)
C_O1_I2(r, rI, rT)
C_O1_I2(r, r, r)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rI)
C_O1_I2(r, r, rT)
C_O1_I2(r, r, rU)
C_O1_I2(r, r, rZW)
C_O1_I2(v, v, v)
C_O1_I3(v, v, v, v)
C_O1_I4(r, r, ri, rZ, rZ)
C_O1_I4(r, r, r, ri, ri)
C_O2_I1(L, L, L)
C_O2_I2(L, L, L, L)
C_O2_I4(r, r, rI, rZM, r, r)
C_O2_I4(r, r, r, r, rI, rZM)
