/* SPDX-License-Identifier: MIT */
/*
 * Define LoongArch target-specific constraint sets.
 *
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 *
 * Based on tcg/riscv/tcg-target-con-set.h
 *
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
C_O0_I2(w, r)
C_O0_I3(r, r, r)
C_O1_I1(r, r)
C_O1_I1(w, r)
C_O1_I1(w, w)
C_O1_I2(r, r, rC)
C_O1_I2(r, r, ri)
C_O1_I2(r, r, rI)
C_O1_I2(r, r, rJ)
C_O1_I2(r, r, rU)
C_O1_I2(r, r, rW)
C_O1_I2(r, 0, rz)
C_O1_I2(r, rz, ri)
C_O1_I2(r, rz, rJ)
C_O1_I2(r, rz, rz)
C_O1_I2(w, w, w)
C_O1_I2(w, w, wM)
C_O1_I2(w, w, wA)
C_O1_I3(w, w, w, w)
C_O1_I4(r, rz, rJ, rz, rz)
C_N2_I1(r, r, r)
