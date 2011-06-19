/*
 * Copyright (C) 2010-2011 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "gdbstub.h"
#include "helper.h"
#include "qemu-common.h"
#include "host-utils.h"

static inline void set_feature(CPUState *env, int feature)
{
    env->features |= feature;
}

struct uc32_cpu_t {
    uint32_t id;
    const char *name;
};

static const struct uc32_cpu_t uc32_cpu_names[] = {
    { UC32_CPUID_UCV2, "UniCore-II"},
    { UC32_CPUID_ANY, "any"},
    { 0, NULL}
};

/* return 0 if not found */
static uint32_t uc32_cpu_find_by_name(const char *name)
{
    int i;
    uint32_t id;

    id = 0;
    for (i = 0; uc32_cpu_names[i].name; i++) {
        if (strcmp(name, uc32_cpu_names[i].name) == 0) {
            id = uc32_cpu_names[i].id;
            break;
        }
    }
    return id;
}

CPUState *uc32_cpu_init(const char *cpu_model)
{
    CPUState *env;
    uint32_t id;
    static int inited = 1;

    env = qemu_mallocz(sizeof(CPUState));
    cpu_exec_init(env);

    id = uc32_cpu_find_by_name(cpu_model);
    switch (id) {
    case UC32_CPUID_UCV2:
        set_feature(env, UC32_HWCAP_CMOV);
        set_feature(env, UC32_HWCAP_UCF64);
        env->ucf64.xregs[UC32_UCF64_FPSCR] = 0;
        env->cp0.c0_cachetype = 0x1dd20d2;
        env->cp0.c1_sys = 0x00090078;
        break;
    case UC32_CPUID_ANY: /* For userspace emulation.  */
        set_feature(env, UC32_HWCAP_CMOV);
        set_feature(env, UC32_HWCAP_UCF64);
        break;
    default:
        cpu_abort(env, "Bad CPU ID: %x\n", id);
    }

    env->cpu_model_str = cpu_model;
    env->cp0.c0_cpuid = id;
    env->uncached_asr = ASR_MODE_USER;
    env->regs[31] = 0;

    if (inited) {
        inited = 0;
        uc32_translate_init();
    }

    tlb_flush(env, 1);
    qemu_init_vcpu(env);
    return env;
}

uint32_t HELPER(clo)(uint32_t x)
{
    return clo32(x);
}

uint32_t HELPER(clz)(uint32_t x)
{
    return clz32(x);
}

void do_interrupt(CPUState *env)
{
    env->exception_index = -1;
}

int uc32_cpu_handle_mmu_fault(CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu)
{
    env->exception_index = UC32_EXCP_TRAP;
    env->cp0.c4_faultaddr = address;
    return 1;
}

/* These should probably raise undefined insn exceptions.  */
void HELPER(set_cp)(CPUState *env, uint32_t insn, uint32_t val)
{
    int op1 = (insn >> 8) & 0xf;
    cpu_abort(env, "cp%i insn %08x\n", op1, insn);
    return;
}

uint32_t HELPER(get_cp)(CPUState *env, uint32_t insn)
{
    int op1 = (insn >> 8) & 0xf;
    cpu_abort(env, "cp%i insn %08x\n", op1, insn);
    return 0;
}

void HELPER(set_cp0)(CPUState *env, uint32_t insn, uint32_t val)
{
    cpu_abort(env, "cp0 insn %08x\n", insn);
}

uint32_t HELPER(get_cp0)(CPUState *env, uint32_t insn)
{
    cpu_abort(env, "cp0 insn %08x\n", insn);
    return 0;
}

void switch_mode(CPUState *env, int mode)
{
    if (mode != ASR_MODE_USER) {
        cpu_abort(env, "Tried to switch out of user mode\n");
    }
}

void HELPER(set_r29_banked)(CPUState *env, uint32_t mode, uint32_t val)
{
    cpu_abort(env, "banked r29 write\n");
}

uint32_t HELPER(get_r29_banked)(CPUState *env, uint32_t mode)
{
    cpu_abort(env, "banked r29 read\n");
    return 0;
}

/* UniCore-F64 support.  We follow the convention used for F64 instrunctions:
   Single precition routines have a "s" suffix, double precision a
   "d" suffix.  */

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

uint32_t HELPER(ucf64_get_fpscr)(CPUState *env)
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

void HELPER(ucf64_set_fpscr)(CPUState *env, uint32_t val)
{
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
            cpu_abort(env, "Unsupported UniCore-F64 round mode");
        }
        set_float_rounding_mode(i, &env->ucf64.fp_status);
    }

    i = ucf64_exceptbits_to_host(UCF64_FPSCR_TRAPEN(val));
    set_float_exception_flags(i, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_adds)(float32 a, float32 b, CPUState *env)
{
    return float32_add(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_addd)(float64 a, float64 b, CPUState *env)
{
    return float64_add(a, b, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_subs)(float32 a, float32 b, CPUState *env)
{
    return float32_sub(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_subd)(float64 a, float64 b, CPUState *env)
{
    return float64_sub(a, b, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_muls)(float32 a, float32 b, CPUState *env)
{
    return float32_mul(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_muld)(float64 a, float64 b, CPUState *env)
{
    return float64_mul(a, b, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_divs)(float32 a, float32 b, CPUState *env)
{
    return float32_div(a, b, &env->ucf64.fp_status);
}

float64 HELPER(ucf64_divd)(float64 a, float64 b, CPUState *env)
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

/* XXX: check quiet/signaling case */
void HELPER(ucf64_cmps)(float32 a, float32 b, uint32_t c, CPUState *env)
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

void HELPER(ucf64_cmpd)(float64 a, float64 b, uint32_t c, CPUState *env)
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

static inline float64 ucf64_itod(uint64_t i)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.i = i;
    return v.d;
}

static inline uint64_t ucf64_dtoi(float64 d)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.d = d;
    return v.i;
}

/* Integer to float conversion.  */
float32 HELPER(ucf64_si2sf)(float32 x, CPUState *env)
{
    return int32_to_float32(ucf64_stoi(x), &env->ucf64.fp_status);
}

float64 HELPER(ucf64_si2df)(float32 x, CPUState *env)
{
    return int32_to_float64(ucf64_stoi(x), &env->ucf64.fp_status);
}

/* Float to integer conversion.  */
float32 HELPER(ucf64_sf2si)(float32 x, CPUState *env)
{
    return ucf64_itos(float32_to_int32(x, &env->ucf64.fp_status));
}

float32 HELPER(ucf64_df2si)(float64 x, CPUState *env)
{
    return ucf64_itos(float64_to_int32(x, &env->ucf64.fp_status));
}

/* floating point conversion */
float64 HELPER(ucf64_sf2df)(float32 x, CPUState *env)
{
    return float32_to_float64(x, &env->ucf64.fp_status);
}

float32 HELPER(ucf64_df2sf)(float64 x, CPUState *env)
{
    return float64_to_float32(x, &env->ucf64.fp_status);
}
