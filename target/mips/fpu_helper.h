/*
 * Helpers for emulation of FPU-related MIPS instructions.
 *
 *  Copyright (C) 2004-2005  Jocelyn Mayer
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "fpu/softfloat-helpers.h"
#include "cpu.h"

extern const FloatRoundMode ieee_rm[4];

uint32_t float_class_s(uint32_t arg, float_status *fst);
uint64_t float_class_d(uint64_t arg, float_status *fst);

static inline void restore_rounding_mode(CPUMIPSState *env)
{
    set_float_rounding_mode(ieee_rm[env->active_fpu.fcr31 & 3],
                            &env->active_fpu.fp_status);
}

static inline void restore_flush_mode(CPUMIPSState *env)
{
    set_flush_to_zero((env->active_fpu.fcr31 & (1 << FCR31_FS)) != 0,
                      &env->active_fpu.fp_status);
}

static inline void restore_snan_bit_mode(CPUMIPSState *env)
{
    bool nan2008 = env->active_fpu.fcr31 & (1 << FCR31_NAN2008);

    /*
     * With nan2008, SNaNs are silenced in the usual way.
     * Before that, SNaNs are not silenced; default nans are produced.
     */
    set_snan_bit_is_one(!nan2008, &env->active_fpu.fp_status);
    set_default_nan_mode(!nan2008, &env->active_fpu.fp_status);
}

static inline void restore_fp_status(CPUMIPSState *env)
{
    restore_rounding_mode(env);
    restore_flush_mode(env);
    restore_snan_bit_mode(env);
}

/* MSA */

enum CPUMIPSMSADataFormat {
    DF_BYTE = 0,
    DF_HALF,
    DF_WORD,
    DF_DOUBLE
};

static inline void restore_msa_fp_status(CPUMIPSState *env)
{
    float_status *status = &env->active_tc.msa_fp_status;
    int rounding_mode = (env->active_tc.msacsr & MSACSR_RM_MASK) >> MSACSR_RM;
    bool flush_to_zero = (env->active_tc.msacsr & MSACSR_FS_MASK) != 0;

    set_float_rounding_mode(ieee_rm[rounding_mode], status);
    set_flush_to_zero(flush_to_zero, status);
    set_flush_inputs_to_zero(flush_to_zero, status);
}
