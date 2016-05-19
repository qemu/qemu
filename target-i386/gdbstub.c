/*
 * x86 gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "qemu-common.h"
#include "cpu.h"
#include "exec/gdbstub.h"

#ifdef TARGET_X86_64
static const int gpr_map[16] = {
    R_EAX, R_EBX, R_ECX, R_EDX, R_ESI, R_EDI, R_EBP, R_ESP,
    8, 9, 10, 11, 12, 13, 14, 15
};
#else
#define gpr_map gpr_map32
#endif
static const int gpr_map32[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

#define IDX_IP_REG      CPU_NB_REGS
#define IDX_FLAGS_REG   (IDX_IP_REG + 1)
#define IDX_SEG_REGS    (IDX_FLAGS_REG + 1)
#define IDX_FP_REGS     (IDX_SEG_REGS + 6)
#define IDX_XMM_REGS    (IDX_FP_REGS + 16)
#define IDX_MXCSR_REG   (IDX_XMM_REGS + CPU_NB_REGS)

int x86_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (n < CPU_NB_REGS) {
        if (TARGET_LONG_BITS == 64 && env->hflags & HF_CS64_MASK) {
            return gdb_get_reg64(mem_buf, env->regs[gpr_map[n]]);
        } else if (n < CPU_NB_REGS32) {
            return gdb_get_reg32(mem_buf, env->regs[gpr_map32[n]]);
        }
    } else if (n >= IDX_FP_REGS && n < IDX_FP_REGS + 8) {
#ifdef USE_X86LDOUBLE
        /* FIXME: byteswap float values - after fixing fpregs layout. */
        memcpy(mem_buf, &env->fpregs[n - IDX_FP_REGS], 10);
#else
        memset(mem_buf, 0, 10);
#endif
        return 10;
    } else if (n >= IDX_XMM_REGS && n < IDX_XMM_REGS + CPU_NB_REGS) {
        n -= IDX_XMM_REGS;
        if (n < CPU_NB_REGS32 ||
            (TARGET_LONG_BITS == 64 && env->hflags & HF_CS64_MASK)) {
            stq_p(mem_buf, env->xmm_regs[n].ZMM_Q(0));
            stq_p(mem_buf + 8, env->xmm_regs[n].ZMM_Q(1));
            return 16;
        }
    } else {
        switch (n) {
        case IDX_IP_REG:
            if (TARGET_LONG_BITS == 64 && env->hflags & HF_CS64_MASK) {
                return gdb_get_reg64(mem_buf, env->eip);
            } else {
                return gdb_get_reg32(mem_buf, env->eip);
            }
        case IDX_FLAGS_REG:
            return gdb_get_reg32(mem_buf, env->eflags);

        case IDX_SEG_REGS:
            return gdb_get_reg32(mem_buf, env->segs[R_CS].selector);
        case IDX_SEG_REGS + 1:
            return gdb_get_reg32(mem_buf, env->segs[R_SS].selector);
        case IDX_SEG_REGS + 2:
            return gdb_get_reg32(mem_buf, env->segs[R_DS].selector);
        case IDX_SEG_REGS + 3:
            return gdb_get_reg32(mem_buf, env->segs[R_ES].selector);
        case IDX_SEG_REGS + 4:
            return gdb_get_reg32(mem_buf, env->segs[R_FS].selector);
        case IDX_SEG_REGS + 5:
            return gdb_get_reg32(mem_buf, env->segs[R_GS].selector);

        case IDX_FP_REGS + 8:
            return gdb_get_reg32(mem_buf, env->fpuc);
        case IDX_FP_REGS + 9:
            return gdb_get_reg32(mem_buf, (env->fpus & ~0x3800) |
                                          (env->fpstt & 0x7) << 11);
        case IDX_FP_REGS + 10:
            return gdb_get_reg32(mem_buf, 0); /* ftag */
        case IDX_FP_REGS + 11:
            return gdb_get_reg32(mem_buf, 0); /* fiseg */
        case IDX_FP_REGS + 12:
            return gdb_get_reg32(mem_buf, 0); /* fioff */
        case IDX_FP_REGS + 13:
            return gdb_get_reg32(mem_buf, 0); /* foseg */
        case IDX_FP_REGS + 14:
            return gdb_get_reg32(mem_buf, 0); /* fooff */
        case IDX_FP_REGS + 15:
            return gdb_get_reg32(mem_buf, 0); /* fop */

        case IDX_MXCSR_REG:
            return gdb_get_reg32(mem_buf, env->mxcsr);
        }
    }
    return 0;
}

