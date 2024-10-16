/*
 * x86 gdb server stub
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
#include "accel/tcg/vcpu-state.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"
#ifdef CONFIG_LINUX_USER
#include "linux-user/qemu.h"
#endif

#ifdef TARGET_X86_64
static const int gpr_map[16] = {
    R_EAX, R_EBX, R_ECX, R_EDX, R_ESI, R_EDI, R_EBP, R_ESP,
    8, 9, 10, 11, 12, 13, 14, 15
};
#else
#define gpr_map gpr_map32
#endif
static const int gpr_map32[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

/*
 * Keep these in sync with assignment to
 * gdb_num_core_regs in target/i386/cpu.c
 * and with the machine description
 */

/*
 * SEG: 6 segments, plus fs_base, gs_base, kernel_gs_base
 */

/*
 * general regs ----->  8 or 16
 */
#define IDX_NB_IP       1
#define IDX_NB_FLAGS    1
#define IDX_NB_SEG      (6 + 3)
#define IDX_NB_CTL      6
#define IDX_NB_FP       16
/*
 * fpu regs ----------> 8 or 16
 */
#define IDX_NB_MXCSR    1
/*
 *          total ----> 8+1+1+9+6+16+8+1=50 or 16+1+1+9+6+16+16+1=66
 */

#define IDX_IP_REG      CPU_NB_REGS
#define IDX_FLAGS_REG   (IDX_IP_REG + IDX_NB_IP)
#define IDX_SEG_REGS    (IDX_FLAGS_REG + IDX_NB_FLAGS)
#define IDX_CTL_REGS    (IDX_SEG_REGS + IDX_NB_SEG)
#define IDX_FP_REGS     (IDX_CTL_REGS + IDX_NB_CTL)
#define IDX_XMM_REGS    (IDX_FP_REGS + IDX_NB_FP)
#define IDX_MXCSR_REG   (IDX_XMM_REGS + CPU_NB_REGS)

#define IDX_CTL_CR0_REG     (IDX_CTL_REGS + 0)
#define IDX_CTL_CR2_REG     (IDX_CTL_REGS + 1)
#define IDX_CTL_CR3_REG     (IDX_CTL_REGS + 2)
#define IDX_CTL_CR4_REG     (IDX_CTL_REGS + 3)
#define IDX_CTL_CR8_REG     (IDX_CTL_REGS + 4)
#define IDX_CTL_EFER_REG    (IDX_CTL_REGS + 5)

#ifdef TARGET_X86_64
#define GDB_FORCE_64 1
#else
#define GDB_FORCE_64 0
#endif

static int gdb_read_reg_cs64(uint32_t hflags, GByteArray *buf, target_ulong val)
{
    if ((hflags & HF_CS64_MASK) || GDB_FORCE_64) {
        return gdb_get_reg64(buf, val);
    }
    return gdb_get_reg32(buf, val);
}

static int gdb_write_reg_cs64(uint32_t hflags, uint8_t *buf, target_ulong *val)
{
    if (hflags & HF_CS64_MASK) {
        *val = ldq_p(buf);
        return 8;
    }
    *val = ldl_p(buf);
    return 4;
}

static int gdb_get_reg(CPUX86State *env, GByteArray *mem_buf, target_ulong val)
{
    if (TARGET_LONG_BITS == 64) {
        if (env->hflags & HF_CS64_MASK) {
            return gdb_get_reg64(mem_buf, val);
        } else {
            return gdb_get_reg64(mem_buf, val & 0xffffffffUL);
        }
    } else {
        return gdb_get_reg32(mem_buf, val);
    }
}

