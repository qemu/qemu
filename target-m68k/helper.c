/*
 *  m68k op helpers
 *
 *  Copyright (c) 2006-2007 CodeSourcery
 *  Written by Paul Brook
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "cpu.h"
#include "exec-all.h"

enum m68k_cpuid {
    M68K_CPUID_M5206,
    M68K_CPUID_M5208,
    M68K_CPUID_CFV4E,
    M68K_CPUID_ANY,
};

typedef struct m68k_def_t m68k_def_t;

struct m68k_def_t {
    const char * name;
    enum m68k_cpuid id;
};

static m68k_def_t m68k_cpu_defs[] = {
    {"m5206", M68K_CPUID_M5206},
    {"m5208", M68K_CPUID_M5208},
    {"cfv4e", M68K_CPUID_CFV4E},
    {"any", M68K_CPUID_ANY},
    {NULL, 0},
};

static void m68k_set_feature(CPUM68KState *env, int feature)
{
    env->features |= (1u << feature);
}

static int cpu_m68k_set_model(CPUM68KState *env, const char *name)
{
    m68k_def_t *def;

    for (def = m68k_cpu_defs; def->name; def++) {
        if (strcmp(def->name, name) == 0)
            break;
    }
    if (!def->name)
        return -1;

    switch (def->id) {
    case M68K_CPUID_M5206:
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
        break;
    case M68K_CPUID_M5208:
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_APLUSC);
        m68k_set_feature(env, M68K_FEATURE_BRAL);
        m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
        m68k_set_feature(env, M68K_FEATURE_USP);
        break;
    case M68K_CPUID_CFV4E:
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_B);
        m68k_set_feature(env, M68K_FEATURE_BRAL);
        m68k_set_feature(env, M68K_FEATURE_CF_FPU);
        m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
        m68k_set_feature(env, M68K_FEATURE_USP);
        break;
    case M68K_CPUID_ANY:
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_B);
        m68k_set_feature(env, M68K_FEATURE_CF_ISA_APLUSC);
        m68k_set_feature(env, M68K_FEATURE_BRAL);
        m68k_set_feature(env, M68K_FEATURE_CF_FPU);
        /* MAC and EMAC are mututally exclusive, so pick EMAC.
           It's mostly backwards compatible.  */
        m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
        m68k_set_feature(env, M68K_FEATURE_CF_EMAC_B);
        m68k_set_feature(env, M68K_FEATURE_USP);
        m68k_set_feature(env, M68K_FEATURE_EXT_FULL);
        m68k_set_feature(env, M68K_FEATURE_WORD_INDEX);
        break;
    }

    register_m68k_insns(env);
    return 0;
}

void cpu_reset(CPUM68KState *env)
{
    memset(env, 0, offsetof(CPUM68KState, breakpoints));
#if !defined (CONFIG_USER_ONLY)
    env->sr = 0x2700;
#endif
    m68k_switch_sp(env);
    /* ??? FP regs should be initialized to NaN.  */
    env->cc_op = CC_OP_FLAGS;
    /* TODO: We should set PC from the interrupt vector.  */
    env->pc = 0;
    tlb_flush(env, 1);
}

CPUM68KState *cpu_m68k_init(const char *cpu_model)
{
    CPUM68KState *env;

    env = malloc(sizeof(CPUM68KState));
    if (!env)
        return NULL;
    cpu_exec_init(env);

    env->cpu_model_str = cpu_model;

    if (cpu_m68k_set_model(env, cpu_model) < 0) {
        cpu_m68k_close(env);
        return NULL;
    }

    cpu_reset(env);
    return env;
}

void cpu_m68k_close(CPUM68KState *env)
{
    qemu_free(env);
}

void cpu_m68k_flush_flags(CPUM68KState *env, int cc_op)
{
    int flags;
    uint32_t src;
    uint32_t dest;
    uint32_t tmp;

#define HIGHBIT 0x80000000u

#define SET_NZ(x) do { \
    if ((x) == 0) \
        flags |= CCF_Z; \
    else if ((int32_t)(x) < 0) \
        flags |= CCF_N; \
    } while (0)

