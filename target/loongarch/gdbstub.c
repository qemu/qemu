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
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    uint64_t val;

    if (0 <= n && n < 32) {
        val = env->gpr[n];
    } else if (n == 32) {
        /* orig_a0 */
        val = 0;
    } else if (n == 33) {
        val = env->pc;
    } else if (n == 34) {
        val = env->CSR_BADV;
    }

    if (0 <= n && n <= 34) {
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
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    CPULoongArchState *env = &cpu->env;
    target_ulong tmp;
    int read_length;
    int length = 0;

    if (is_la64(env)) {
        tmp = ldq_p(mem_buf);
        read_length = 8;
    } else {
        tmp = ldl_p(mem_buf);
        read_length = 4;
    }

    if (0 <= n && n < 32) {
        env->gpr[n] = tmp;
        length = read_length;
    } else if (n == 33) {
        set_pc(env, tmp);
        length = read_length;
    }
    return length;
}

static int loongarch_gdb_get_fpu(CPULoongArchState *env,
                                 GByteArray *mem_buf, int n)
{
    if (0 <= n && n < 32) {
        return gdb_get_reg64(mem_buf, env->fpr[n].vreg.D(0));
    } else if (32 <= n && n < 40) {
        return gdb_get_reg8(mem_buf, env->cf[n - 32]);
    } else if (n == 40) {
        return gdb_get_reg32(mem_buf, env->fcsr0);
    }
    return 0;
}

static int loongarch_gdb_set_fpu(CPULoongArchState *env,
                                 uint8_t *mem_buf, int n)
{
    int length = 0;

    if (0 <= n && n < 32) {
        env->fpr[n].vreg.D(0) = ldq_p(mem_buf);
        length = 8;
    } else if (32 <= n && n < 40) {
        env->cf[n - 32] = ldub_p(mem_buf);
        length = 1;
    } else if (n == 40) {
        env->fcsr0 = ldl_p(mem_buf);
        length = 4;
    }
    return length;
}

void loongarch_cpu_register_gdb_regs_for_features(CPUState *cs)
{
    gdb_register_coprocessor(cs, loongarch_gdb_get_fpu, loongarch_gdb_set_fpu,
                             41, "loongarch-fpu.xml", 0);
}