int x86_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    uint64_t tpr;

    /* N.B. GDB can't deal with changes in registers or sizes in the middle
       of a session. So if we're in 32-bit mode on a 64-bit cpu, still act
       as if we're on a 64-bit cpu. */

    if (n < CPU_NB_REGS) {
        if (TARGET_LONG_BITS == 64) {
            if (env->hflags & HF_CS64_MASK) {
                return gdb_get_reg64(mem_buf, env->regs[gpr_map[n]]);
            } else if (n < CPU_NB_REGS32) {
                return gdb_get_reg64(mem_buf,
                                     env->regs[gpr_map[n]] & 0xffffffffUL);
            } else {
                return gdb_get_regl(mem_buf, 0);
            }
        } else {
            return gdb_get_reg32(mem_buf, env->regs[gpr_map32[n]]);
        }
    } else if (n >= IDX_FP_REGS && n < IDX_FP_REGS + 8) {
        int st_index = n - IDX_FP_REGS;
        int r_index = (st_index + env->fpstt) % 8;
        floatx80 *fp = &env->fpregs[r_index].d;
        int len = gdb_get_reg64(mem_buf, cpu_to_le64(fp->low));
        len += gdb_get_reg16(mem_buf, cpu_to_le16(fp->high));
        return len;
    } else if (n >= IDX_XMM_REGS && n < IDX_XMM_REGS + CPU_NB_REGS) {
        n -= IDX_XMM_REGS;
        if (n < CPU_NB_REGS32 || TARGET_LONG_BITS == 64) {
            return gdb_get_reg128(mem_buf,
                                  env->xmm_regs[n].ZMM_Q(1),
                                  env->xmm_regs[n].ZMM_Q(0));
        }
    } else {
        switch (n) {
        case IDX_IP_REG:
            return gdb_get_reg(env, mem_buf, env->eip);
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
        case IDX_SEG_REGS + 6:
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->segs[R_FS].base);
        case IDX_SEG_REGS + 7:
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->segs[R_GS].base);

        case IDX_SEG_REGS + 8:
#ifdef TARGET_X86_64
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->kernelgsbase);
#else
            return gdb_get_reg32(mem_buf, 0);
#endif

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
            update_mxcsr_from_sse_status(env);
            return gdb_get_reg32(mem_buf, env->mxcsr);

        case IDX_CTL_CR0_REG:
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->cr[0]);
        case IDX_CTL_CR2_REG:
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->cr[2]);
        case IDX_CTL_CR3_REG:
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->cr[3]);
        case IDX_CTL_CR4_REG:
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->cr[4]);
        case IDX_CTL_CR8_REG:
#ifndef CONFIG_USER_ONLY
            tpr = cpu_get_apic_tpr(cpu->apic_state);
#else
            tpr = 0;
#endif
            return gdb_read_reg_cs64(env->hflags, mem_buf, tpr);

        case IDX_CTL_EFER_REG:
            return gdb_read_reg_cs64(env->hflags, mem_buf, env->efer);
        }
    }
    return 0;
}

static int x86_cpu_gdb_load_seg(X86CPU *cpu, X86Seg sreg, uint8_t *mem_buf)
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

static int gdb_write_reg(CPUX86State *env, uint8_t *mem_buf, target_ulong *val)
{
    if (TARGET_LONG_BITS == 64) {
        if (env->hflags & HF_CS64_MASK) {
            *val = ldq_p(mem_buf);
        } else {
            *val = ldq_p(mem_buf) & 0xffffffffUL;
        }
        return 8;
    } else {
        *val = (uint32_t)ldl_p(mem_buf);
        return 4;
    }
}

