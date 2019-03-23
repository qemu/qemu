/*
 * UniCore-F64 simulation helpers for QEMU.
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

/*
 * The convention used for UniCore-F64 instructions:
 *  Single precition routines have a "s" suffix
 *  Double precision routines have a "d" suffix.
 */

/* Convert host exception flags to f64 form.  */
static inline int ucf64_exceptbits_from_host(int host_bits)
{
    int target_bits = 0;

    if (host_bits & float_flag_invalid) {
        target_bits |= UCF64_FPSCR_FLAG_INVALID;
    }
    if (host_bits & float_flag_divbyzero) {
        target_bits |= UCF64_FPSCR_FLAG_DIVZERO;
    }
    if (host_bits & float_flag_overflow) {
        target_bits |= UCF64_FPSCR_FLAG_OVERFLOW;
    }
    if (host_bits & float_flag_underflow) {
        target_bits |= UCF64_FPSCR_FLAG_UNDERFLOW;
    }
    if (host_bits & float_flag_inexact) {
        target_bits |= UCF64_FPSCR_FLAG_INEXACT;
    }
    return target_bits;
}

uint32_t HELPER(ucf64_get_fpscr)(CPUUniCore32State *env)
{
    int i;
    uint32_t fpscr;

    fpscr = (env->ucf64.xregs[UC32_UCF64_FPSCR] & UCF64_FPSCR_MASK);
    i = get_float_exception_flags(&env->ucf64.fp_status);
    fpscr |= ucf64_exceptbits_from_host(i);
    return fpscr;
}

/* Convert ucf64 exception flags to target form.  */
static inline int ucf64_exceptbits_to_host(int target_bits)
{
    int host_bits = 0;

    if (target_bits & UCF64_FPSCR_FLAG_INVALID) {
        host_bits |= float_flag_invalid;
    }
    if (target_bits & UCF64_FPSCR_FLAG_DIVZERO) {
        host_bits |= float_flag_divbyzero;
    }
    if (target_bits & UCF64_FPSCR_FLAG_OVERFLOW) {
        host_bits |= float_flag_overflow;
    }
    if (target_bits & UCF64_FPSCR_FLAG_UNDERFLOW) {
        host_bits |= float_flag_underflow;
    }
    if (target_bits & UCF64_FPSCR_FLAG_INEXACT) {
        host_bits |= float_flag_inexact;
    }
    return host_bits;
}

