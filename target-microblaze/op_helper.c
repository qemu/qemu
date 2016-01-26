/*
 *  Microblaze helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>.
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
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
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/cpu_ldst.h"

#define D(x)

#if !defined(CONFIG_USER_ONLY)

/* Try to fill the TLB and return an exception if error. If retaddr is
 * NULL, it means that the function was called in C code (i.e. not
 * from generated code or from helper.c)
 */
void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;

    ret = mb_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            cpu_restore_state(cs, retaddr);
        }
        cpu_loop_exit(cs);
    }
}
#endif

void helper_put(uint32_t id, uint32_t ctrl, uint32_t data)
{
    int test = ctrl & STREAM_TEST;
    int atomic = ctrl & STREAM_ATOMIC;
    int control = ctrl & STREAM_CONTROL;
    int nonblock = ctrl & STREAM_NONBLOCK;
    int exception = ctrl & STREAM_EXCEPTION;

    qemu_log_mask(LOG_UNIMP, "Unhandled stream put to stream-id=%d data=%x %s%s%s%s%s\n",
             id, data,
             test ? "t" : "",
             nonblock ? "n" : "",
             exception ? "e" : "",
             control ? "c" : "",
             atomic ? "a" : "");
}

uint32_t helper_get(uint32_t id, uint32_t ctrl)
{
    int test = ctrl & STREAM_TEST;
    int atomic = ctrl & STREAM_ATOMIC;
    int control = ctrl & STREAM_CONTROL;
    int nonblock = ctrl & STREAM_NONBLOCK;
    int exception = ctrl & STREAM_EXCEPTION;

    qemu_log_mask(LOG_UNIMP, "Unhandled stream get from stream-id=%d %s%s%s%s%s\n",
             id,
             test ? "t" : "",
             nonblock ? "n" : "",
             exception ? "e" : "",
             control ? "c" : "",
             atomic ? "a" : "");
    return 0xdead0000 | id;
}

void helper_raise_exception(CPUMBState *env, uint32_t index)
{
    CPUState *cs = CPU(mb_env_get_cpu(env));

    cs->exception_index = index;
    cpu_loop_exit(cs);
}

void helper_debug(CPUMBState *env)
{
    int i;

    qemu_log("PC=%8.8x\n", env->sregs[SR_PC]);
    qemu_log("rmsr=%x resr=%x rear=%x debug[%x] imm=%x iflags=%x\n",
             env->sregs[SR_MSR], env->sregs[SR_ESR], env->sregs[SR_EAR],
             env->debug, env->imm, env->iflags);
    qemu_log("btaken=%d btarget=%x mode=%s(saved=%s) eip=%d ie=%d\n",
             env->btaken, env->btarget,
             (env->sregs[SR_MSR] & MSR_UM) ? "user" : "kernel",
             (env->sregs[SR_MSR] & MSR_UMS) ? "user" : "kernel",
             (env->sregs[SR_MSR] & MSR_EIP),
             (env->sregs[SR_MSR] & MSR_IE));
    for (i = 0; i < 32; i++) {
        qemu_log("r%2.2d=%8.8x ", i, env->regs[i]);
        if ((i + 1) % 4 == 0)
            qemu_log("\n");
    }
    qemu_log("\n\n");
}

static inline uint32_t compute_carry(uint32_t a, uint32_t b, uint32_t cin)
{
    uint32_t cout = 0;

    if ((b == ~0) && cin)
        cout = 1;
    else if ((~0 - a) < (b + cin))
        cout = 1;
    return cout;
}

uint32_t helper_cmp(uint32_t a, uint32_t b)
{
    uint32_t t;

    t = b + ~a + 1;
    if ((b & 0x80000000) ^ (a & 0x80000000))
        t = (t & 0x7fffffff) | (b & 0x80000000);
    return t;
}

