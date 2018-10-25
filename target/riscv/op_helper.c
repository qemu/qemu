/*
 * RISC-V Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

#ifndef CONFIG_USER_ONLY

#if defined(TARGET_RISCV32)
static const char valid_vm_1_09[16] = {
    [VM_1_09_MBARE] = 1,
    [VM_1_09_SV32] = 1,
};
static const char valid_vm_1_10[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV32] = 1
};
#elif defined(TARGET_RISCV64)
static const char valid_vm_1_09[16] = {
    [VM_1_09_MBARE] = 1,
    [VM_1_09_SV39] = 1,
    [VM_1_09_SV48] = 1,
};
static const char valid_vm_1_10[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV39] = 1,
    [VM_1_10_SV48] = 1,
    [VM_1_10_SV57] = 1
};
#endif

static int validate_vm(CPURISCVState *env, target_ulong vm)
{
    return (env->priv_ver >= PRIV_VERSION_1_10_0) ?
        valid_vm_1_10[vm & 0xf] : valid_vm_1_09[vm & 0xf];
}

#endif

/* Exceptions processing helpers */
void QEMU_NORETURN do_raise_exception_err(CPURISCVState *env,
                                          uint32_t exception, uintptr_t pc)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));
    qemu_log_mask(CPU_LOG_INT, "%s: %d\n", __func__, exception);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPURISCVState *env, uint32_t exception)
{
    do_raise_exception_err(env, exception, 0);
}

