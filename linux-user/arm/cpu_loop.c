/*
 *  qemu user cpu loop
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "elf.h"
#include "user/cpu_loop.h"
#include "signal-common.h"
#include "semihosting/common-semi.h"
#include "exec/page-protection.h"
#include "exec/mmap-lock.h"
#include "user/page-protection.h"
#include "target/arm/syndrome.h"

#define get_user_code_u32(x, gaddr, env)                \
    ({ abi_long __r = get_user_u32((x), (gaddr));       \
        if (!__r && bswap_code(arm_sctlr_b(env))) {     \
            (x) = bswap32(x);                           \
        }                                               \
        __r;                                            \
    })

/*
 * Note that if we need to do data accesses here, they should do a
 * bswap if arm_cpu_bswap_data() returns true.
 */

/*
 * Similar to code in accel/tcg/user-exec.c, but outside the execution loop.
 * Must be called with mmap_lock.
 * We get the PC of the entry address - which is as good as anything,
 * on a real kernel what you get depends on which mode it uses.
 */
static void *atomic_mmu_lookup(CPUArchState *env, uint32_t addr, int size)
{
    int need_flags = PAGE_READ | PAGE_WRITE_ORG | PAGE_VALID;
    int page_flags;

    /* Enforce guest required alignment.  */
    if (unlikely(addr & (size - 1))) {
        force_sig_fault(TARGET_SIGBUS, TARGET_BUS_ADRALN, addr);
        return NULL;
    }

    page_flags = page_get_flags(addr);
    if (unlikely((page_flags & need_flags) != need_flags)) {
        force_sig_fault(TARGET_SIGSEGV,
                        page_flags & PAGE_VALID ?
                        TARGET_SEGV_ACCERR : TARGET_SEGV_MAPERR, addr);
        return NULL;
    }

    return g2h(env_cpu(env), addr);
}

/*
 * See the Linux kernel's Documentation/arm/kernel_user_helpers.rst
 * Input:
 * r0 = oldval
 * r1 = newval
 * r2 = pointer to target value
 *
 * Output:
 * r0 = 0 if *ptr was changed, non-0 if no exchange happened
 * C set if *ptr was changed, clear if no exchange happened
 */
static void arm_kernel_cmpxchg32_helper(CPUARMState *env)
{
    uint32_t oldval, newval, val, addr, cpsr, *host_addr;

    /* Swap if host != guest endianness, for the host cmpxchg below */
    oldval = tswap32(env->regs[0]);
    newval = tswap32(env->regs[1]);
    addr = env->regs[2];

    mmap_lock();
    host_addr = atomic_mmu_lookup(env, addr, 4);
    if (!host_addr) {
        mmap_unlock();
        return;
    }

    val = qatomic_cmpxchg__nocheck(host_addr, oldval, newval);
    mmap_unlock();

    cpsr = (val == oldval) * CPSR_C;
    cpsr_write(env, cpsr, CPSR_C, CPSRWriteByInstr);
    env->regs[0] = cpsr ? 0 : -1;
}

/*
 * See the Linux kernel's Documentation/arm/kernel_user_helpers.rst
 * Input:
 * r0 = pointer to oldval
 * r1 = pointer to newval
 * r2 = pointer to target value
 *
 * Output:
 * r0 = 0 if *ptr was changed, non-0 if no exchange happened
 * C set if *ptr was changed, clear if no exchange happened
 *
 * Note segv's in kernel helpers are a bit tricky, we can set the
 * data address sensibly but the PC address is just the entry point.
 */
static void arm_kernel_cmpxchg64_helper(CPUARMState *env)
{
    uint64_t oldval, newval, val;
    uint32_t addr, cpsr;
    uint64_t *host_addr;

    addr = env->regs[0];
    if (get_user_u64(oldval, addr)) {
        goto segv;
    }

    addr = env->regs[1];
    if (get_user_u64(newval, addr)) {
        goto segv;
    }

    mmap_lock();
    addr = env->regs[2];
    host_addr = atomic_mmu_lookup(env, addr, 8);
    if (!host_addr) {
        mmap_unlock();
        return;
    }

    /* Swap if host != guest endianness, for the host cmpxchg below */
    oldval = tswap64(oldval);
    newval = tswap64(newval);

#ifdef CONFIG_ATOMIC64
    val = qatomic_cmpxchg__nocheck(host_addr, oldval, newval);
    cpsr = (val == oldval) * CPSR_C;
#else
    /*
     * This only works between threads, not between processes, but since
     * the host has no 64-bit cmpxchg, it is the best that we can do.
     */
    start_exclusive();
    val = *host_addr;
    if (val == oldval) {
        *host_addr = newval;
        cpsr = CPSR_C;
    } else {
        cpsr = 0;
    }
    end_exclusive();
#endif
    mmap_unlock();

    cpsr_write(env, cpsr, CPSR_C, CPSRWriteByInstr);
    env->regs[0] = cpsr ? 0 : -1;
    return;

 segv:
    force_sig_fault(TARGET_SIGSEGV,
                    page_get_flags(addr) & PAGE_VALID ?
                    TARGET_SEGV_ACCERR : TARGET_SEGV_MAPERR, addr);
}