uint32_t helper_cmpu(uint32_t a, uint32_t b)
{
    uint32_t t;

    t = b + ~a + 1;
    if ((b & 0x80000000) ^ (a & 0x80000000))
        t = (t & 0x7fffffff) | (a & 0x80000000);
    return t;
}

uint32_t helper_clz(uint32_t t0)
{
    return clz32(t0);
}

uint32_t helper_carry(uint32_t a, uint32_t b, uint32_t cf)
{
    return compute_carry(a, b, cf);
}

static inline int div_prepare(CPUMBState *env, uint32_t a, uint32_t b)
{
    if (b == 0) {
        env->sregs[SR_MSR] |= MSR_DZ;

        if ((env->sregs[SR_MSR] & MSR_EE)
            && !(env->pvr.regs[2] & PVR2_DIV_ZERO_EXC_MASK)) {
            env->sregs[SR_ESR] = ESR_EC_DIVZERO;
            helper_raise_exception(env, EXCP_HW_EXCP);
        }
        return 0;
    }
    env->sregs[SR_MSR] &= ~MSR_DZ;
    return 1;
}

uint32_t helper_divs(CPUMBState *env, uint32_t a, uint32_t b)
{
    if (!div_prepare(env, a, b)) {
        return 0;
    }
    return (int32_t)a / (int32_t)b;
}

uint32_t helper_divu(CPUMBState *env, uint32_t a, uint32_t b)
{
    if (!div_prepare(env, a, b)) {
        return 0;
    }
    return a / b;
}

/* raise FPU exception.  */
static void raise_fpu_exception(CPUMBState *env)
{
    env->sregs[SR_ESR] = ESR_EC_FPU;
    helper_raise_exception(env, EXCP_HW_EXCP);
}

static void update_fpu_flags(CPUMBState *env, int flags)
{
    int raise = 0;

    if (flags & float_flag_invalid) {
        env->sregs[SR_FSR] |= FSR_IO;
        raise = 1;
    }
    if (flags & float_flag_divbyzero) {
        env->sregs[SR_FSR] |= FSR_DZ;
        raise = 1;
    }
    if (flags & float_flag_overflow) {
        env->sregs[SR_FSR] |= FSR_OF;
        raise = 1;
    }
    if (flags & float_flag_underflow) {
        env->sregs[SR_FSR] |= FSR_UF;
        raise = 1;
    }
    if (raise
        && (env->pvr.regs[2] & PVR2_FPU_EXC_MASK)
        && (env->sregs[SR_MSR] & MSR_EE)) {
        raise_fpu_exception(env);
    }
}

uint32_t helper_fadd(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_add(fa.f, fb.f, &env->fp_status);

    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags);
    return fd.l;
}

uint32_t helper_frsub(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_sub(fb.f, fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags);
    return fd.l;
}

uint32_t helper_fmul(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_mul(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags);

    return fd.l;
}

uint32_t helper_fdiv(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_div(fb.f, fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags);

    return fd.l;
}

uint32_t helper_fcmp_un(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    uint32_t r = 0;

    fa.l = a;
    fb.l = b;

    if (float32_is_signaling_nan(fa.f) || float32_is_signaling_nan(fb.f)) {
        update_fpu_flags(env, float_flag_invalid);
        r = 1;
    }

    if (float32_is_quiet_nan(fa.f) || float32_is_quiet_nan(fb.f)) {
        r = 1;
    }

    return r;
}

uint32_t helper_fcmp_lt(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int r;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    r = float32_lt(fb.f, fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid);

    return r;
}

uint32_t helper_fcmp_eq(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags;
    int r;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    r = float32_eq_quiet(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid);

    return r;
}

uint32_t helper_fcmp_le(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags;
    int r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = float32_le(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid);


    return r;
}

uint32_t helper_fcmp_gt(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags, r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = float32_lt(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid);
    return r;
}

uint32_t helper_fcmp_ne(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags, r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = !float32_eq_quiet(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid);

    return r;
}

