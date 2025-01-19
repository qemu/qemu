/* SPDX-License-Identifier: MIT */
/*
 * Define S390 target-specific constraint sets.
 * Copyright (c) 2021 Linaro
 */

/*
 * C_On_Im(...) defines a constraint set with <n> outputs and <m> inputs.
 * Each operand should be a sequence of constraint letters as defined by
 * tcg-target-con-str.h; the constraint combination is inclusive or.
 *
 * C_Nn_Om_Ik(...) defines a constraint set with <n + m> outputs and <k>
 * inputs, except that the first <n> outputs must use new registers.
 */
C_O0_I1(r)
C_O0_I2(r, r)
C_O0_I2(r, ri)
C_O0_I2(r, rC)
C_O0_I2(v, r)
C_O0_I3(o, m, r)
C_O1_I1(r, r)
C_O1_I1(v, r)
C_O1_I1(v, v)
C_O1_I1(v, vr)
C_O1_I2(r, 0, r)
C_O1_I2(r, 0, ri)
C_O1_I2(r, 0, rI)
C_O1_I2(r, 0, rJ)
C_O1_I2(r, r, r)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rC)
C_O1_I2(r, r, rI)
C_O1_I2(r, r, rJ)
C_O1_I2(r, r, rK)
C_O1_I2(r, r, rNKR)
C_O1_I2(r, r, rUV)
C_O1_I2(r, rZ, r)
C_O1_I2(v, v, r)
C_O1_I2(v, v, v)
C_O1_I3(v, v, v, v)
C_O1_I4(v, v, v, vZ, v)
C_O1_I4(v, v, v, vZM, v)
C_O1_I4(r, r, rC, rI, r)
C_O2_I1(o, m, r)
C_O2_I2(o, m, 0, r)
C_O2_I2(o, m, r, r)
C_O2_I3(o, m, 0, 1, r)