static void validate_mstatus_fs(CPURISCVState *env, uintptr_t ra)
{
#ifndef CONFIG_USER_ONLY
    if (!(env->mstatus & MSTATUS_FS)) {
        do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
#endif
}

/*
 * Handle writes to CSRs and any resulting special behavior
 *
 * Adapted from Spike's processor_t::set_csr
 */
void csr_write_helper(CPURISCVState *env, target_ulong val_to_write,
        target_ulong csrno)
{
#ifndef CONFIG_USER_ONLY
    uint64_t delegable_ints = MIP_SSIP | MIP_STIP | MIP_SEIP;
    uint64_t all_ints = delegable_ints | MIP_MSIP | MIP_MTIP;
#endif

    switch (csrno) {
    case CSR_FFLAGS:
        validate_mstatus_fs(env, GETPC());
        cpu_riscv_set_fflags(env, val_to_write & (FSR_AEXC >> FSR_AEXC_SHIFT));
        break;
    case CSR_FRM:
        validate_mstatus_fs(env, GETPC());
        env->frm = val_to_write & (FSR_RD >> FSR_RD_SHIFT);
        break;
    case CSR_FCSR:
        validate_mstatus_fs(env, GETPC());
        env->frm = (val_to_write & FSR_RD) >> FSR_RD_SHIFT;
        cpu_riscv_set_fflags(env, (val_to_write & FSR_AEXC) >> FSR_AEXC_SHIFT);
        break;
#ifndef CONFIG_USER_ONLY
    case CSR_MSTATUS: {
        target_ulong mstatus = env->mstatus;
        target_ulong mask = 0;
        target_ulong mpp = get_field(val_to_write, MSTATUS_MPP);

        /* flush tlb on mstatus fields that affect VM */
        if (env->priv_ver <= PRIV_VERSION_1_09_1) {
            if ((val_to_write ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP |
                    MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_VM)) {
                helper_tlb_flush(env);
            }
            mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
                MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
                MSTATUS_MPP | MSTATUS_MXR |
                (validate_vm(env, get_field(val_to_write, MSTATUS_VM)) ?
                    MSTATUS_VM : 0);
        }
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            if ((val_to_write ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP |
                    MSTATUS_MPRV | MSTATUS_SUM)) {
                helper_tlb_flush(env);
            }
            mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
                MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
                MSTATUS_MPP | MSTATUS_MXR;
        }

        /* silenty discard mstatus.mpp writes for unsupported modes */
        if (mpp == PRV_H ||
            (!riscv_has_ext(env, RVS) && mpp == PRV_S) ||
            (!riscv_has_ext(env, RVU) && mpp == PRV_U)) {
            mask &= ~MSTATUS_MPP;
        }

        mstatus = (mstatus & ~mask) | (val_to_write & mask);

        /* Note: this is a workaround for an issue where mstatus.FS
           does not report dirty after floating point operations
           that modify floating point state. This workaround is
           technically compliant with the RISC-V Privileged
           specification as it is legal to return only off, or dirty.
           at the expense of extra floating point save/restore. */

        /* FP is always dirty or off */
        if (mstatus & MSTATUS_FS) {
            mstatus |= MSTATUS_FS;
        }

        int dirty = ((mstatus & MSTATUS_FS) == MSTATUS_FS) |
                    ((mstatus & MSTATUS_XS) == MSTATUS_XS);
        mstatus = set_field(mstatus, MSTATUS_SD, dirty);
        env->mstatus = mstatus;
        break;
    }
    case CSR_MIP: {
        /*
         * Since the writeable bits in MIP are not set asynchrously by the
         * CLINT, no additional locking is needed for read-modifiy-write
         * CSR operations
         */
        qemu_mutex_lock_iothread();
        RISCVCPU *cpu = riscv_env_get_cpu(env);
        riscv_cpu_update_mip(cpu, MIP_SSIP | MIP_STIP,
                                  (val_to_write & (MIP_SSIP | MIP_STIP)));
        /*
         * csrs, csrc on mip.SEIP is not decomposable into separate read and
         * write steps, so a different implementation is needed
         */
        qemu_mutex_unlock_iothread();
        break;
    }
    case CSR_MIE: {
        env->mie = (env->mie & ~all_ints) |
            (val_to_write & all_ints);
        break;
    }
    case CSR_MIDELEG:
        env->mideleg = (env->mideleg & ~delegable_ints)
                                | (val_to_write & delegable_ints);
        break;
    case CSR_MEDELEG: {
        target_ulong mask = 0;
        mask |= 1ULL << (RISCV_EXCP_INST_ADDR_MIS);
        mask |= 1ULL << (RISCV_EXCP_INST_ACCESS_FAULT);
        mask |= 1ULL << (RISCV_EXCP_ILLEGAL_INST);
        mask |= 1ULL << (RISCV_EXCP_BREAKPOINT);
        mask |= 1ULL << (RISCV_EXCP_LOAD_ADDR_MIS);
        mask |= 1ULL << (RISCV_EXCP_LOAD_ACCESS_FAULT);
        mask |= 1ULL << (RISCV_EXCP_STORE_AMO_ADDR_MIS);
        mask |= 1ULL << (RISCV_EXCP_STORE_AMO_ACCESS_FAULT);
        mask |= 1ULL << (RISCV_EXCP_U_ECALL);
        mask |= 1ULL << (RISCV_EXCP_S_ECALL);
        mask |= 1ULL << (RISCV_EXCP_H_ECALL);
        mask |= 1ULL << (RISCV_EXCP_M_ECALL);
        mask |= 1ULL << (RISCV_EXCP_INST_PAGE_FAULT);
        mask |= 1ULL << (RISCV_EXCP_LOAD_PAGE_FAULT);
        mask |= 1ULL << (RISCV_EXCP_STORE_PAGE_FAULT);
        env->medeleg = (env->medeleg & ~mask)
                                | (val_to_write & mask);
        break;
    }
    case CSR_MINSTRET:
        /* minstret is WARL so unsupported writes are ignored */
        break;
    case CSR_MCYCLE:
        /* mcycle is WARL so unsupported writes are ignored */
        break;
#if defined(TARGET_RISCV32)
    case CSR_MINSTRETH:
        /* minstreth is WARL so unsupported writes are ignored */
        break;
    case CSR_MCYCLEH:
        /* mcycleh is WARL so unsupported writes are ignored */
        break;
#endif
    case CSR_MUCOUNTEREN:
        if (env->priv_ver <= PRIV_VERSION_1_09_1) {
            env->scounteren = val_to_write;
            break;
        } else {
            goto do_illegal;
        }
    case CSR_MSCOUNTEREN:
        if (env->priv_ver <= PRIV_VERSION_1_09_1) {
            env->mcounteren = val_to_write;
            break;
        } else {
            goto do_illegal;
        }
    case CSR_SSTATUS: {
        target_ulong ms = env->mstatus;
        target_ulong mask = SSTATUS_SIE | SSTATUS_SPIE | SSTATUS_UIE
            | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS
            | SSTATUS_SUM | SSTATUS_SD;
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            mask |= SSTATUS_MXR;
        }
        ms = (ms & ~mask) | (val_to_write & mask);
        csr_write_helper(env, ms, CSR_MSTATUS);
        break;
    }
    case CSR_SIP: {
        qemu_mutex_lock_iothread();
        target_ulong next_mip = (env->mip & ~env->mideleg)
                                | (val_to_write & env->mideleg);
        qemu_mutex_unlock_iothread();
        csr_write_helper(env, next_mip, CSR_MIP);
        break;
    }
    case CSR_SIE: {
        target_ulong next_mie = (env->mie & ~env->mideleg)
                                | (val_to_write & env->mideleg);
        csr_write_helper(env, next_mie, CSR_MIE);
        break;
    }
    case CSR_SATP: /* CSR_SPTBR */ {
        if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
            break;
        }
        if (env->priv_ver <= PRIV_VERSION_1_09_1 && (val_to_write ^ env->sptbr))
        {
            helper_tlb_flush(env);
            env->sptbr = val_to_write & (((target_ulong)
                1 << (TARGET_PHYS_ADDR_SPACE_BITS - PGSHIFT)) - 1);
        }
        if (env->priv_ver >= PRIV_VERSION_1_10_0 &&
            validate_vm(env, get_field(val_to_write, SATP_MODE)) &&
            ((val_to_write ^ env->satp) & (SATP_MODE | SATP_ASID | SATP_PPN)))
        {
            helper_tlb_flush(env);
            env->satp = val_to_write;
        }
        break;
    }
    case CSR_SEPC:
        env->sepc = val_to_write;
        break;
    case CSR_STVEC:
        /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
        if ((val_to_write & 3) == 0) {
            env->stvec = val_to_write >> 2 << 2;
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "CSR_STVEC: vectored traps not supported\n");
        }
        break;
    case CSR_SCOUNTEREN:
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            env->scounteren = val_to_write;
            break;
        } else {
            goto do_illegal;
        }
    case CSR_SSCRATCH:
        env->sscratch = val_to_write;
        break;
    case CSR_SCAUSE:
        env->scause = val_to_write;
        break;
    case CSR_SBADADDR:
        env->sbadaddr = val_to_write;
        break;
    case CSR_MEPC:
        env->mepc = val_to_write;
        break;
    case CSR_MTVEC:
        /* bits [1:0] indicate mode; 0 = direct, 1 = vectored, 2 >= reserved */
        if ((val_to_write & 3) == 0) {
            env->mtvec = val_to_write >> 2 << 2;
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "CSR_MTVEC: vectored traps not supported\n");
        }
        break;
    case CSR_MCOUNTEREN:
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            env->mcounteren = val_to_write;
            break;
        } else {
            goto do_illegal;
        }
    case CSR_MSCRATCH:
        env->mscratch = val_to_write;
        break;
    case CSR_MCAUSE:
        env->mcause = val_to_write;
        break;
    case CSR_MBADADDR:
        env->mbadaddr = val_to_write;
        break;
    case CSR_MISA:
        /* misa is WARL so unsupported writes are ignored */
        break;
    case CSR_PMPCFG0:
    case CSR_PMPCFG1:
    case CSR_PMPCFG2:
    case CSR_PMPCFG3:
       pmpcfg_csr_write(env, csrno - CSR_PMPCFG0, val_to_write);
       break;
    case CSR_PMPADDR0:
    case CSR_PMPADDR1:
    case CSR_PMPADDR2:
    case CSR_PMPADDR3:
    case CSR_PMPADDR4:
    case CSR_PMPADDR5:
    case CSR_PMPADDR6:
    case CSR_PMPADDR7:
    case CSR_PMPADDR8:
    case CSR_PMPADDR9:
    case CSR_PMPADDR10:
    case CSR_PMPADDR11:
    case CSR_PMPADDR12:
    case CSR_PMPADDR13:
    case CSR_PMPADDR14:
    case CSR_PMPADDR15:
       pmpaddr_csr_write(env, csrno - CSR_PMPADDR0, val_to_write);
       break;