/* Handle a jump to the kernel code page.  */
static int
do_kernel_trap(CPUARMState *env)
{
    uint32_t addr;

    switch (env->regs[15]) {
    case 0xffff0fa0: /* __kernel_memory_barrier */
        smp_mb();
        break;
    case 0xffff0fc0: /* __kernel_cmpxchg */
        arm_kernel_cmpxchg32_helper(env);
        break;
    case 0xffff0fe0: /* __kernel_get_tls */
        env->regs[0] = cpu_get_tls(env);
        break;
    case 0xffff0f60: /* __kernel_cmpxchg64 */
        arm_kernel_cmpxchg64_helper(env);
        break;

    default:
        return 1;
    }
    /* Jump back to the caller.  */
    addr = env->regs[14];
    if (addr & 1) {
        env->thumb = true;
        addr &= ~1;
    }
    env->regs[15] = addr;

    return 0;
}

static bool insn_is_linux_bkpt(uint32_t opcode, bool is_thumb)
{
    /*
     * Return true if this insn is one of the three magic UDF insns
     * which the kernel treats as breakpoint insns.
     */
    if (!is_thumb) {
        return (opcode & 0x0fffffff) == 0x07f001f0;
    } else {
        /*
         * Note that we get the two halves of the 32-bit T32 insn
         * in the opposite order to the value the kernel uses in
         * its undef_hook struct.
         */
        return ((opcode & 0xffff) == 0xde01) || (opcode == 0xa000f7f0);
    }
}

static bool emulate_arm_fpa11(CPUARMState *env, uint32_t opcode)
{
    TaskState *ts = get_task_state(env_cpu(env));
    int rc = EmulateAll(opcode, &ts->fpa, env);
    int raise, enabled;

    if (rc == 0) {
        /* Illegal instruction */
        return false;
    }
    if (rc > 0) {
        /* Everything ok. */
        env->regs[15] += 4;
        return true;
    }

    /* FP exception */
    rc = -rc;
    raise = 0;

    /* Translate softfloat flags to FPSR flags */
    if (rc & float_flag_invalid) {
        raise |= BIT_IOC;
    }
    if (rc & float_flag_divbyzero) {
        raise |= BIT_DZC;
    }
    if (rc & float_flag_overflow) {
        raise |= BIT_OFC;
    }
    if (rc & float_flag_underflow) {
        raise |= BIT_UFC;
    }
    if (rc & float_flag_inexact) {
        raise |= BIT_IXC;
    }

    /* Accumulate unenabled exceptions */
    enabled = ts->fpa.fpsr >> 16;
    ts->fpa.fpsr |= raise & ~enabled;

    if (raise & enabled) {
        /*
         * The kernel's nwfpe emulator does not pass a real si_code.
         * It merely uses send_sig(SIGFPE, current, 1), which results in
         * __send_signal() filling out SI_KERNEL with pid and uid 0 (under
         * the "SEND_SIG_PRIV" case). That's what our force_sig() does.
         */
        force_sig(TARGET_SIGFPE);
    } else {
        env->regs[15] += 4;
    }
    return true;
}

