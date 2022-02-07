/*
 *  Microblaze helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>.
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "fpu/softfloat.h"

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
    CPUState *cs = env_cpu(env);

    cs->exception_index = index;
    cpu_loop_exit(cs);
}

static bool check_divz(CPUMBState *env, uint32_t a, uint32_t b, uintptr_t ra)
{
    if (unlikely(b == 0)) {
        env->msr |= MSR_DZ;

        if ((env->msr & MSR_EE) &&
            env_archcpu(env)->cfg.div_zero_exception) {
            CPUState *cs = env_cpu(env);

            env->esr = ESR_EC_DIVZERO;
            cs->exception_index = EXCP_HW_EXCP;
            cpu_loop_exit_restore(cs, ra);
        }
        return false;
    }
    return true;
}

uint32_t helper_divs(CPUMBState *env, uint32_t a, uint32_t b)
{
    if (!check_divz(env, a, b, GETPC())) {
        return 0;
    }
    return (int32_t)a / (int32_t)b;
}

uint32_t helper_divu(CPUMBState *env, uint32_t a, uint32_t b)
{
    if (!check_divz(env, a, b, GETPC())) {
        return 0;
    }
    return a / b;
}

/* raise FPU exception.  */
static void raise_fpu_exception(CPUMBState *env, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    env->esr = ESR_EC_FPU;
    cs->exception_index = EXCP_HW_EXCP;
    cpu_loop_exit_restore(cs, ra);
}

static void update_fpu_flags(CPUMBState *env, int flags, uintptr_t ra)
{
    int raise = 0;

    if (flags & float_flag_invalid) {
        env->fsr |= FSR_IO;
        raise = 1;
    }
    if (flags & float_flag_divbyzero) {
        env->fsr |= FSR_DZ;
        raise = 1;
    }
    if (flags & float_flag_overflow) {
        env->fsr |= FSR_OF;
        raise = 1;
    }
    if (flags & float_flag_underflow) {
        env->fsr |= FSR_UF;
        raise = 1;
    }
    if (raise
        && (env_archcpu(env)->cfg.pvr_regs[2] & PVR2_FPU_EXC_MASK)
        && (env->msr & MSR_EE)) {
        raise_fpu_exception(env, ra);
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
    update_fpu_flags(env, flags, GETPC());
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
    update_fpu_flags(env, flags, GETPC());
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
    update_fpu_flags(env, flags, GETPC());

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
    update_fpu_flags(env, flags, GETPC());

    return fd.l;
}

uint32_t helper_fcmp_un(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    uint32_t r = 0;

    fa.l = a;
    fb.l = b;

    if (float32_is_signaling_nan(fa.f, &env->fp_status) ||
        float32_is_signaling_nan(fb.f, &env->fp_status)) {
        update_fpu_flags(env, float_flag_invalid, GETPC());
        r = 1;
    }

    if (float32_is_quiet_nan(fa.f, &env->fp_status) ||
        float32_is_quiet_nan(fb.f, &env->fp_status)) {
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
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

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
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

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
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());


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
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());
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
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

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
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

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
    update_fpu_flags(env, flags, GETPC());

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
    update_fpu_flags(env, flags, GETPC());

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

void helper_stackprot(CPUMBState *env, target_ulong addr)
{
    if (addr < env->slr || addr > env->shr) {
        CPUState *cs = env_cpu(env);

        qemu_log_mask(CPU_LOG_INT, "Stack protector violation at "
                      TARGET_FMT_lx " %x %x\n",
                      addr, env->slr, env->shr);

        env->ear = addr;
        env->esr = ESR_EC_STACKPROT;
        cs->exception_index = EXCP_HW_EXCP;
        cpu_loop_exit_restore(cs, GETPC());
    }
}

#if !defined(CONFIG_USER_ONLY)
/* Writes/reads to the MMU's special regs end up here.  */
uint32_t helper_mmu_read(CPUMBState *env, uint32_t ext, uint32_t rn)
{
    return mmu_read(env, ext, rn);
}

void helper_mmu_write(CPUMBState *env, uint32_t ext, uint32_t rn, uint32_t v)
{
    mmu_write(env, ext, rn, v);
}

void mb_cpu_transaction_failed(CPUState *cs, hwaddr physaddr, vaddr addr,
                               unsigned size, MMUAccessType access_type,
                               int mmu_idx, MemTxAttrs attrs,
                               MemTxResult response, uintptr_t retaddr)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_INT, "Transaction failed: vaddr 0x%" VADDR_PRIx
                  " physaddr 0x" TARGET_FMT_plx " size %d access type %s\n",
                  addr, physaddr, size,
                  access_type == MMU_INST_FETCH ? "INST_FETCH" :
                  (access_type == MMU_DATA_LOAD ? "DATA_LOAD" : "DATA_STORE"));

    if (!(env->msr & MSR_EE)) {
        return;
    }

    if (access_type == MMU_INST_FETCH) {
        if (!cpu->cfg.iopb_bus_exception) {
            return;
        }
        env->esr = ESR_EC_INSN_BUS;
    } else {
        if (!cpu->cfg.dopb_bus_exception) {
            return;
        }
        env->esr = ESR_EC_DATA_BUS;
    }

    env->ear = addr;
    cs->exception_index = EXCP_HW_EXCP;
    cpu_loop_exit_restore(cs, retaddr);
}
#endif
