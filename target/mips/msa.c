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

    /* clear float_status exception flags */
    set_float_exception_flags(0, &env->active_tc.msa_fp_status);

    /* clear float_status nan mode */
    set_default_nan_mode(0, &env->active_tc.msa_fp_status);

    /* set proper signanling bit meaning ("1" means "quiet") */
    set_snan_bit_is_one(0, &env->active_tc.msa_fp_status);
}