void HELPER(ucf64_set_fpscr)(CPUUniCore32State *env, uint32_t val)
{
    UniCore32CPU *cpu = env_archcpu(env);
    int i;
    uint32_t changed;

    changed = env->ucf64.xregs[UC32_UCF64_FPSCR];
    env->ucf64.xregs[UC32_UCF64_FPSCR] = (val & UCF64_FPSCR_MASK);

    changed ^= val;
    if (changed & (UCF64_FPSCR_RND_MASK)) {
        i = UCF64_FPSCR_RND(val);
        switch (i) {
        case 0:
            i = float_round_nearest_even;
            break;
        case 1:
            i = float_round_to_zero;
            break;
        case 2:
            i = float_round_up;
            break;
        case 3:
            i = float_round_down;
            break;
        default: /* 100 and 101 not implement */
            cpu_abort(CPU(cpu), "Unsupported UniCore-F64 round mode");
        }
        set_float_rounding_mode(i, &env->ucf64.fp_status);
    }

    i = ucf64_exceptbits_to_host(UCF64_FPSCR_TRAPEN(val));
    set_float_exception_flags(i, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_adds)(float32 a, float32 b, CPUUniCore32State *env)
{
    return float32_add(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_addd)(float64 a, float64 b, CPUUniCore32State *env)
{
    return float64_add(a, b, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_subs)(float32 a, float32 b, CPUUniCore32State *env)
{
    return float32_sub(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_subd)(float64 a, float64 b, CPUUniCore32State *env)
{
    return float64_sub(a, b, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_muls)(float32 a, float32 b, CPUUniCore32State *env)
{
    return float32_mul(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_muld)(float64 a, float64 b, CPUUniCore32State *env)
{
    return float64_mul(a, b, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_divs)(float32 a, float32 b, CPUUniCore32State *env)
{
    return float32_div(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_divd)(float64 a, float64 b, CPUUniCore32State *env)
{
    return float64_div(a, b, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_negs)(float32 a)
{
    return float32_chs(a);
}

float64 HELPER(ucf64_negd)(float64 a)
{
    return float64_chs(a);
}

float32 HELPER(ucf64_abss)(float32 a)
{
    return float32_abs(a);
}

float64 HELPER(ucf64_absd)(float64 a)
{
    return float64_abs(a);
}

void HELPER(ucf64_cmps)(float32 a, float32 b, uint32_t c,
        CPUUniCore32State *env)
{
    int flag;
    flag = float32_compare_quiet(a, b, &env->ucf64.fp_status);
    env->CF = 0;
    switch (c & 0x7) {
    case 0: /* F */
        break;
    case 1: /* UN */
        if (flag == 2) {
            env->CF = 1;
        }
        break;
    case 2: /* EQ */
        if (flag == 0) {
            env->CF = 1;
        }
        break;
    case 3: /* UEQ */
        if ((flag == 0) || (flag == 2)) {
            env->CF = 1;
        }
        break;
    case 4: /* OLT */
        if (flag == -1) {
            env->CF = 1;
        }
        break;
    case 5: /* ULT */
        if ((flag == -1) || (flag == 2)) {
            env->CF = 1;
        }
        break;
    case 6: /* OLE */
        if ((flag == -1) || (flag == 0)) {
            env->CF = 1;
        }
        break;
    case 7: /* ULE */
        if (flag != 1) {
            env->CF = 1;
        }
        break;
    }
    env->ucf64.xregs[UC32_UCF64_FPSCR] = (env->CF << 29)
                    | (env->ucf64.xregs[UC32_UCF64_FPSCR] & 0x0fffffff);
}

void HELPER(ucf64_cmpd)(float64 a, float64 b, uint32_t c,
        CPUUniCore32State *env)
{
    int flag;
    flag = float64_compare_quiet(a, b, &env->ucf64.fp_status);
    env->CF = 0;
    switch (c & 0x7) {
    case 0: /* F */
        break;
    case 1: /* UN */
        if (flag == 2) {
            env->CF = 1;
        }
        break;
    case 2: /* EQ */
        if (flag == 0) {
            env->CF = 1;
        }
        break;
    case 3: /* UEQ */
        if ((flag == 0) || (flag == 2)) {
            env->CF = 1;
        }
        break;
    case 4: /* OLT */
        if (flag == -1) {
            env->CF = 1;
        }
        break;
    case 5: /* ULT */
        if ((flag == -1) || (flag == 2)) {
            env->CF = 1;
        }
        break;
    case 6: /* OLE */
        if ((flag == -1) || (flag == 0)) {
            env->CF = 1;
        }
        break;
    case 7: /* ULE */
        if (flag != 1) {
            env->CF = 1;
        }
        break;
    }
    env->ucf64.xregs[UC32_UCF64_FPSCR] = (env->CF << 29)
                    | (env->ucf64.xregs[UC32_UCF64_FPSCR] & 0x0fffffff);
}

/* Helper routines to perform bitwise copies between float and int.  */
static inline float32 ucf64_itos(uint32_t i)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.i = i;
    return v.s;
}

static inline uint32_t ucf64_stoi(float32 s)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.s = s;
    return v.i;
}

/* Integer to float conversion.  */
float32 HELPER(ucf64_si2sf)(float32 x, CPUUniCore32State *env)
{
    return int32_to_float32(ucf64_stoi(x), &env->ucf64.fp_status);
}

float64 HELPER(ucf64_si2df)(float32 x, CPUUniCore32State *env)
{
    return int32_to_float64(ucf64_stoi(x), &env->ucf64.fp_status);
}

/* Float to integer conversion.  */
float32 HELPER(ucf64_sf2si)(float32 x, CPUUniCore32State *env)
{
    return ucf64_itos(float32_to_int32(x, &env->ucf64.fp_status));
}

float32 HELPER(ucf64_df2si)(float64 x, CPUUniCore32State *env)
{
    return ucf64_itos(float64_to_int32(x, &env->ucf64.fp_status));
}

/* floating point conversion */
float64 HELPER(ucf64_sf2df)(float32 x, CPUUniCore32State *env)
{
    return float32_to_float64(x, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_df2sf)(float64 x, CPUUniCore32State *env)
{
    return float64_to_float32(x, &env->ucf64.fp_status);
}