#define SET_FLAGS_SUB(type, utype) do { \
    SET_NZ((type)dest); \
    tmp = dest + src; \
    if ((utype) tmp < (utype) src) \
        flags |= CCF_C; \
    if ((1u << (sizeof(type) * 8 - 1)) & (tmp ^ dest) & (tmp ^ src)) \
        flags |= CCF_V; \
    } while (0)

    flags = 0;
    src = env->cc_src;
    dest = env->cc_dest;
    switch (cc_op) {
    case CC_OP_FLAGS:
        flags = dest;
        break;
    case CC_OP_LOGIC:
        SET_NZ(dest);
        break;
    case CC_OP_ADD:
        SET_NZ(dest);
        if (dest < src)
            flags |= CCF_C;
        tmp = dest - src;
        if (HIGHBIT & (src ^ dest) & ~(tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SUB:
        SET_FLAGS_SUB(int32_t, uint32_t);
        break;
    case CC_OP_CMPB:
        SET_FLAGS_SUB(int8_t, uint8_t);
        break;
    case CC_OP_CMPW:
        SET_FLAGS_SUB(int16_t, uint16_t);
        break;
    case CC_OP_ADDX:
        SET_NZ(dest);
        if (dest <= src)
            flags |= CCF_C;
        tmp = dest - src - 1;
        if (HIGHBIT & (src ^ dest) & ~(tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SUBX:
        SET_NZ(dest);
        tmp = dest + src + 1;
        if (tmp <= src)
            flags |= CCF_C;
        if (HIGHBIT & (tmp ^ dest) & (tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SHL:
        if (src >= 32) {
            SET_NZ(0);
        } else {
            tmp = dest << src;
            SET_NZ(tmp);
        }
        if (src && src <= 32 && (dest & (1 << (32 - src))))
            flags |= CCF_C;
        break;
    case CC_OP_SHR:
        if (src >= 32) {
            SET_NZ(0);
        } else {
            tmp = dest >> src;
            SET_NZ(tmp);
        }
        if (src && src <= 32 && ((dest >> (src - 1)) & 1))
            flags |= CCF_C;
        break;
    case CC_OP_SAR:
        if (src >= 32) {
            SET_NZ(-1);
        } else {
            tmp = (int32_t)dest >> src;
            SET_NZ(tmp);
        }
        if (src && src <= 32 && (((int32_t)dest >> (src - 1)) & 1))
            flags |= CCF_C;
        break;
    default:
        cpu_abort(env, "Bad CC_OP %d", cc_op);
    }
    env->cc_op = CC_OP_FLAGS;
    env->cc_dest = flags;
}

float64 helper_sub_cmpf64(CPUM68KState *env, float64 src0, float64 src1)
{
    /* ??? This may incorrectly raise exceptions.  */
    /* ??? Should flush denormals to zero.  */
    float64 res;
    res = float64_sub(src0, src1, &env->fp_status);
    if (float64_is_nan(res)) {
        /* +/-inf compares equal against itself, but sub returns nan.  */
        if (!float64_is_nan(src0)
            && !float64_is_nan(src1)) {
            res = float64_zero;
            if (float64_lt_quiet(src0, res, &env->fp_status))
                res = float64_chs(res);
        }
    }
    return res;
}

void helper_movec(CPUM68KState *env, int reg, uint32_t val)
{
    switch (reg) {
    case 0x02: /* CACR */
        env->cacr = val;
        m68k_switch_sp(env);
        break;
    case 0x04: case 0x05: case 0x06: case 0x07: /* ACR[0-3] */
        /* TODO: Implement Access Control Registers.  */
        break;
    case 0x801: /* VBR */
        env->vbr = val;
        break;
    /* TODO: Implement control registers.  */
    default:
        cpu_abort(env, "Unimplemented control register write 0x%x = 0x%x\n",
                  reg, val);
    }
}

void m68k_set_macsr(CPUM68KState *env, uint32_t val)
{
    uint32_t acc;
    int8_t exthigh;
    uint8_t extlow;
    uint64_t regval;
    int i;
    if ((env->macsr ^ val) & (MACSR_FI | MACSR_SU)) {
        for (i = 0; i < 4; i++) {
            regval = env->macc[i];
            exthigh = regval >> 40;
            if (env->macsr & MACSR_FI) {
                acc = regval >> 8;
                extlow = regval;
            } else {
                acc = regval;
                extlow = regval >> 32;
            }
            if (env->macsr & MACSR_FI) {
                regval = (((uint64_t)acc) << 8) | extlow;
                regval |= ((int64_t)exthigh) << 40;
            } else if (env->macsr & MACSR_SU) {
                regval = acc | (((int64_t)extlow) << 32);
                regval |= ((int64_t)exthigh) << 40;
            } else {
                regval = acc | (((uint64_t)extlow) << 32);
                regval |= ((uint64_t)(uint8_t)exthigh) << 40;
            }
            env->macc[i] = regval;
        }
    }
    env->macsr = val;
}

void m68k_switch_sp(CPUM68KState *env)
{
    int new_sp;

    env->sp[env->current_sp] = env->aregs[7];
    new_sp = (env->sr & SR_S && env->cacr & M68K_CACR_EUSP)
             ? M68K_SSP : M68K_USP;
    env->aregs[7] = env->sp[new_sp];
    env->current_sp = new_sp;
}

/* MMU */

/* TODO: This will need fixing once the MMU is implemented.  */
target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

#if defined(CONFIG_USER_ONLY)

int cpu_m68k_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
    env->exception_index = EXCP_ACCESS;
    env->mmu.ar = address;
    return 1;
}

#else

int cpu_m68k_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
    int prot;

    address &= TARGET_PAGE_MASK;
    prot = PAGE_READ | PAGE_WRITE;
    return tlb_set_page(env, address, address, prot, mmu_idx, is_softmmu);
}

/* Notify CPU of a pending interrupt.  Prioritization and vectoring should
   be handled by the interrupt controller.  Real hardware only requests
   the vector when the interrupt is acknowledged by the CPU.  For
   simplicitly we calculate it when the interrupt is signalled.  */
void m68k_set_irq_level(CPUM68KState *env, int level, uint8_t vector)
{
    env->pending_level = level;
    env->pending_vector = vector;
    if (level)
        cpu_interrupt(env, CPU_INTERRUPT_HARD);
    else
        cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
}

#endif
