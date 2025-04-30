/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Define AArch64 target-specific constraint sets.
 * Copyright (c) 2021 Linaro
 */

/*
 * C_On_Im(...) defines a constraint set with <n> outputs and <m> inputs.
 * Each operand should be a sequence of constraint letters as defined by
 * tcg-target-con-str.h; the constraint combination is inclusive or.
 */
C_O0_I1(r)
C_O0_I2(r, rC)
C_O0_I2(rz, r)
C_O0_I2(w, r)
C_O0_I3(rz, rz, r)
C_O1_I1(r, r)
C_O1_I1(w, r)
C_O1_I1(w, w)
C_O1_I1(w, wr)
C_O1_I2(r, r, r)
C_O1_I2(r, r, rA)
C_O1_I2(r, r, rAL)
C_O1_I2(r, r, rC)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rL)
C_O1_I2(r, rZ, rA)
C_O1_I2(r, rz, rMZ)
C_O1_I2(r, rz, rz)
C_O1_I2(r, rZ, rZ)
C_O1_I2(w, 0, w)
C_O1_I2(w, w, w)
C_O1_I2(w, w, wN)
C_O1_I2(w, w, wO)
C_O1_I2(w, w, wZ)
C_O1_I3(w, w, w, w)
C_O1_I4(r, r, rC, rz, rz)
C_O2_I1(r, r, r)