#endif
#if !defined(CONFIG_USER_ONLY)
    do_illegal:
#endif
    default:
        do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
}

/*
 * Handle reads to CSRs and any resulting special behavior
 *
 * Adapted from Spike's processor_t::get_csr
 */
target_ulong csr_read_helper(CPURISCVState *env, target_ulong csrno)
{
#ifndef CONFIG_USER_ONLY
    target_ulong ctr_en = env->priv == PRV_U ? env->scounteren :
                          env->priv == PRV_S ? env->mcounteren : -1U;
#else
    target_ulong ctr_en = -1;
#endif
    target_ulong ctr_ok = (ctr_en >> (csrno & 31)) & 1;

    if (csrno >= CSR_HPMCOUNTER3 && csrno <= CSR_HPMCOUNTER31) {
        if (ctr_ok) {
            return 0;
        }
    }
#if defined(TARGET_RISCV32)
    if (csrno >= CSR_HPMCOUNTER3H && csrno <= CSR_HPMCOUNTER31H) {
        if (ctr_ok) {
            return 0;
        }
    }
#endif
    if (csrno >= CSR_MHPMCOUNTER3 && csrno <= CSR_MHPMCOUNTER31) {
        return 0;
    }
#if defined(TARGET_RISCV32)
    if (csrno >= CSR_MHPMCOUNTER3 && csrno <= CSR_MHPMCOUNTER31) {
        return 0;
    }
#endif
    if (csrno >= CSR_MHPMEVENT3 && csrno <= CSR_MHPMEVENT31) {
        return 0;
    }

    switch (csrno) {
    case CSR_FFLAGS:
        validate_mstatus_fs(env, GETPC());
        return cpu_riscv_get_fflags(env);
    case CSR_FRM:
        validate_mstatus_fs(env, GETPC());
        return env->frm;
    case CSR_FCSR:
        validate_mstatus_fs(env, GETPC());
        return (cpu_riscv_get_fflags(env) << FSR_AEXC_SHIFT)
                | (env->frm << FSR_RD_SHIFT);
    /* rdtime/rdtimeh is trapped and emulated by bbl in system mode */
#ifdef CONFIG_USER_ONLY
    case CSR_TIME:
        return cpu_get_host_ticks();
#if defined(TARGET_RISCV32)
    case CSR_TIMEH:
        return cpu_get_host_ticks() >> 32;
#endif
#endif
    case CSR_INSTRET:
    case CSR_CYCLE:
        if (ctr_ok) {
#if !defined(CONFIG_USER_ONLY)
            if (use_icount) {
                return cpu_get_icount();
            } else {
                return cpu_get_host_ticks();
            }
#else
            return cpu_get_host_ticks();
#endif
        }
        break;
#if defined(TARGET_RISCV32)
    case CSR_INSTRETH:
    case CSR_CYCLEH:
        if (ctr_ok) {
#if !defined(CONFIG_USER_ONLY)
            if (use_icount) {
                return cpu_get_icount() >> 32;
            } else {
                return cpu_get_host_ticks() >> 32;
            }
#else
            return cpu_get_host_ticks() >> 32;
#endif
        }
        break;
#endif
#ifndef CONFIG_USER_ONLY
    case CSR_MINSTRET:
    case CSR_MCYCLE:
        if (use_icount) {
            return cpu_get_icount();
        } else {
            return cpu_get_host_ticks();
        }
    case CSR_MINSTRETH:
    case CSR_MCYCLEH:
#if defined(TARGET_RISCV32)
        if (use_icount) {
            return cpu_get_icount() >> 32;
        } else {
            return cpu_get_host_ticks() >> 32;
        }
#endif
        break;
    case CSR_MUCOUNTEREN:
        if (env->priv_ver <= PRIV_VERSION_1_09_1) {
            return env->scounteren;
        } else {
            break; /* illegal instruction */
        }
    case CSR_MSCOUNTEREN:
        if (env->priv_ver <= PRIV_VERSION_1_09_1) {
            return env->mcounteren;
        } else {
            break; /* illegal instruction */
        }
    case CSR_SSTATUS: {
        target_ulong mask = SSTATUS_SIE | SSTATUS_SPIE | SSTATUS_UIE
            | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS
            | SSTATUS_SUM | SSTATUS_SD;
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            mask |= SSTATUS_MXR;
        }
        return env->mstatus & mask;
    }
    case CSR_SIP: {
        qemu_mutex_lock_iothread();
        target_ulong tmp = env->mip & env->mideleg;
        qemu_mutex_unlock_iothread();
        return tmp;
    }
    case CSR_SIE:
        return env->mie & env->mideleg;
    case CSR_SEPC:
        return env->sepc;
    case CSR_SBADADDR:
        return env->sbadaddr;
    case CSR_STVEC:
        return env->stvec;
    case CSR_SCOUNTEREN:
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            return env->scounteren;
        } else {
            break; /* illegal instruction */
        }
    case CSR_SCAUSE:
        return env->scause;
    case CSR_SATP: /* CSR_SPTBR */
        if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
            return 0;
        }
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            return env->satp;
        } else {
            return env->sptbr;
        }
    case CSR_SSCRATCH:
        return env->sscratch;
    case CSR_MSTATUS:
        return env->mstatus;
    case CSR_MIP: {
        qemu_mutex_lock_iothread();
        target_ulong tmp = env->mip;
        qemu_mutex_unlock_iothread();
        return tmp;
    }
    case CSR_MIE:
        return env->mie;
    case CSR_MEPC:
        return env->mepc;
    case CSR_MSCRATCH:
        return env->mscratch;
    case CSR_MCAUSE:
        return env->mcause;
    case CSR_MBADADDR:
        return env->mbadaddr;
    case CSR_MISA:
        return env->misa;
    case CSR_MARCHID:
        return 0; /* as spike does */
    case CSR_MIMPID:
        return 0; /* as spike does */
    case CSR_MVENDORID:
        return 0; /* as spike does */
    case CSR_MHARTID:
        return env->mhartid;
    case CSR_MTVEC:
        return env->mtvec;
    case CSR_MCOUNTEREN:
        if (env->priv_ver >= PRIV_VERSION_1_10_0) {
            return env->mcounteren;
        } else {
            break; /* illegal instruction */
        }
    case CSR_MEDELEG:
        return env->medeleg;
    case CSR_MIDELEG:
        return env->mideleg;
    case CSR_PMPCFG0:
    case CSR_PMPCFG1:
    case CSR_PMPCFG2:
    case CSR_PMPCFG3:
       return pmpcfg_csr_read(env, csrno - CSR_PMPCFG0);
    case CSR_PMPADDR0:
    case CSR_PMPADDR1:
    case CSR_PMPADDR2:
    case CSR_PMPADDR3:
    case CSR_PMPADDR4:
    case CSR_PMPADDR5:
    case CSR_PMPADDR6:
    case CSR_PMPADDR7:
    case CSR_PMPADDR8:
    case CSR_PMPADDR9:
    case CSR_PMPADDR10:
    case CSR_PMPADDR11:
    case CSR_PMPADDR12:
    case CSR_PMPADDR13:
    case CSR_PMPADDR14:
    case CSR_PMPADDR15:
       return pmpaddr_csr_read(env, csrno - CSR_PMPADDR0);