int x86_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    target_ulong tmp;
    int len;

    /* N.B. GDB can't deal with changes in registers or sizes in the middle
       of a session. So if we're in 32-bit mode on a 64-bit cpu, still act
       as if we're on a 64-bit cpu. */

    if (n < CPU_NB_REGS) {
        if (TARGET_LONG_BITS == 64) {
            if (env->hflags & HF_CS64_MASK) {
                env->regs[gpr_map[n]] = ldtul_p(mem_buf);
            } else if (n < CPU_NB_REGS32) {
                env->regs[gpr_map[n]] = ldtul_p(mem_buf) & 0xffffffffUL;
            }
            return sizeof(target_ulong);
        } else if (n < CPU_NB_REGS32) {
            n = gpr_map32[n];
            env->regs[n] &= ~0xffffffffUL;
            env->regs[n] |= (uint32_t)ldl_p(mem_buf);
            return 4;
        }
    } else if (n >= IDX_FP_REGS && n < IDX_FP_REGS + 8) {
        floatx80 *fp = (floatx80 *) &env->fpregs[n - IDX_FP_REGS];
        fp->low = le64_to_cpu(* (uint64_t *) mem_buf);
        fp->high = le16_to_cpu(* (uint16_t *) (mem_buf + 8));
        return 10;
    } else if (n >= IDX_XMM_REGS && n < IDX_XMM_REGS + CPU_NB_REGS) {
        n -= IDX_XMM_REGS;
        if (n < CPU_NB_REGS32 || TARGET_LONG_BITS == 64) {
            env->xmm_regs[n].ZMM_Q(0) = ldq_p(mem_buf);
            env->xmm_regs[n].ZMM_Q(1) = ldq_p(mem_buf + 8);
            return 16;
        }
    } else {
        switch (n) {
        case IDX_IP_REG:
            return gdb_write_reg(env, mem_buf, &env->eip);
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
        case IDX_SEG_REGS + 6:
            return gdb_write_reg_cs64(env->hflags, mem_buf, &env->segs[R_FS].base);
        case IDX_SEG_REGS + 7:
            return gdb_write_reg_cs64(env->hflags, mem_buf, &env->segs[R_GS].base);
        case IDX_SEG_REGS + 8:
#ifdef TARGET_X86_64
            return gdb_write_reg_cs64(env->hflags, mem_buf, &env->kernelgsbase);
#endif
            return 4;

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

        case IDX_CTL_CR0_REG:
            len = gdb_write_reg_cs64(env->hflags, mem_buf, &tmp);
#ifndef CONFIG_USER_ONLY
            cpu_x86_update_cr0(env, tmp);
#endif
            return len;

        case IDX_CTL_CR2_REG:
            len = gdb_write_reg_cs64(env->hflags, mem_buf, &tmp);
#ifndef CONFIG_USER_ONLY
            env->cr[2] = tmp;
#endif
            return len;

        case IDX_CTL_CR3_REG:
            len = gdb_write_reg_cs64(env->hflags, mem_buf, &tmp);
#ifndef CONFIG_USER_ONLY
            cpu_x86_update_cr3(env, tmp);
#endif
            return len;

        case IDX_CTL_CR4_REG:
            len = gdb_write_reg_cs64(env->hflags, mem_buf, &tmp);
#ifndef CONFIG_USER_ONLY
            cpu_x86_update_cr4(env, tmp);
#endif
            return len;

        case IDX_CTL_CR8_REG:
            len = gdb_write_reg_cs64(env->hflags, mem_buf, &tmp);
#ifndef CONFIG_USER_ONLY
            cpu_set_apic_tpr(cpu->apic_state, tmp);
#endif
            return len;

        case IDX_CTL_EFER_REG:
            len = gdb_write_reg_cs64(env->hflags, mem_buf, &tmp);
#ifndef CONFIG_USER_ONLY
            cpu_load_efer(env, tmp);
#endif
            return len;
        }
    }
    /* Unrecognised register.  */
    return 0;
}

#ifdef CONFIG_LINUX_USER

#define IDX_ORIG_AX 0

static int x86_cpu_gdb_read_linux_register(CPUState *cs, GByteArray *mem_buf,
                                           int n)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    switch (n) {
    case IDX_ORIG_AX:
        return gdb_get_reg(env, mem_buf, get_task_state(cs)->orig_ax);
    }
    return 0;
}

static int x86_cpu_gdb_write_linux_register(CPUState *cs, uint8_t *mem_buf,
                                            int n)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    switch (n) {
    case IDX_ORIG_AX:
        return gdb_write_reg(env, mem_buf, &get_task_state(cs)->orig_ax);
    }
    return 0;
}

#endif

void x86_cpu_gdb_init(CPUState *cs)
{
#ifdef CONFIG_LINUX_USER
    gdb_register_coprocessor(cs, x86_cpu_gdb_read_linux_register,
                             x86_cpu_gdb_write_linux_register,
#ifdef TARGET_X86_64
                             gdb_find_static_feature("i386-64bit-linux.xml"),
#else
                             gdb_find_static_feature("i386-32bit-linux.xml"),
#endif
                             0);
#endif
}
