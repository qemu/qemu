/*
 * Copyright (c) 2011 - 2019, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "fpu/softfloat.h"

void HELPER(wur_fcr)(CPUXtensaState *env, uint32_t v)
{
    static const int rounding_mode[] = {
        float_round_nearest_even,
        float_round_to_zero,
        float_round_up,
        float_round_down,
    };

    env->uregs[FCR] = v & 0xfffff07f;
    set_float_rounding_mode(rounding_mode[v & 3], &env->fp_status);
}

float32 HELPER(abs_s)(float32 v)
{
    return float32_abs(v);
}

float32 HELPER(neg_s)(float32 v)
{
    return float32_chs(v);
}

float32 HELPER(add_s)(CPUXtensaState *env, float32 a, float32 b)
{
    return float32_add(a, b, &env->fp_status);
}

float32 HELPER(sub_s)(CPUXtensaState *env, float32 a, float32 b)
{
    return float32_sub(a, b, &env->fp_status);
}

float32 HELPER(mul_s)(CPUXtensaState *env, float32 a, float32 b)
{
    return float32_mul(a, b, &env->fp_status);
}

float32 HELPER(madd_s)(CPUXtensaState *env, float32 a, float32 b, float32 c)
{
    return float32_muladd(b, c, a, 0, &env->fp_status);
}

float32 HELPER(msub_s)(CPUXtensaState *env, float32 a, float32 b, float32 c)
{
    return float32_muladd(b, c, a, float_muladd_negate_product,
                          &env->fp_status);
}

uint32_t HELPER(ftoi)(float32 v, uint32_t rounding_mode, uint32_t scale)
{
    float_status fp_status = {0};

    set_float_rounding_mode(rounding_mode, &fp_status);
    return float32_to_int32(float32_scalbn(v, scale, &fp_status), &fp_status);
}

uint32_t HELPER(ftoui)(float32 v, uint32_t rounding_mode, uint32_t scale)
{
    float_status fp_status = {0};
    float32 res;

    set_float_rounding_mode(rounding_mode, &fp_status);

    res = float32_scalbn(v, scale, &fp_status);

    if (float32_is_neg(v) && !float32_is_any_nan(v)) {
        return float32_to_int32(res, &fp_status);
    } else {
        return float32_to_uint32(res, &fp_status);
    }
}

float32 HELPER(itof)(CPUXtensaState *env, uint32_t v, uint32_t scale)
{
    return float32_scalbn(int32_to_float32(v, &env->fp_status),
                          (int32_t)scale, &env->fp_status);
}

float32 HELPER(uitof)(CPUXtensaState *env, uint32_t v, uint32_t scale)
{
    return float32_scalbn(uint32_to_float32(v, &env->fp_status),
                          (int32_t)scale, &env->fp_status);
}

static inline void set_br(CPUXtensaState *env, bool v, uint32_t br)
{
    if (v) {
        env->sregs[BR] |= br;
    } else {
        env->sregs[BR] &= ~br;
    }
}

void HELPER(un_s)(CPUXtensaState *env, uint32_t br, float32 a, float32 b)
{
    set_br(env, float32_unordered_quiet(a, b, &env->fp_status), br);
}

void HELPER(oeq_s)(CPUXtensaState *env, uint32_t br, float32 a, float32 b)
{
    set_br(env, float32_eq_quiet(a, b, &env->fp_status), br);
}

void HELPER(ueq_s)(CPUXtensaState *env, uint32_t br, float32 a, float32 b)
{
    FloatRelation v = float32_compare_quiet(a, b, &env->fp_status);
    set_br(env, v == float_relation_equal || v == float_relation_unordered, br);
}

void HELPER(olt_s)(CPUXtensaState *env, uint32_t br, float32 a, float32 b)
{
    set_br(env, float32_lt_quiet(a, b, &env->fp_status), br);
}

void HELPER(ult_s)(CPUXtensaState *env, uint32_t br, float32 a, float32 b)
{
    FloatRelation v = float32_compare_quiet(a, b, &env->fp_status);
    set_br(env, v == float_relation_less || v == float_relation_unordered, br);
}

void HELPER(ole_s)(CPUXtensaState *env, uint32_t br, float32 a, float32 b)
{
    set_br(env, float32_le_quiet(a, b, &env->fp_status), br);
}

void HELPER(ule_s)(CPUXtensaState *env, uint32_t br, float32 a, float32 b)
{
    FloatRelation v = float32_compare_quiet(a, b, &env->fp_status);
    set_br(env, v != float_relation_greater, br);
}