#endif
    }
    /* used by e.g. MTIME read */
    do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
}

/*
 * Check that CSR access is allowed.
 *
 * Adapted from Spike's decode.h:validate_csr
 */
static void validate_csr(CPURISCVState *env, uint64_t which,
                         uint64_t write, uintptr_t ra)
{
#ifndef CONFIG_USER_ONLY
    unsigned csr_priv = get_field((which), 0x300);
    unsigned csr_read_only = get_field((which), 0xC00) == 3;
    if (((write) && csr_read_only) || (env->priv < csr_priv)) {
        do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
#endif
}

target_ulong helper_csrrw(CPURISCVState *env, target_ulong src,
        target_ulong csr)
{
    validate_csr(env, csr, 1, GETPC());
    uint64_t csr_backup = csr_read_helper(env, csr);
    csr_write_helper(env, src, csr);
    return csr_backup;
}

target_ulong helper_csrrs(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong rs1_pass)
{
    validate_csr(env, csr, rs1_pass != 0, GETPC());
    uint64_t csr_backup = csr_read_helper(env, csr);
    if (rs1_pass != 0) {
        csr_write_helper(env, src | csr_backup, csr);
    }
    return csr_backup;
}

target_ulong helper_csrrc(CPURISCVState *env, target_ulong src,
        target_ulong csr, target_ulong rs1_pass)
{
    validate_csr(env, csr, rs1_pass != 0, GETPC());
    uint64_t csr_backup = csr_read_helper(env, csr);
    if (rs1_pass != 0) {
        csr_write_helper(env, (~src) & csr_backup, csr);
    }
    return csr_backup;
}

#ifndef CONFIG_USER_ONLY

target_ulong helper_sret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_S)) {
        do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->sepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        do_raise_exception_err(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    target_ulong mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_SPP);
    mstatus = set_field(mstatus,
        env->priv_ver >= PRIV_VERSION_1_10_0 ?
        MSTATUS_SIE : MSTATUS_UIE << prev_priv,
        get_field(mstatus, MSTATUS_SPIE));
    mstatus = set_field(mstatus, MSTATUS_SPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
    riscv_set_mode(env, prev_priv);
    csr_write_helper(env, mstatus, CSR_MSTATUS);

    return retpc;
}

target_ulong helper_mret(CPURISCVState *env, target_ulong cpu_pc_deb)
{
    if (!(env->priv >= PRV_M)) {
        do_raise_exception_err(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->mepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        do_raise_exception_err(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    target_ulong mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);
    mstatus = set_field(mstatus,
        env->priv_ver >= PRIV_VERSION_1_10_0 ?
        MSTATUS_MIE : MSTATUS_UIE << prev_priv,
        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 0);
    mstatus = set_field(mstatus, MSTATUS_MPP, PRV_U);
    riscv_set_mode(env, prev_priv);
    csr_write_helper(env, mstatus, CSR_MSTATUS);

    return retpc;
}

void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));

    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

void helper_tlb_flush(CPURISCVState *env)
{
    RISCVCPU *cpu = riscv_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    tlb_flush(cs);
}

#endif /* !CONFIG_USER_ONLY */
