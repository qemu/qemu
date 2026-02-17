/*
 * SPARC gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
#include "exec/gdbstub.h"
#include "cpu.h"
#include "gdbstub/helpers.h"

static inline int gdb_get_rega(GByteArray *buf, uint64_t val)
{
#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    return gdb_get_reg32(buf, val);
#else
    return gdb_get_reg64(buf, val);
#endif
}

int sparc_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPUSPARCState *env = cpu_env(cs);

    if (n < 8) {
        /* g0..g7 */
        return gdb_get_rega(mem_buf, env->gregs[n]);
    }
    if (n < 32) {
        /* register window */
        return gdb_get_rega(mem_buf, env->regwptr[n - 8]);
    }
    return 0;
}

static int sparc_fpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPUSPARCState *env = cpu_env(cs);

#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    if (n < 32) {
        /* fprs */
        if (n & 1) {
            return gdb_get_reg32(mem_buf, env->fpr[n / 2].l.lower);
        } else {
            return gdb_get_reg32(mem_buf, env->fpr[n / 2].l.upper);
        }
    }
#else
    if (n < 32) {
        /* f0-f31 */
        if (n & 1) {
            return gdb_get_reg32(mem_buf, env->fpr[n / 2].l.lower);
        } else {
            return gdb_get_reg32(mem_buf, env->fpr[n / 2].l.upper);
        }
    }
    if (n < 48) {
        /* f32-f62 (16 double width registers, even register numbers only)
         * n == 32: f32 : env->fpr[16]
         * n == 33: f34 : env->fpr[17]
         * etc...
         * n == 47: f62 : env->fpr[31]
         */
        return gdb_get_reg64(mem_buf, env->fpr[(n - 32) + 16].ll);
    }
#endif
    return 0;
}

static int sparc_cp0_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPUSPARCState *env = cpu_env(cs);

#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    switch (n) {
    case 0:
        return gdb_get_rega(mem_buf, env->y);
    case 1:
        return gdb_get_rega(mem_buf, cpu_get_psr(env));
    case 2:
        return gdb_get_rega(mem_buf, env->wim);
    case 3:
        return gdb_get_rega(mem_buf, env->tbr);
    case 4:
        return gdb_get_rega(mem_buf, env->pc);
    case 5:
        return gdb_get_rega(mem_buf, env->npc);
    case 6:
        return gdb_get_rega(mem_buf, cpu_get_fsr(env));
    case 7:
        return gdb_get_rega(mem_buf, 0); /* csr */
    }
#else
    switch (n) {
    case 0:
        return gdb_get_regl(mem_buf, env->pc);
    case 1:
        return gdb_get_regl(mem_buf, env->npc);
    case 2:
        return gdb_get_regl(mem_buf, (cpu_get_ccr(env) << 32) |
                                     ((env->asi & 0xff) << 24) |
                                     ((env->pstate & 0xfff) << 8) |
                                     cpu_get_cwp64(env));
    case 3:
        return gdb_get_regl(mem_buf, cpu_get_fsr(env));
    case 4:
        return gdb_get_regl(mem_buf, env->fprs);
    case 5:
        return gdb_get_regl(mem_buf, env->y);
    }
#endif
    return 0;
}

static unsigned sparc_gdb_register_bytes(void)
{
#ifdef CONFIG_USER_ONLY
# if defined(TARGET_ABI32)
    return 4;
# endif
#endif
    return target_long_bits() / 8;
}

int sparc_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    SPARCCPU *cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;
    const unsigned regsz = sparc_gdb_register_bytes();
    uint64_t tmp = ldn_p(mem_buf, regsz);

    if (n < 8) {
        /* g0..g7 */
        env->gregs[n] = tmp;
    } else {
        /* register window */
        env->regwptr[n - 8] = tmp;
    }
    return regsz;
}

static int sparc_fpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUSPARCState *env = cpu_env(cs);

#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    /* fprs */
    /* f0-f31 */
    if (n & 1) {
        env->fpr[n / 2].l.lower = tmp;
    } else {
        env->fpr[n / 2].l.upper = tmp;
    }

    return 4;
#else
    if (n < 32) {
        /* f0-f31 */
        uint32_t tmp = ldl_p(mem_buf);
        if (n & 1) {
            env->fpr[n / 2].l.lower = tmp;
        } else {
            env->fpr[n  / 2].l.upper = tmp;
        }
        return 4;
    } else {
        uint64_t tmp = ldq_p(mem_buf);
        /* f32-f62 (16 double width registers, even register numbers only)
         * n == 32: f32 : env->fpr[16]
         * n == 33: f34 : env->fpr[17]
         * etc...
         * n == 47: f62 : env->fpr[31]
         */
        env->fpr[(n - 32) + 16].ll = tmp;
    }
    return 8;
#endif
}

static int sparc_cp0_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUSPARCState *env = cpu_env(cs);

#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    switch (n) {
    case 0:
        env->y = tmp;
        break;
    case 1:
        cpu_put_psr(env, tmp);
        break;
    case 2:
        env->wim = tmp;
        break;
    case 3:
        env->tbr = tmp;
        break;
    case 4:
        env->pc = tmp;
        break;
    case 5:
        env->npc = tmp;
        break;
    case 6:
        cpu_put_fsr(env, tmp);
        break;
    default:
        return 0;
    }
    return 4;
#else
    uint64_t tmp;

    tmp = ldq_p(mem_buf);

    switch (n) {
    case 0:
        env->pc = tmp;
        break;
    case 1:
        env->npc = tmp;
        break;
    case 2:
        cpu_put_ccr(env, tmp >> 32);
        env->asi = (tmp >> 24) & 0xff;
        env->pstate = (tmp >> 8) & 0xfff;
        cpu_put_cwp64(env, tmp & 0xff);
        break;
    case 3:
        cpu_put_fsr(env, tmp);
        break;
    case 4:
        env->fprs = tmp;
        break;
    case 5:
        env->y = tmp;
        break;
    default:
        return 0;
    }
    return 8;
#endif
}

void sparc_cpu_register_gdb_regs(CPUState *cs)
{
#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    gdb_register_coprocessor(cs, sparc_fpu_gdb_read_register,
                             sparc_fpu_gdb_write_register,
                             gdb_find_static_feature("sparc32-fpu.xml"),
                             0);
    gdb_register_coprocessor(cs, sparc_cp0_gdb_read_register,
                             sparc_cp0_gdb_write_register,
                             gdb_find_static_feature("sparc32-cp0.xml"),
                             0);
#else
    gdb_register_coprocessor(cs, sparc_fpu_gdb_read_register,
                             sparc_fpu_gdb_write_register,
                             gdb_find_static_feature("sparc64-fpu.xml"),
                             0);
    gdb_register_coprocessor(cs, sparc_cp0_gdb_read_register,
                             sparc_cp0_gdb_write_register,
                             gdb_find_static_feature("sparc64-cp0.xml"),
                             0);
#endif
}