void cpu_loop(CPUARMState *env)
{
    CPUState *cs = env_cpu(env);
    int trapnr, si_signo, si_code;
    unsigned int n, insn;
    abi_ulong ret;

    for(;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch(trapnr) {
        case EXCP_UDEF:
        case EXCP_NOCP:
        case EXCP_INVSTATE:
            {
                uint32_t opcode;

                /* we handle the FPU emulation here, as Linux */
                /* we get the opcode */
                /* FIXME - what to do if get_user() fails? */
                get_user_code_u32(opcode, env->regs[15], env);

                /*
                 * The Linux kernel treats some UDF patterns specially
                 * to use as breakpoints (instead of the architectural
                 * bkpt insn). These should trigger a SIGTRAP rather
                 * than SIGILL.
                 */
                if (insn_is_linux_bkpt(opcode, env->thumb)) {
                    goto excp_debug;
                }

                if (!env->thumb && emulate_arm_fpa11(env, opcode)) {
                    break;
                }

                force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLOPN,
                                env->regs[15]);
            }
            break;
        case EXCP_SWI:
            {
                env->eabi = true;
                /* system call */
                if (env->thumb) {
                    /* Thumb is always EABI style with syscall number in r7 */
                    n = env->regs[7];
                } else {
                    /*
                     * Equivalent of kernel CONFIG_OABI_COMPAT: read the
                     * Arm SVC insn to extract the immediate, which is the
                     * syscall number in OABI.
                     */
                    /* FIXME - what to do if get_user() fails? */
                    get_user_code_u32(insn, env->regs[15] - 4, env);
                    n = insn & 0xffffff;
                    if (n == 0) {
                        /* zero immediate: EABI, syscall number in r7 */
                        n = env->regs[7];
                    } else {
                        /*
                         * This XOR matches the kernel code: an immediate
                         * in the valid range (0x900000 .. 0x9fffff) is
                         * converted into the correct EABI-style syscall
                         * number; invalid immediates end up as values
                         * > 0xfffff and are handled below as out-of-range.
                         */
                        n ^= ARM_SYSCALL_BASE;
                        env->eabi = false;
                    }
                }

                if (n > ARM_NR_BASE) {
                    switch (n) {
                    case ARM_NR_cacheflush:
                        /* nop */
                        break;
                    case ARM_NR_set_tls:
                        cpu_set_tls(env, env->regs[0]);
                        env->regs[0] = 0;
                        break;
                    case ARM_NR_breakpoint:
                        env->regs[15] -= env->thumb ? 2 : 4;
                        goto excp_debug;
                    case ARM_NR_get_tls:
                        env->regs[0] = cpu_get_tls(env);
                        break;
                    default:
                        if (n < 0xf0800) {
                            /*
                             * Syscalls 0xf0000..0xf07ff (or 0x9f0000..
                             * 0x9f07ff in OABI numbering) are defined
                             * to return -ENOSYS rather than raising
                             * SIGILL. Note that we have already
                             * removed the 0x900000 prefix.
                             */
                            qemu_log_mask(LOG_UNIMP,
                                "qemu: Unsupported ARM syscall: 0x%x\n",
                                          n);
                            env->regs[0] = -TARGET_ENOSYS;
                        } else {
                            /*
                             * Otherwise SIGILL. This includes any SWI with
                             * immediate not originally 0x9fxxxx, because
                             * of the earlier XOR.
                             * Like the real kernel, we report the addr of the
                             * SWI in the siginfo si_addr but leave the PC
                             * pointing at the insn after the SWI.
                             */
                            abi_ulong faultaddr = env->regs[15];
                            faultaddr -= env->thumb ? 2 : 4;
                            force_sig_fault(TARGET_SIGILL, TARGET_ILL_ILLTRP,
                                            faultaddr);
                        }
                        break;
                    }
                } else {
                    ret = do_syscall(env,
                                     n,
                                     env->regs[0],
                                     env->regs[1],
                                     env->regs[2],
                                     env->regs[3],
                                     env->regs[4],
                                     env->regs[5],
                                     0, 0);
                    if (ret == -QEMU_ERESTARTSYS) {
                        env->regs[15] -= env->thumb ? 2 : 4;
                    } else if (ret != -QEMU_ESIGRETURN) {
                        env->regs[0] = ret;
                    }
                }
            }
            break;
        case EXCP_SEMIHOST:
            do_common_semihosting(cs);
            env->regs[15] += env->thumb ? 2 : 4;
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_PREFETCH_ABORT:
        case EXCP_DATA_ABORT:
            /* For user-only we don't set TTBCR_EAE, so look at the FSR. */
            switch (env->exception.fsr & 0x1f) {
            case 0x1: /* Alignment */
                si_signo = TARGET_SIGBUS;
                si_code = TARGET_BUS_ADRALN;
                break;
            case 0x3: /* Access flag fault, level 1 */
            case 0x6: /* Access flag fault, level 2 */
            case 0x9: /* Domain fault, level 1 */
            case 0xb: /* Domain fault, level 2 */
            case 0xd: /* Permission fault, level 1 */
            case 0xf: /* Permission fault, level 2 */
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_ACCERR;
                break;
            case 0x5: /* Translation fault, level 1 */
            case 0x7: /* Translation fault, level 2 */
                si_signo = TARGET_SIGSEGV;
                si_code = TARGET_SEGV_MAPERR;
                break;
            default:
                g_assert_not_reached();
            }
            force_sig_fault(si_signo, si_code, env->exception.vaddress);
            break;
        case EXCP_DEBUG:
        case EXCP_BKPT:
        excp_debug:
            force_sig_fault(TARGET_SIGTRAP, TARGET_TRAP_BRKPT, env->regs[15]);
            break;
        case EXCP_KERNEL_TRAP:
            if (do_kernel_trap(env))
              goto error;
            break;
        case EXCP_YIELD:
            /* nothing to do here for user-mode, just resume guest code */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
        error:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}

void target_cpu_copy_regs(CPUArchState *env, target_pt_regs *regs)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = get_task_state(cpu);
    struct image_info *info = ts->info;
    int i;

    cpsr_write(env, regs->uregs[16], CPSR_USER | CPSR_EXEC,
               CPSRWriteByInstr);
    for(i = 0; i < 16; i++) {
        env->regs[i] = regs->uregs[i];
    }
#if TARGET_BIG_ENDIAN
    /* Enable BE8.  */
    if (EF_ARM_EABI_VERSION(info->elf_flags) >= EF_ARM_EABI_VER4
        && (info->elf_flags & EF_ARM_BE8)) {
        env->uncached_cpsr |= CPSR_E;
        env->cp15.sctlr_el[1] |= SCTLR_E0E;
    } else {
        env->cp15.sctlr_el[1] |= SCTLR_B;
    }
    arm_rebuild_hflags(env);
#endif

    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
}
