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
    FloatInfZeroNaNRule izn_rule;
    Float3NaNPropRule nan3_rule;

    /*
     * With nan2008, SNaNs are silenced in the usual way.
     * Before that, SNaNs are not silenced; default nans are produced.
     */
    set_snan_bit_is_one(!nan2008, &env->active_fpu.fp_status);
    set_default_nan_mode(!nan2008, &env->active_fpu.fp_status);
    /*
     * For MIPS systems that conform to IEEE754-1985, the (inf,zero,nan)
     * case sets InvalidOp and returns the default NaN.
     * For MIPS systems that conform to IEEE754-2008, the (inf,zero,nan)
     * case sets InvalidOp and returns the input value 'c'.
     */
    izn_rule = nan2008 ? float_infzeronan_dnan_never : float_infzeronan_dnan_always;
    set_float_infzeronan_rule(izn_rule, &env->active_fpu.fp_status);
    nan3_rule = nan2008 ? float_3nan_prop_s_cab : float_3nan_prop_s_abc;
    set_float_3nan_prop_rule(nan3_rule, &env->active_fpu.fp_status);
    /*
     * With nan2008, the default NaN value has the sign bit clear and the
     * frac msb set; with the older mode, the sign bit is clear, and all
     * frac bits except the msb are set.
     */
    set_float_default_nan_pattern(nan2008 ? 0b01000000 : 0b00111111,
                                  &env->active_fpu.fp_status);

}

static inline void restore_fp_status(CPUMIPSState *env)
{
    restore_rounding_mode(env);
    restore_flush_mode(env);
    restore_snan_bit_mode(env);
}

static inline void fp_reset(CPUMIPSState *env)
{
    restore_fp_status(env);

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
                             &env->active_fpu.fp_status);
    /*
     * TODO: the spec does't say clearly whether FTZ happens before
     * or after rounding for normal FPU operations.
     */
    set_float_ftz_detection(float_ftz_before_rounding,
                            &env->active_fpu.fp_status);
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
