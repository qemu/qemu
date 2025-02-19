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
C_O0_I2(rz, r)
C_O0_I2(rz, rz)
C_O1_I1(r, r)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rI)
C_O1_I2(r, r, rJ)
C_O1_I2(r, rz, rN)
C_O1_I2(r, rz, rz)
C_N1_I2(r, r, rM)
C_O1_I4(r, r, rI, rM, rM)
C_O2_I4(r, r, rz, rz, rM, rM)
C_O0_I2(v, r)
C_O1_I1(v, r)
C_O1_I1(v, v)
C_O1_I2(v, v, r)
C_O1_I2(v, v, v)
C_O1_I2(v, vK, v)
C_O1_I2(v, v, vK)
C_O1_I2(v, v, vL)
C_O1_I4(v, v, vL, vK, vK)
