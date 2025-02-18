/*
 * LOONGARCH gdb server stub
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"
#include "vec.h"

uint64_t read_fcc(CPULoongArchState *env)
{
    uint64_t ret = 0;

    for (int i = 0; i < 8; ++i) {
        ret |= (uint64_t)env->cf[i] << (i * 8);
    }

    return ret;
}

void write_fcc(CPULoongArchState *env, uint64_t val)
{
    for (int i = 0; i < 8; ++i) {
        env->cf[i] = (val >> (i * 8)) & 1;
    }
}

int loongarch_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPULoongArchState *env = cpu_env(cs);

    if (0 <= n && n <= 34) {
        uint64_t val;

        if (n < 32) {
            val = env->gpr[n];
        } else if (n == 32) {
            /* orig_a0 */
            val = 0;
        } else if (n == 33) {
            val = env->pc;
        } else /* if (n == 34) */ {
            val = env->CSR_BADV;
        }

        if (is_la64(env)) {
            return gdb_get_reg64(mem_buf, val);
        } else {
            return gdb_get_reg32(mem_buf, val);
        }
    }

    return 0;
}

int loongarch_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPULoongArchState *env = cpu_env(cs);
    target_ulong tmp;
    int length = 0;

    if (n < 0 || n > 34) {
        return 0;
    }

    if (is_la64(env)) {
        tmp = ldq_le_p(mem_buf);
        length = 8;
    } else {
        tmp = ldl_le_p(mem_buf);
        length = 4;
    }

    if (0 <= n && n < 32) {
        env->gpr[n] = tmp;
    } else if (n == 33) {
        set_pc(env, tmp);
    }
    return length;
}

static int loongarch_gdb_get_fpu(CPUState *cs, GByteArray *mem_buf, int n)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    if (0 <= n && n < 32) {
        return gdb_get_reg64(mem_buf, env->fpr[n].vreg.D(0));
    } else if (32 <= n && n < 40) {
        return gdb_get_reg8(mem_buf, env->cf[n - 32]);
    } else if (n == 40) {
        return gdb_get_reg32(mem_buf, env->fcsr0);
    }
    return 0;
}

static int loongarch_gdb_set_fpu(CPUState *cs, uint8_t *mem_buf, int n)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    int length = 0;

    if (0 <= n && n < 32) {
        env->fpr[n].vreg.D(0) = ldq_le_p(mem_buf);
        length = 8;
    } else if (32 <= n && n < 40) {
        env->cf[n - 32] = ldub_p(mem_buf);
        length = 1;
    } else if (n == 40) {
        env->fcsr0 = ldl_le_p(mem_buf);
        length = 4;
    }
    return length;
}

#define VREG_NUM       32
#define REG64_LEN      64

static int loongarch_gdb_get_vec(CPUState *cs, GByteArray *mem_buf, int n, int vl)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    int i, length = 0;

    if (0 <= n && n < VREG_NUM) {
        for (i = 0; i < vl / REG64_LEN; i++) {
            length += gdb_get_reg64(mem_buf, env->fpr[n].vreg.D(i));
        }
    }

    return length;
}

static int loongarch_gdb_set_vec(CPUState *cs, uint8_t *mem_buf, int n, int vl)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    int i, length = 0;

    if (0 <= n && n < VREG_NUM) {
        for (i = 0; i < vl / REG64_LEN; i++) {
            env->fpr[n].vreg.D(i) = ldq_le_p(mem_buf + 8 * i);
            length += 8;
        }
    }

    return length;
}

static int loongarch_gdb_get_lsx(CPUState *cs, GByteArray *mem_buf, int n)
{
    return loongarch_gdb_get_vec(cs, mem_buf, n, LSX_LEN);
}

static int loongarch_gdb_set_lsx(CPUState *cs, uint8_t *mem_buf, int n)
{
    return loongarch_gdb_set_vec(cs, mem_buf, n, LSX_LEN);
}

static int loongarch_gdb_get_lasx(CPUState *cs, GByteArray *mem_buf, int n)
{
    return loongarch_gdb_get_vec(cs, mem_buf, n, LASX_LEN);
}

static int loongarch_gdb_set_lasx(CPUState *cs, uint8_t *mem_buf, int n)
{
    return loongarch_gdb_set_vec(cs, mem_buf, n, LASX_LEN);
}

void loongarch_cpu_register_gdb_regs_for_features(CPUState *cs)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;

    if (FIELD_EX32(env->cpucfg[2], CPUCFG2, FP)) {
        gdb_register_coprocessor(cs, loongarch_gdb_get_fpu, loongarch_gdb_set_fpu,
                                 gdb_find_static_feature("loongarch-fpu.xml"), 0);
    }

    if (FIELD_EX32(env->cpucfg[2], CPUCFG2, LSX)) {
        gdb_register_coprocessor(cs, loongarch_gdb_get_lsx, loongarch_gdb_set_lsx,
                                 gdb_find_static_feature("loongarch-lsx.xml"), 0);
    }

    if (FIELD_EX32(env->cpucfg[2], CPUCFG2, LASX)) {
        gdb_register_coprocessor(cs, loongarch_gdb_get_lasx, loongarch_gdb_set_lasx,
                                 gdb_find_static_feature("loongarch-lasx.xml"), 0);
    }
}