static int x86_cpu_gdb_load_seg(X86CPU *cpu, int sreg, uint8_t *mem_buf)
{
    CPUX86State *env = &cpu->env;
    uint16_t selector = ldl_p(mem_buf);

    if (selector != env->segs[sreg].selector) {
#if defined(CONFIG_USER_ONLY)
        cpu_x86_load_seg(env, sreg, selector);
#else
        unsigned int limit, flags;
        target_ulong base;

        if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
            int dpl = (env->eflags & VM_MASK) ? 3 : 0;
            base = selector << 4;
            limit = 0xffff;
            flags = DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                    DESC_A_MASK | (dpl << DESC_DPL_SHIFT);
        } else {
            if (!cpu_x86_get_descr_debug(env, selector, &base, &limit,
                                         &flags)) {
                return 4;
            }
        }
        cpu_x86_load_seg_cache(env, sreg, selector, base, limit, flags);
#endif
    }
    return 4;
}

int x86_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint32_t tmp;

    if (n < CPU_NB_REGS) {
        if (TARGET_LONG_BITS == 64 && env->hflags & HF_CS64_MASK) {
            env->regs[gpr_map[n]] = ldtul_p(mem_buf);
            return sizeof(target_ulong);
        } else if (n < CPU_NB_REGS32) {
            n = gpr_map32[n];
            env->regs[n] &= ~0xffffffffUL;
            env->regs[n] |= (uint32_t)ldl_p(mem_buf);
            return 4;
        }
    } else if (n >= IDX_FP_REGS && n < IDX_FP_REGS + 8) {
#ifdef USE_X86LDOUBLE
        /* FIXME: byteswap float values - after fixing fpregs layout. */
        memcpy(&env->fpregs[n - IDX_FP_REGS], mem_buf, 10);
#endif
        return 10;
    } else if (n >= IDX_XMM_REGS && n < IDX_XMM_REGS + CPU_NB_REGS) {
        n -= IDX_XMM_REGS;
        if (n < CPU_NB_REGS32 ||
            (TARGET_LONG_BITS == 64 && env->hflags & HF_CS64_MASK)) {
            env->xmm_regs[n].ZMM_Q(0) = ldq_p(mem_buf);
            env->xmm_regs[n].ZMM_Q(1) = ldq_p(mem_buf + 8);
            return 16;
        }
    } else {
        switch (n) {
        case IDX_IP_REG:
            if (TARGET_LONG_BITS == 64 && env->hflags & HF_CS64_MASK) {
                env->eip = ldq_p(mem_buf);
                return 8;
            } else {
                env->eip &= ~0xffffffffUL;
                env->eip |= (uint32_t)ldl_p(mem_buf);
                return 4;
            }
        case IDX_FLAGS_REG:
            env->eflags = ldl_p(mem_buf);
            return 4;

        case IDX_SEG_REGS:
            return x86_cpu_gdb_load_seg(cpu, R_CS, mem_buf);
        case IDX_SEG_REGS + 1:
            return x86_cpu_gdb_load_seg(cpu, R_SS, mem_buf);
        case IDX_SEG_REGS + 2:
            return x86_cpu_gdb_load_seg(cpu, R_DS, mem_buf);
        case IDX_SEG_REGS + 3:
            return x86_cpu_gdb_load_seg(cpu, R_ES, mem_buf);
        case IDX_SEG_REGS + 4:
            return x86_cpu_gdb_load_seg(cpu, R_FS, mem_buf);
        case IDX_SEG_REGS + 5:
            return x86_cpu_gdb_load_seg(cpu, R_GS, mem_buf);

        case IDX_FP_REGS + 8:
            cpu_set_fpuc(env, ldl_p(mem_buf));
            return 4;
        case IDX_FP_REGS + 9:
            tmp = ldl_p(mem_buf);
            env->fpstt = (tmp >> 11) & 7;
            env->fpus = tmp & ~0x3800;
            return 4;
        case IDX_FP_REGS + 10: /* ftag */
            return 4;
        case IDX_FP_REGS + 11: /* fiseg */
            return 4;
        case IDX_FP_REGS + 12: /* fioff */
            return 4;
        case IDX_FP_REGS + 13: /* foseg */
            return 4;
        case IDX_FP_REGS + 14: /* fooff */
            return 4;
        case IDX_FP_REGS + 15: /* fop */
            return 4;

        case IDX_MXCSR_REG:
            cpu_set_mxcsr(env, ldl_p(mem_buf));
            return 4;
        }
    }
    /* Unrecognised register.  */
    return 0;
}
