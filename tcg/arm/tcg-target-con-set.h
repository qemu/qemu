/* SPDX-License-Identifier: MIT */
/*
 * Define Arm target-specific constraint sets.
 * Copyright (c) 2021 Linaro
 */

/*
 * C_On_Im(...) defines a constraint set with <n> outputs and <m> inputs.
 * Each operand should be a sequence of constraint letters as defined by
 * tcg-target-con-str.h; the constraint combination is inclusive or.
 */
C_O0_I1(r)
C_O0_I2(r, r)
C_O0_I2(r, rIN)
C_O0_I2(q, q)
C_O0_I2(w, r)
C_O0_I3(q, q, q)
C_O0_I3(Q, p, q)
C_O0_I4(r, r, rI, rI)
C_O0_I4(Q, p, q, q)
C_O1_I1(r, q)
C_O1_I1(r, r)
C_O1_I1(w, r)
C_O1_I1(w, w)
C_O1_I1(w, wr)
C_O1_I2(r, 0, rZ)
C_O1_I2(r, q, q)
C_O1_I2(r, r, r)
C_O1_I2(r, r, rI)
C_O1_I2(r, r, rIK)
C_O1_I2(r, r, rIN)
C_O1_I2(r, r, ri)
C_O1_I2(r, rI, r)
C_O1_I2(r, rI, rIK)
C_O1_I2(r, rI, rIN)
C_O1_I2(r, rZ, rZ)
C_O1_I2(w, 0, w)
C_O1_I2(w, w, w)
C_O1_I2(w, w, wO)
C_O1_I2(w, w, wV)
C_O1_I2(w, w, wZ)
C_O1_I3(w, w, w, w)
C_O1_I4(r, r, r, rI, rI)
C_O1_I4(r, r, rIN, rIK, 0)
C_O2_I1(e, p, q)
C_O2_I2(e, p, q, q)
C_O2_I2(r, r, r, r)
