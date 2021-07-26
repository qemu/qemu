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
#include "qemu-common.h"
#include "qemu.h"
#include "cpu_loop-common.h"
#include "elf.h"
#include "internal.h"
#include "fpu_helper.h"

/* MIPS_PATCH */
#include "qemuafl/common.h"

# ifdef TARGET_ABI_MIPSO32
#  define MIPS_SYSCALL_NUMBER_UNUSED -1
static const int8_t mips_syscall_args[] = {
#include "syscall-args-o32.c.inc"
};
# endif /* O32 */

/* Break codes */
enum {
    BRK_OVERFLOW = 6,
    BRK_DIVZERO = 7
};

static int do_break(CPUMIPSState *env, target_siginfo_t *info,
                    unsigned int code)
{
    int ret = -1;

    switch (code) {
    case BRK_OVERFLOW:
    case BRK_DIVZERO:
        info->si_signo = TARGET_SIGFPE;
        info->si_errno = 0;
        info->si_code = (code == BRK_OVERFLOW) ? FPE_INTOVF : FPE_INTDIV;
        queue_signal(env, info->si_signo, QEMU_SI_FAULT, &*info);
        ret = 0;
        break;
    default:
        info->si_signo = TARGET_SIGTRAP;
        info->si_errno = 0;
        queue_signal(env, info->si_signo, QEMU_SI_FAULT, &*info);
        ret = 0;
        break;
    }

    return ret;
}

