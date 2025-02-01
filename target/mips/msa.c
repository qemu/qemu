/*
 * MIPS SIMD Architecture Module Instruction emulation helpers for QEMU.
 *
 * Copyright (c) 2014 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "fpu/softfloat.h"
#include "fpu_helper.h"

void msa_reset(CPUMIPSState *env)
{
    if (!ase_msa_available(env)) {
        return;
    }

#ifdef CONFIG_USER_ONLY
    /* MSA access enabled */
    env->CP0_Config5 |= 1 << CP0C5_MSAEn;
    env->CP0_Status |= (1 << CP0St_CU1) | (1 << CP0St_FR);
#endif

    /*
     * MSA CSR:
     * - non-signaling floating point exception mode off (NX bit is 0)
     * - Cause, Enables, and Flags are all 0
     * - round to nearest / ties to even (RM bits are 0)
     */
    env->active_tc.msacsr = 0;

    restore_msa_fp_status(env);

    /* tininess detected after rounding.*/
    set_float_detect_tininess(float_tininess_after_rounding,
                              &env->active_tc.msa_fp_status);
    /*
     * MSACSR.FS detects tiny results to flush to zero before rounding
     * (per "MIPS Architecture for Programmers Volume IV-j: The MIPS64 SIMD
     * Architecture Module, Revision 1.1" section 3.5.4), even though it
     * detects tininess after rounding for underflow purposes (section 3.4.2
     * table 3.3).
     */
    set_float_ftz_detection(float_ftz_before_rounding,
                            &env->active_tc.msa_fp_status);

    /*
     * According to MIPS specifications, if one of the two operands is
     * a sNaN, a new qNaN has to be generated. This is done in
     * floatXX_silence_nan(). For qNaN inputs the specifications
     * says: "When possible, this QNaN result is one of the operand QNaN
     * values." In practice it seems that most implementations choose
     * the first operand if both operands are qNaN. In short this gives
     * the following rules:
     *  1. A if it is signaling
     *  2. B if it is signaling
     *  3. A (quiet)
     *  4. B (quiet)
     * A signaling NaN is always silenced before returning it.
     */
    set_float_2nan_prop_rule(float_2nan_prop_s_ab,
                             &env->active_tc.msa_fp_status);

    set_float_3nan_prop_rule(float_3nan_prop_s_cab,
                             &env->active_tc.msa_fp_status);

    /* clear float_status exception flags */
    set_float_exception_flags(0, &env->active_tc.msa_fp_status);

    /* clear float_status nan mode */
    set_default_nan_mode(0, &env->active_tc.msa_fp_status);

    /* set proper signanling bit meaning ("1" means "quiet") */
    set_snan_bit_is_one(0, &env->active_tc.msa_fp_status);

    /* Inf * 0 + NaN returns the input NaN */
    set_float_infzeronan_rule(float_infzeronan_dnan_never,
                              &env->active_tc.msa_fp_status);
    /* Default NaN: sign bit clear, frac msb set */
    set_float_default_nan_pattern(0b01000000,
                                  &env->active_tc.msa_fp_status);
}