uint32_t helper_fcmp_ge(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags, r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = !float32_lt(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid);

    return r;
}

uint32_t helper_flt(CPUMBState *env, uint32_t a)
{
    CPU_FloatU fd, fa;

    fa.l = a;
    fd.f = int32_to_float32(fa.l, &env->fp_status);
    return fd.l;
}

uint32_t helper_fint(CPUMBState *env, uint32_t a)
{
    CPU_FloatU fa;
    uint32_t r;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    r = float32_to_int32(fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags);

    return r;
}

uint32_t helper_fsqrt(CPUMBState *env, uint32_t a)
{
    CPU_FloatU fd, fa;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fd.l = float32_sqrt(fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags);

    return fd.l;
}

uint32_t helper_pcmpbf(uint32_t a, uint32_t b)
{
    unsigned int i;
    uint32_t mask = 0xff000000;

    for (i = 0; i < 4; i++) {
        if ((a & mask) == (b & mask))
            return i + 1;
        mask >>= 8;
    }
    return 0;
}

void helper_memalign(CPUMBState *env, uint32_t addr, uint32_t dr, uint32_t wr,
                     uint32_t mask)
{
    if (addr & mask) {
            qemu_log_mask(CPU_LOG_INT,
                          "unaligned access addr=%x mask=%x, wr=%d dr=r%d\n",
                          addr, mask, wr, dr);
            env->sregs[SR_EAR] = addr;
            env->sregs[SR_ESR] = ESR_EC_UNALIGNED_DATA | (wr << 10) \
                                 | (dr & 31) << 5;
            if (mask == 3) {
                env->sregs[SR_ESR] |= 1 << 11;
            }
            if (!(env->sregs[SR_MSR] & MSR_EE)) {
                return;
            }
            helper_raise_exception(env, EXCP_HW_EXCP);
    }
}

void helper_stackprot(CPUMBState *env, uint32_t addr)
{
    if (addr < env->slr || addr > env->shr) {
        qemu_log_mask(CPU_LOG_INT, "Stack protector violation at %x %x %x\n",
                      addr, env->slr, env->shr);
        env->sregs[SR_EAR] = addr;
        env->sregs[SR_ESR] = ESR_EC_STACKPROT;
        helper_raise_exception(env, EXCP_HW_EXCP);
    }
}

#if !defined(CONFIG_USER_ONLY)
/* Writes/reads to the MMU's special regs end up here.  */
uint32_t helper_mmu_read(CPUMBState *env, uint32_t rn)
{
    return mmu_read(env, rn);
}

void helper_mmu_write(CPUMBState *env, uint32_t rn, uint32_t v)
{
    mmu_write(env, rn, v);
}

void mb_cpu_unassigned_access(CPUState *cs, hwaddr addr,
                              bool is_write, bool is_exec, int is_asi,
                              unsigned size)
{
    MicroBlazeCPU *cpu;
    CPUMBState *env;

    qemu_log_mask(CPU_LOG_INT, "Unassigned " TARGET_FMT_plx " wr=%d exe=%d\n",
             addr, is_write ? 1 : 0, is_exec ? 1 : 0);
    if (cs == NULL) {
        return;
    }
    cpu = MICROBLAZE_CPU(cs);
    env = &cpu->env;
    if (!(env->sregs[SR_MSR] & MSR_EE)) {
        return;
    }

    env->sregs[SR_EAR] = addr;
    if (is_exec) {
        if ((env->pvr.regs[2] & PVR2_IOPB_BUS_EXC_MASK)) {
            env->sregs[SR_ESR] = ESR_EC_INSN_BUS;
            helper_raise_exception(env, EXCP_HW_EXCP);
        }
    } else {
        if ((env->pvr.regs[2] & PVR2_DOPB_BUS_EXC_MASK)) {
            env->sregs[SR_ESR] = ESR_EC_DATA_BUS;
            helper_raise_exception(env, EXCP_HW_EXCP);
        }
    }
}
#endif