void cpu_loop(CPUMIPSState *env)
{
    CPUState *cs = env_cpu(env);
    target_siginfo_t info;
    int trapnr;
    abi_long ret;
# ifdef TARGET_ABI_MIPSO32
    unsigned int syscall_num;
# endif

    for(;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch(trapnr) {
        case EXCP_SYSCALL:
            env->active_tc.PC += 4;
# ifdef TARGET_ABI_MIPSO32
            syscall_num = env->active_tc.gpr[2] - 4000;
            if (syscall_num >= sizeof(mips_syscall_args)) {
                /* syscall_num is larger that any defined for MIPS O32 */
                ret = -TARGET_ENOSYS;
            } else if (mips_syscall_args[syscall_num] ==
                       MIPS_SYSCALL_NUMBER_UNUSED) {
                /* syscall_num belongs to the range not defined for MIPS O32 */
                ret = -TARGET_ENOSYS;
            } else {
                /* syscall_num is valid */
                int nb_args;
                abi_ulong sp_reg;
                abi_ulong arg5 = 0, arg6 = 0, arg7 = 0, arg8 = 0;

                nb_args = mips_syscall_args[syscall_num];
                sp_reg = env->active_tc.gpr[29];
                switch (nb_args) {
                /* these arguments are taken from the stack */
                case 8:
                    if ((ret = get_user_ual(arg8, sp_reg + 28)) != 0) {
                        goto done_syscall;
                    }
                    /* fall through */
                case 7:
                    if ((ret = get_user_ual(arg7, sp_reg + 24)) != 0) {
                        goto done_syscall;
                    }
                    /* fall through */
                case 6:
                    if ((ret = get_user_ual(arg6, sp_reg + 20)) != 0) {
                        goto done_syscall;
                    }
                    /* fall through */
                case 5:
                    if ((ret = get_user_ual(arg5, sp_reg + 16)) != 0) {
                        goto done_syscall;
                    }
                    /* fall through */
                default:
                    break;
                }
                ret = do_syscall(env, env->active_tc.gpr[2],
                                 env->active_tc.gpr[4],
                                 env->active_tc.gpr[5],
                                 env->active_tc.gpr[6],
                                 env->active_tc.gpr[7],
                                 arg5, arg6, arg7, arg8);
            }
done_syscall:
# else
            ret = do_syscall(env, env->active_tc.gpr[2],
                             env->active_tc.gpr[4], env->active_tc.gpr[5],
                             env->active_tc.gpr[6], env->active_tc.gpr[7],
                             env->active_tc.gpr[8], env->active_tc.gpr[9],
                             env->active_tc.gpr[10], env->active_tc.gpr[11]);
# endif /* O32 */
            if (ret == -TARGET_ERESTARTSYS) {
                env->active_tc.PC -= 4;
                break;
            }
            if (ret == -TARGET_QEMU_ESIGRETURN) {
                /* Returning from a successful sigreturn syscall.
                   Avoid clobbering register state.  */
                break;
            }
            if ((abi_ulong)ret >= (abi_ulong)-1133) {
                env->active_tc.gpr[7] = 1; /* error flag */
                ret = -ret;
            } else {
                env->active_tc.gpr[7] = 0; /* error flag */
            }
            env->active_tc.gpr[2] = ret;
            break;
        case EXCP_TLBL:
        case EXCP_TLBS:
        case EXCP_AdEL:
        case EXCP_AdES:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            /* XXX: check env->error_code */
            info.si_code = TARGET_SEGV_MAPERR;
            info._sifields._sigfault._addr = env->CP0_BadVAddr;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_CpU:
        case EXCP_RI:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = 0;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            info.si_signo = TARGET_SIGTRAP;
            info.si_errno = 0;
            info.si_code = TARGET_TRAP_BRKPT;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_DSPDIS:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPC;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_FPE:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = TARGET_FPE_FLTUNK;
            if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_INVALID) {
                info.si_code = TARGET_FPE_FLTINV;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_DIV0) {
                info.si_code = TARGET_FPE_FLTDIV;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_OVERFLOW) {
                info.si_code = TARGET_FPE_FLTOVF;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_UNDERFLOW) {
                info.si_code = TARGET_FPE_FLTUND;
            } else if (GET_FP_CAUSE(env->active_fpu.fcr31) & FP_INEXACT) {
                info.si_code = TARGET_FPE_FLTRES;
            }
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        /* The code below was inspired by the MIPS Linux kernel trap
         * handling code in arch/mips/kernel/traps.c.
         */
        case EXCP_BREAK:
            {
                abi_ulong trap_instr;
                unsigned int code;

                if (env->hflags & MIPS_HFLAG_M16) {
                    if (env->insn_flags & ASE_MICROMIPS) {
                        /* microMIPS mode */
                        ret = get_user_u16(trap_instr, env->active_tc.PC);
                        if (ret != 0) {
                            goto error;
                        }

                        if ((trap_instr >> 10) == 0x11) {
                            /* 16-bit instruction */
                            code = trap_instr & 0xf;
                        } else {
                            /* 32-bit instruction */
                            abi_ulong instr_lo;

                            ret = get_user_u16(instr_lo,
                                               env->active_tc.PC + 2);
                            if (ret != 0) {
                                goto error;
                            }
                            trap_instr = (trap_instr << 16) | instr_lo;
                            code = ((trap_instr >> 6) & ((1 << 20) - 1));
                            /* Unfortunately, microMIPS also suffers from
                               the old assembler bug...  */
                            if (code >= (1 << 10)) {
                                code >>= 10;
                            }
                        }
                    } else {
                        /* MIPS16e mode */
                        ret = get_user_u16(trap_instr, env->active_tc.PC);
                        if (ret != 0) {
                            goto error;
                        }
                        code = (trap_instr >> 6) & 0x3f;
                    }
                } else {
                    ret = get_user_u32(trap_instr, env->active_tc.PC);
                    if (ret != 0) {
                        goto error;
                    }

                    /* As described in the original Linux kernel code, the
                     * below checks on 'code' are to work around an old
                     * assembly bug.
                     */
                    code = ((trap_instr >> 6) & ((1 << 20) - 1));
                    if (code >= (1 << 10)) {
                        code >>= 10;
                    }
                }

                if (do_break(env, &info, code) != 0) {
                    goto error;
                }
            }
            break;
        case EXCP_TRAP:
            {
                abi_ulong trap_instr;
                unsigned int code = 0;

                if (env->hflags & MIPS_HFLAG_M16) {
                    /* microMIPS mode */
                    abi_ulong instr[2];

                    ret = get_user_u16(instr[0], env->active_tc.PC) ||
                          get_user_u16(instr[1], env->active_tc.PC + 2);

                    trap_instr = (instr[0] << 16) | instr[1];
                } else {
                    ret = get_user_u32(trap_instr, env->active_tc.PC);
                }

                if (ret != 0) {
                    goto error;
                }

                /* The immediate versions don't provide a code.  */
                if (!(trap_instr & 0xFC000000)) {
                    if (env->hflags & MIPS_HFLAG_M16) {
                        /* microMIPS mode */
                        code = ((trap_instr >> 12) & ((1 << 4) - 1));
                    } else {
                        code = ((trap_instr >> 6) & ((1 << 10) - 1));
                    }
                }

                if (do_break(env, &info, code) != 0) {
                    goto error;
                }
            }
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

void target_cpu_copy_regs(CPUArchState *env, struct target_pt_regs *regs)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    struct image_info *info = ts->info;
    int i;

    struct mode_req {
        bool single;
        bool soft;
        bool fr1;
        bool frdefault;
        bool fre;
    };

    static const struct mode_req fpu_reqs[] = {
        [MIPS_ABI_FP_ANY]    = { true,  true,  true,  true,  true  },
        [MIPS_ABI_FP_DOUBLE] = { false, false, false, true,  true  },
        [MIPS_ABI_FP_SINGLE] = { true,  false, false, false, false },
        [MIPS_ABI_FP_SOFT]   = { false, true,  false, false, false },
        [MIPS_ABI_FP_OLD_64] = { false, false, false, false, false },
        [MIPS_ABI_FP_XX]     = { false, false, true,  true,  true  },
        [MIPS_ABI_FP_64]     = { false, false, true,  false, false },
        [MIPS_ABI_FP_64A]    = { false, false, true,  false, true  }
    };

    /*
     * Mode requirements when .MIPS.abiflags is not present in the ELF.
     * Not present means that everything is acceptable except FR1.
     */
    static struct mode_req none_req = { true, true, false, true, true };

    struct mode_req prog_req;
    struct mode_req interp_req;

    for(i = 0; i < 32; i++) {
        env->active_tc.gpr[i] = regs->regs[i];
    }
    env->active_tc.PC = regs->cp0_epc & ~(target_ulong)1;
    if (regs->cp0_epc & 1) {
        env->hflags |= MIPS_HFLAG_M16;
    }

#ifdef TARGET_ABI_MIPSO32
# define MAX_FP_ABI MIPS_ABI_FP_64A
#else
# define MAX_FP_ABI MIPS_ABI_FP_SOFT
#endif
     if ((info->fp_abi > MAX_FP_ABI && info->fp_abi != MIPS_ABI_FP_UNKNOWN)
        || (info->interp_fp_abi > MAX_FP_ABI &&
            info->interp_fp_abi != MIPS_ABI_FP_UNKNOWN)) {
        fprintf(stderr, "qemu: Unexpected FPU mode\n");
        exit(1);
    }

    prog_req = (info->fp_abi == MIPS_ABI_FP_UNKNOWN) ? none_req
                                            : fpu_reqs[info->fp_abi];
    interp_req = (info->interp_fp_abi == MIPS_ABI_FP_UNKNOWN) ? none_req
                                            : fpu_reqs[info->interp_fp_abi];

    prog_req.single &= interp_req.single;
    prog_req.soft &= interp_req.soft;
    prog_req.fr1 &= interp_req.fr1;
    prog_req.frdefault &= interp_req.frdefault;
    prog_req.fre &= interp_req.fre;

    bool cpu_has_mips_r2_r6 = env->insn_flags & ISA_MIPS_R2 ||
                              env->insn_flags & ISA_MIPS_R6;

    if (prog_req.fre && !prog_req.frdefault && !prog_req.fr1) {
        env->CP0_Config5 |= (1 << CP0C5_FRE);
        if (env->active_fpu.fcr0 & (1 << FCR0_FREP)) {
            env->hflags |= MIPS_HFLAG_FRE;
        }
    } else if ((prog_req.fr1 && prog_req.frdefault) ||
         (prog_req.single && !prog_req.frdefault)) {
        if ((env->active_fpu.fcr0 & (1 << FCR0_F64)
            && cpu_has_mips_r2_r6) || prog_req.fr1) {
            env->CP0_Status |= (1 << CP0St_FR);
            env->hflags |= MIPS_HFLAG_F64;
        }
    } else  if (!prog_req.fre && !prog_req.frdefault &&
          !prog_req.fr1 && !prog_req.single && !prog_req.soft) {
        fprintf(stderr, "qemu: Can't find a matching FPU mode\n");
        exit(1);
    }

    if (env->insn_flags & ISA_NANOMIPS32) {
        return;
    }
    if (((info->elf_flags & EF_MIPS_NAN2008) != 0) !=
        ((env->active_fpu.fcr31 & (1 << FCR31_NAN2008)) != 0)) {
        if ((env->active_fpu.fcr31_rw_bitmask &
              (1 << FCR31_NAN2008)) == 0) {
            fprintf(stderr, "ELF binary's NaN mode not supported by CPU\n");
            exit(1);
        }
        if ((info->elf_flags & EF_MIPS_NAN2008) != 0) {
            env->active_fpu.fcr31 |= (1 << FCR31_NAN2008);
        } else {
            env->active_fpu.fcr31 &= ~(1 << FCR31_NAN2008);
        }
        restore_snan_bit_mode(env);
    }
}
