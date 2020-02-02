/*
 * RISC-V Control and Status Registers.
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

/* CSR function table */
static riscv_csr_operations csr_ops[];

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

/* CSR function table public API */
void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops)
{
    *ops = csr_ops[csrno & (CSR_TABLE_SIZE - 1)];
}

void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops)
{
    csr_ops[csrno & (CSR_TABLE_SIZE - 1)] = *ops;
}

/* Predicates */
static int fs(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -1;
    }
#endif
    return 0;
}

static int ctr(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);
    uint32_t ctr_en = ~0u;

    if (!cpu->cfg.ext_counters) {
        /* The Counters extensions is not enabled */
        return -1;
    }

    /*
     * The counters are always enabled at run time on newer priv specs, as the
     * CSR has changed from controlling that the counters can be read to
     * controlling that the counters increment.
     */
    if (env->priv_ver > PRIV_VERSION_1_09_1) {
        return 0;
    }

    if (env->priv < PRV_M) {
        ctr_en &= env->mcounteren;
    }
    if (env->priv < PRV_S) {
        ctr_en &= env->scounteren;
    }
    if (!(ctr_en & (1u << (csrno & 31)))) {
        return -1;
    }
#endif
    return 0;
}

#if !defined(CONFIG_USER_ONLY)
static int any(CPURISCVState *env, int csrno)
{
    return 0;
}

static int smode(CPURISCVState *env, int csrno)
{
    return -!riscv_has_ext(env, RVS);
}

static int hmode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS) &&
        riscv_has_ext(env, RVH)) {
        /* Hypervisor extension is supported */
        if ((env->priv == PRV_S && !riscv_cpu_virt_enabled(env)) ||
            env->priv == PRV_M) {
            return 0;
        }
    }

    return -1;
}

static int pmp(CPURISCVState *env, int csrno)
{
    return -!riscv_feature(env, RISCV_FEATURE_PMP);
}
#endif

/* User Floating-Point CSRs */
static int read_fflags(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -1;
    }
#endif
    *val = riscv_cpu_get_fflags(env);
    return 0;
}

static int write_fflags(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -1;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    riscv_cpu_set_fflags(env, val & (FSR_AEXC >> FSR_AEXC_SHIFT));
    return 0;
}

static int read_frm(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -1;
    }
#endif
    *val = env->frm;
    return 0;
}

static int write_frm(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -1;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = val & (FSR_RD >> FSR_RD_SHIFT);
    return 0;
}

static int read_fcsr(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -1;
    }
#endif
    *val = (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT)
        | (env->frm << FSR_RD_SHIFT);
    return 0;
}

static int write_fcsr(CPURISCVState *env, int csrno, target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return -1;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = (val & FSR_RD) >> FSR_RD_SHIFT;
    riscv_cpu_set_fflags(env, (val & FSR_AEXC) >> FSR_AEXC_SHIFT);
    return 0;
}

/* User Timers and Counters */
static int read_instret(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (use_icount) {
        *val = cpu_get_icount();
    } else {
        *val = cpu_get_host_ticks();
    }
#else
    *val = cpu_get_host_ticks();
#endif
    return 0;
}

#if defined(TARGET_RISCV32)
static int read_instreth(CPURISCVState *env, int csrno, target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (use_icount) {
        *val = cpu_get_icount() >> 32;
    } else {
        *val = cpu_get_host_ticks() >> 32;
    }
#else
    *val = cpu_get_host_ticks() >> 32;
#endif
    return 0;
}
#endif /* TARGET_RISCV32 */

#if defined(CONFIG_USER_ONLY)
static int read_time(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = cpu_get_host_ticks();
    return 0;
}

#if defined(TARGET_RISCV32)
static int read_timeh(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = cpu_get_host_ticks() >> 32;
    return 0;
}
#endif

#else /* CONFIG_USER_ONLY */

static int read_time(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return -1;
    }

    *val = env->rdtime_fn() + delta;
    return 0;
}

#if defined(TARGET_RISCV32)
static int read_timeh(CPURISCVState *env, int csrno, target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return -1;
    }

    *val = (env->rdtime_fn() + delta) >> 32;
    return 0;
}
#endif

/* Machine constants */

#define M_MODE_INTERRUPTS  (MIP_MSIP | MIP_MTIP | MIP_MEIP)
#define S_MODE_INTERRUPTS  (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define VS_MODE_INTERRUPTS (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP)

static const target_ulong delegable_ints = S_MODE_INTERRUPTS |
                                           VS_MODE_INTERRUPTS;
static const target_ulong all_ints = M_MODE_INTERRUPTS | S_MODE_INTERRUPTS |
                                     VS_MODE_INTERRUPTS;
static const target_ulong delegable_excps =
    (1ULL << (RISCV_EXCP_INST_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_INST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_ILLEGAL_INST)) |
    (1ULL << (RISCV_EXCP_BREAKPOINT)) |
    (1ULL << (RISCV_EXCP_LOAD_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_LOAD_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_U_ECALL)) |
    (1ULL << (RISCV_EXCP_S_ECALL)) |
    (1ULL << (RISCV_EXCP_VS_ECALL)) |
    (1ULL << (RISCV_EXCP_M_ECALL)) |
    (1ULL << (RISCV_EXCP_INST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_INST_GUEST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT));
static const target_ulong sstatus_v1_9_mask = SSTATUS_SIE | SSTATUS_SPIE |
    SSTATUS_UIE | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS |
    SSTATUS_SUM | SSTATUS_SD;
static const target_ulong sstatus_v1_10_mask = SSTATUS_SIE | SSTATUS_SPIE |
    SSTATUS_UIE | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS |
    SSTATUS_SUM | SSTATUS_MXR | SSTATUS_SD;
static const target_ulong sip_writable_mask = SIP_SSIP | MIP_USIP | MIP_UEIP;
static const target_ulong hip_writable_mask = MIP_VSSIP | MIP_VSTIP | MIP_VSEIP;
static const target_ulong vsip_writable_mask = MIP_VSSIP;

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
#endif /* CONFIG_USER_ONLY */

/* Machine Information Registers */
static int read_zero(CPURISCVState *env, int csrno, target_ulong *val)
{
    return *val = 0;
}

static int read_mhartid(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mhartid;
    return 0;
}

/* Machine Trap Setup */
static int read_mstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mstatus;
    return 0;
}

static int validate_vm(CPURISCVState *env, target_ulong vm)
{
    return (env->priv_ver >= PRIV_VERSION_1_10_0) ?
        valid_vm_1_10[vm & 0xf] : valid_vm_1_09[vm & 0xf];
}

static int write_mstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong mstatus = env->mstatus;
    target_ulong mask = 0;
    int dirty;

    /* flush tlb on mstatus fields that affect VM */
    if (env->priv_ver <= PRIV_VERSION_1_09_1) {
        if ((val ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP |
                MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_VM)) {
            tlb_flush(env_cpu(env));
        }
        mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
            MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
            MSTATUS_MPP | MSTATUS_MXR |
            (validate_vm(env, get_field(val, MSTATUS_VM)) ?
                MSTATUS_VM : 0);
    }
    if (env->priv_ver >= PRIV_VERSION_1_10_0) {
        if ((val ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP | MSTATUS_MPV |
                MSTATUS_MPRV | MSTATUS_SUM)) {
            tlb_flush(env_cpu(env));
        }
        mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
            MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
            MSTATUS_MPP | MSTATUS_MXR | MSTATUS_TVM | MSTATUS_TSR |
            MSTATUS_TW;
#if defined(TARGET_RISCV64)
            /*
             * RV32: MPV and MTL are not in mstatus. The current plan is to
             * add them to mstatush. For now, we just don't support it.
             */
            mask |= MSTATUS_MTL | MSTATUS_MPV;
#endif
    }

    mstatus = (mstatus & ~mask) | (val & mask);

    dirty = ((mstatus & MSTATUS_FS) == MSTATUS_FS) |
            ((mstatus & MSTATUS_XS) == MSTATUS_XS);
    mstatus = set_field(mstatus, MSTATUS_SD, dirty);
    env->mstatus = mstatus;

    return 0;
}

#ifdef TARGET_RISCV32
static int read_mstatush(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mstatush;
    return 0;
}

static int write_mstatush(CPURISCVState *env, int csrno, target_ulong val)
{
    if ((val ^ env->mstatush) & (MSTATUS_MPV)) {
        tlb_flush(env_cpu(env));
    }

    val &= MSTATUS_MPV | MSTATUS_MTL;

    env->mstatush = val;

    return 0;
}
#endif

static int read_misa(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->misa;
    return 0;
}

static int write_misa(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MISA)) {
        /* drop write to misa */
        return 0;
    }

    /* 'I' or 'E' must be present */
    if (!(val & (RVI | RVE))) {
        /* It is not, drop write to misa */
        return 0;
    }

    /* 'E' excludes all other extensions */
    if (val & RVE) {
        /* when we support 'E' we can do "val = RVE;" however
         * for now we just drop writes if 'E' is present.
         */
        return 0;
    }

    /* Mask extensions that are not supported by this hart */
    val &= env->misa_mask;

    /* Mask extensions that are not supported by QEMU */
    val &= (RVI | RVE | RVM | RVA | RVF | RVD | RVC | RVS | RVU);

    /* 'D' depends on 'F', so clear 'D' if 'F' is not present */
    if ((val & RVD) && !(val & RVF)) {
        val &= ~RVD;
    }

    /* Suppress 'C' if next instruction is not aligned
     * TODO: this should check next_pc
     */
    if ((val & RVC) && (GETPC() & ~3) != 0) {
        val &= ~RVC;
    }

    /* misa.MXL writes are not supported by QEMU */
    val = (env->misa & MISA_MXL) | (val & ~MISA_MXL);

    /* flush translation cache */
    if (val != env->misa) {
        tb_flush(env_cpu(env));
    }

    env->misa = val;

    return 0;
}

static int read_medeleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->medeleg;
    return 0;
}

static int write_medeleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->medeleg = (env->medeleg & ~delegable_excps) | (val & delegable_excps);
    return 0;
}

static int read_mideleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mideleg;
    return 0;
}

static int write_mideleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mideleg = (env->mideleg & ~delegable_ints) | (val & delegable_ints);
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= VS_MODE_INTERRUPTS;
    }
    return 0;
}

static int read_mie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie;
    return 0;
}

static int write_mie(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mie = (env->mie & ~all_ints) | (val & all_ints);
    return 0;
}

static int read_mtvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtvec;
    return 0;
}

static int write_mtvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->mtvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_MTVEC: reserved mode not supported\n");
    }
    return 0;
}

static int read_mcounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (env->priv_ver < PRIV_VERSION_1_10_0) {
        return -1;
    }
    *val = env->mcounteren;
    return 0;
}

static int write_mcounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    if (env->priv_ver < PRIV_VERSION_1_10_0) {
        return -1;
    }
    env->mcounteren = val;
    return 0;
}

/* This regiser is replaced with CSR_MCOUNTINHIBIT in 1.11.0 */
static int read_mscounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (env->priv_ver > PRIV_VERSION_1_09_1
        && env->priv_ver < PRIV_VERSION_1_11_0) {
        return -1;
    }
    *val = env->mcounteren;
    return 0;
}

/* This regiser is replaced with CSR_MCOUNTINHIBIT in 1.11.0 */
static int write_mscounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    if (env->priv_ver > PRIV_VERSION_1_09_1
        && env->priv_ver < PRIV_VERSION_1_11_0) {
        return -1;
    }
    env->mcounteren = val;
    return 0;
}

static int read_mucounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (env->priv_ver > PRIV_VERSION_1_09_1) {
        return -1;
    }
    *val = env->scounteren;
    return 0;
}

static int write_mucounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    if (env->priv_ver > PRIV_VERSION_1_09_1) {
        return -1;
    }
    env->scounteren = val;
    return 0;
}

/* Machine Trap Handling */
static int read_mscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mscratch;
    return 0;
}

static int write_mscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mscratch = val;
    return 0;
}

static int read_mepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mepc;
    return 0;
}

static int write_mepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mepc = val;
    return 0;
}

static int read_mcause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mcause;
    return 0;
}

static int write_mcause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mcause = val;
    return 0;
}

static int read_mbadaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mbadaddr;
    return 0;
}

static int write_mbadaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mbadaddr = val;
    return 0;
}

static int rmw_mip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    target_ulong mask = write_mask & delegable_ints & ~env->miclaim;
    uint32_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip;
    }

    return 0;
}

/* Supervisor Trap Setup */
static int read_sstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    target_ulong mask = ((env->priv_ver >= PRIV_VERSION_1_10_0) ?
                         sstatus_v1_10_mask : sstatus_v1_9_mask);
    *val = env->mstatus & mask;
    return 0;
}

static int write_sstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong mask = ((env->priv_ver >= PRIV_VERSION_1_10_0) ?
                         sstatus_v1_10_mask : sstatus_v1_9_mask);
    target_ulong newval = (env->mstatus & ~mask) | (val & mask);
    return write_mstatus(env, CSR_MSTATUS, newval);
}

static int read_sie(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (riscv_cpu_virt_enabled(env)) {
        /* Tell the guest the VS bits, shifted to the S bit locations */
        *val = (env->mie & env->mideleg & VS_MODE_INTERRUPTS) >> 1;
    } else {
        *val = env->mie & env->mideleg;
    }
    return 0;
}

static int write_sie(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong newval;

    if (riscv_cpu_virt_enabled(env)) {
        /* Shift the guests S bits to VS */
        newval = (env->mie & ~VS_MODE_INTERRUPTS) |
                 ((val << 1) & VS_MODE_INTERRUPTS);
    } else {
        newval = (env->mie & ~S_MODE_INTERRUPTS) | (val & S_MODE_INTERRUPTS);
    }

    return write_mie(env, CSR_MIE, newval);
}

static int read_stvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->stvec;
    return 0;
}

static int write_stvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->stvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_STVEC: reserved mode not supported\n");
    }
    return 0;
}

static int read_scounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (env->priv_ver < PRIV_VERSION_1_10_0) {
        return -1;
    }
    *val = env->scounteren;
    return 0;
}

static int write_scounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    if (env->priv_ver < PRIV_VERSION_1_10_0) {
        return -1;
    }
    env->scounteren = val;
    return 0;
}

/* Supervisor Trap Handling */
static int read_sscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sscratch;
    return 0;
}

static int write_sscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sscratch = val;
    return 0;
}

static int read_sepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sepc;
    return 0;
}

static int write_sepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sepc = val;
    return 0;
}

static int read_scause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->scause;
    return 0;
}

static int write_scause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->scause = val;
    return 0;
}

static int read_sbadaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sbadaddr;
    return 0;
}

static int write_sbadaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sbadaddr = val;
    return 0;
}

static int rmw_sip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    int ret;

    if (riscv_cpu_virt_enabled(env)) {
        /* Shift the new values to line up with the VS bits */
        ret = rmw_mip(env, CSR_MSTATUS, ret_value, new_value << 1,
                      (write_mask & sip_writable_mask) << 1 & env->mideleg);
        ret &= vsip_writable_mask;
        ret >>= 1;
    } else {
        ret = rmw_mip(env, CSR_MSTATUS, ret_value, new_value,
                      write_mask & env->mideleg & sip_writable_mask);
    }

    *ret_value &= env->mideleg;
    return ret;
}

/* Supervisor Protection and Translation */
static int read_satp(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        *val = 0;
    } else if (env->priv_ver >= PRIV_VERSION_1_10_0) {
        if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
            return -1;
        } else {
            *val = env->satp;
        }
    } else {
        *val = env->sptbr;
    }
    return 0;
}

static int write_satp(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        return 0;
    }
    if (env->priv_ver <= PRIV_VERSION_1_09_1 && (val ^ env->sptbr)) {
        tlb_flush(env_cpu(env));
        env->sptbr = val & (((target_ulong)
            1 << (TARGET_PHYS_ADDR_SPACE_BITS - PGSHIFT)) - 1);
    }
    if (env->priv_ver >= PRIV_VERSION_1_10_0 &&
        validate_vm(env, get_field(val, SATP_MODE)) &&
        ((val ^ env->satp) & (SATP_MODE | SATP_ASID | SATP_PPN)))
    {
        if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
            return -1;
        } else {
            if((val ^ env->satp) & SATP_ASID) {
                tlb_flush(env_cpu(env));
            }
            env->satp = val;
        }
    }
    return 0;
}

/* Hypervisor Extensions */
static int read_hstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hstatus;
    return 0;
}

static int write_hstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hstatus = val;
    return 0;
}

static int read_hedeleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hedeleg;
    return 0;
}

static int write_hedeleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hedeleg = val;
    return 0;
}

static int read_hideleg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hideleg;
    return 0;
}

static int write_hideleg(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hideleg = val;
    return 0;
}

static int rmw_hip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                   target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & hip_writable_mask);

    return ret;
}

static int read_hie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie & VS_MODE_INTERRUPTS;
    return 0;
}

static int write_hie(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong newval = (env->mie & ~VS_MODE_INTERRUPTS) | (val & VS_MODE_INTERRUPTS);
    return write_mie(env, CSR_MIE, newval);
}

static int read_hcounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hcounteren;
    return 0;
}

static int write_hcounteren(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hcounteren = val;
    return 0;
}

static int read_htval(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->htval;
    return 0;
}

static int write_htval(CPURISCVState *env, int csrno, target_ulong val)
{
    env->htval = val;
    return 0;
}

static int read_htinst(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->htinst;
    return 0;
}

static int write_htinst(CPURISCVState *env, int csrno, target_ulong val)
{
    env->htinst = val;
    return 0;
}

static int read_hgatp(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->hgatp;
    return 0;
}

static int write_hgatp(CPURISCVState *env, int csrno, target_ulong val)
{
    env->hgatp = val;
    return 0;
}

static int read_htimedelta(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!env->rdtime_fn) {
        return -1;
    }

#if defined(TARGET_RISCV32)
    *val = env->htimedelta & 0xffffffff;
#else
    *val = env->htimedelta;
#endif
    return 0;
}

static int write_htimedelta(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!env->rdtime_fn) {
        return -1;
    }

#if defined(TARGET_RISCV32)
    env->htimedelta = deposit64(env->htimedelta, 0, 32, (uint64_t)val);
#else
    env->htimedelta = val;
#endif
    return 0;
}

#if defined(TARGET_RISCV32)
static int read_htimedeltah(CPURISCVState *env, int csrno, target_ulong *val)
{
    if (!env->rdtime_fn) {
        return -1;
    }

    *val = env->htimedelta >> 32;
    return 0;
}

static int write_htimedeltah(CPURISCVState *env, int csrno, target_ulong val)
{
    if (!env->rdtime_fn) {
        return -1;
    }

    env->htimedelta = deposit64(env->htimedelta, 32, 32, (uint64_t)val);
    return 0;
}
#endif

/* Virtual CSR Registers */
static int read_vsstatus(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsstatus;
    return 0;
}

static int write_vsstatus(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsstatus = val;
    return 0;
}

static int rmw_vsip(CPURISCVState *env, int csrno, target_ulong *ret_value,
                    target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & env->mideleg & vsip_writable_mask);
    return ret;
}

static int read_vsie(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mie & env->mideleg & VS_MODE_INTERRUPTS;
    return 0;
}

static int write_vsie(CPURISCVState *env, int csrno, target_ulong val)
{
    target_ulong newval = (env->mie & ~env->mideleg) | (val & env->mideleg & MIP_VSSIP);
    return write_mie(env, CSR_MIE, newval);
}

static int read_vstvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstvec;
    return 0;
}

static int write_vstvec(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstvec = val;
    return 0;
}

static int read_vsscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsscratch;
    return 0;
}

static int write_vsscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsscratch = val;
    return 0;
}

static int read_vsepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsepc;
    return 0;
}

static int write_vsepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsepc = val;
    return 0;
}

static int read_vscause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vscause;
    return 0;
}

static int write_vscause(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vscause = val;
    return 0;
}

static int read_vstval(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstval;
    return 0;
}

static int write_vstval(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstval = val;
    return 0;
}

static int read_vsatp(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsatp;
    return 0;
}

static int write_vsatp(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsatp = val;
    return 0;
}

static int read_mtval2(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtval2;
    return 0;
}

static int write_mtval2(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mtval2 = val;
    return 0;
}

static int read_mtinst(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtinst;
    return 0;
}

static int write_mtinst(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mtinst = val;
    return 0;
}

/* Physical Memory Protection */
static int read_pmpcfg(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = pmpcfg_csr_read(env, csrno - CSR_PMPCFG0);
    return 0;
}

static int write_pmpcfg(CPURISCVState *env, int csrno, target_ulong val)
{
    pmpcfg_csr_write(env, csrno - CSR_PMPCFG0, val);
    return 0;
}

static int read_pmpaddr(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = pmpaddr_csr_read(env, csrno - CSR_PMPADDR0);
    return 0;
}

static int write_pmpaddr(CPURISCVState *env, int csrno, target_ulong val)
{
    pmpaddr_csr_write(env, csrno - CSR_PMPADDR0, val);
    return 0;
}

#endif

/*
 * riscv_csrrw - read and/or update control and status register
 *
 * csrr   <->  riscv_csrrw(env, csrno, ret_value, 0, 0);
 * csrrw  <->  riscv_csrrw(env, csrno, ret_value, value, -1);
 * csrrs  <->  riscv_csrrw(env, csrno, ret_value, -1, value);
 * csrrc  <->  riscv_csrrw(env, csrno, ret_value, 0, value);
 */

int riscv_csrrw(CPURISCVState *env, int csrno, target_ulong *ret_value,
                target_ulong new_value, target_ulong write_mask)
{
    int ret;
    target_ulong old_value;
    RISCVCPU *cpu = env_archcpu(env);

    /* check privileges and return -1 if check fails */
#if !defined(CONFIG_USER_ONLY)
    int effective_priv = env->priv;
    int read_only = get_field(csrno, 0xC00) == 3;

    if (riscv_has_ext(env, RVH) &&
        env->priv == PRV_S &&
        !riscv_cpu_virt_enabled(env)) {
        /*
         * We are in S mode without virtualisation, therefore we are in HS Mode.
         * Add 1 to the effective privledge level to allow us to access the
         * Hypervisor CSRs.
         */
        effective_priv++;
    }

    if ((write_mask && read_only) ||
        (!env->debugger && (effective_priv < get_field(csrno, 0x300)))) {
        return -1;
    }
#endif

    /* ensure the CSR extension is enabled. */
    if (!cpu->cfg.ext_icsr) {
        return -1;
    }

    /* check predicate */
    if (!csr_ops[csrno].predicate || csr_ops[csrno].predicate(env, csrno) < 0) {
        return -1;
    }

    /* execute combined read/write operation if it exists */
    if (csr_ops[csrno].op) {
        return csr_ops[csrno].op(env, csrno, ret_value, new_value, write_mask);
    }

    /* if no accessor exists then return failure */
    if (!csr_ops[csrno].read) {
        return -1;
    }

    /* read old value */
    ret = csr_ops[csrno].read(env, csrno, &old_value);
    if (ret < 0) {
        return ret;
    }

    /* write value if writable and write mask set, otherwise drop writes */
    if (write_mask) {
        new_value = (old_value & ~write_mask) | (new_value & write_mask);
        if (csr_ops[csrno].write) {
            ret = csr_ops[csrno].write(env, csrno, new_value);
            if (ret < 0) {
                return ret;
            }
        }
    }

    /* return old value */
    if (ret_value) {
        *ret_value = old_value;
    }

    return 0;
}

/*
 * Debugger support.  If not in user mode, set env->debugger before the
 * riscv_csrrw call and clear it after the call.
 */
int riscv_csrrw_debug(CPURISCVState *env, int csrno, target_ulong *ret_value,
                target_ulong new_value, target_ulong write_mask)
{
    int ret;
#if !defined(CONFIG_USER_ONLY)
    env->debugger = true;
#endif
    ret = riscv_csrrw(env, csrno, ret_value, new_value, write_mask);
#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif
    return ret;
}

/* Control and Status Register function table */
static riscv_csr_operations csr_ops[CSR_TABLE_SIZE] = {
    /* User Floating-Point CSRs */
    [CSR_FFLAGS] =              { fs,   read_fflags,      write_fflags      },
    [CSR_FRM] =                 { fs,   read_frm,         write_frm         },
    [CSR_FCSR] =                { fs,   read_fcsr,        write_fcsr        },

    /* User Timers and Counters */
    [CSR_CYCLE] =               { ctr,  read_instret                        },
    [CSR_INSTRET] =             { ctr,  read_instret                        },
#if defined(TARGET_RISCV32)
    [CSR_CYCLEH] =              { ctr,  read_instreth                       },
    [CSR_INSTRETH] =            { ctr,  read_instreth                       },
#endif

    /* In privileged mode, the monitor will have to emulate TIME CSRs only if
     * rdtime callback is not provided by machine/platform emulation */
    [CSR_TIME] =                { ctr,  read_time                           },
#if defined(TARGET_RISCV32)
    [CSR_TIMEH] =               { ctr,  read_timeh                          },
#endif

#if !defined(CONFIG_USER_ONLY)
    /* Machine Timers and Counters */
    [CSR_MCYCLE] =              { any,  read_instret                        },
    [CSR_MINSTRET] =            { any,  read_instret                        },
#if defined(TARGET_RISCV32)
    [CSR_MCYCLEH] =             { any,  read_instreth                       },
    [CSR_MINSTRETH] =           { any,  read_instreth                       },
#endif

    /* Machine Information Registers */
    [CSR_MVENDORID] =           { any,  read_zero                           },
    [CSR_MARCHID] =             { any,  read_zero                           },
    [CSR_MIMPID] =              { any,  read_zero                           },
    [CSR_MHARTID] =             { any,  read_mhartid                        },

    /* Machine Trap Setup */
    [CSR_MSTATUS] =             { any,  read_mstatus,     write_mstatus     },
    [CSR_MISA] =                { any,  read_misa,        write_misa        },
    [CSR_MIDELEG] =             { any,  read_mideleg,     write_mideleg     },
    [CSR_MEDELEG] =             { any,  read_medeleg,     write_medeleg     },
    [CSR_MIE] =                 { any,  read_mie,         write_mie         },
    [CSR_MTVEC] =               { any,  read_mtvec,       write_mtvec       },
    [CSR_MCOUNTEREN] =          { any,  read_mcounteren,  write_mcounteren  },

#if defined(TARGET_RISCV32)
    [CSR_MSTATUSH] =            { any,  read_mstatush,    write_mstatush    },
#endif

    /* Legacy Counter Setup (priv v1.9.1) */
    [CSR_MUCOUNTEREN] =         { any,  read_mucounteren, write_mucounteren },
    [CSR_MSCOUNTEREN] =         { any,  read_mscounteren, write_mscounteren },

    /* Machine Trap Handling */
    [CSR_MSCRATCH] =            { any,  read_mscratch,    write_mscratch    },
    [CSR_MEPC] =                { any,  read_mepc,        write_mepc        },
    [CSR_MCAUSE] =              { any,  read_mcause,      write_mcause      },
    [CSR_MBADADDR] =            { any,  read_mbadaddr,    write_mbadaddr    },
    [CSR_MIP] =                 { any,  NULL,     NULL,     rmw_mip         },

    /* Supervisor Trap Setup */
    [CSR_SSTATUS] =             { smode, read_sstatus,     write_sstatus     },
    [CSR_SIE] =                 { smode, read_sie,         write_sie         },
    [CSR_STVEC] =               { smode, read_stvec,       write_stvec       },
    [CSR_SCOUNTEREN] =          { smode, read_scounteren,  write_scounteren  },

    /* Supervisor Trap Handling */
    [CSR_SSCRATCH] =            { smode, read_sscratch,    write_sscratch    },
    [CSR_SEPC] =                { smode, read_sepc,        write_sepc        },
    [CSR_SCAUSE] =              { smode, read_scause,      write_scause      },
    [CSR_SBADADDR] =            { smode, read_sbadaddr,    write_sbadaddr    },
    [CSR_SIP] =                 { smode, NULL,     NULL,     rmw_sip         },

    /* Supervisor Protection and Translation */
    [CSR_SATP] =                { smode, read_satp,        write_satp        },

    [CSR_HSTATUS] =             { hmode,   read_hstatus,     write_hstatus    },
    [CSR_HEDELEG] =             { hmode,   read_hedeleg,     write_hedeleg    },
    [CSR_HIDELEG] =             { hmode,   read_hideleg,     write_hideleg    },
    [CSR_HIP] =                 { hmode,   NULL,     NULL,     rmw_hip        },
    [CSR_HIE] =                 { hmode,   read_hie,         write_hie        },
    [CSR_HCOUNTEREN] =          { hmode,   read_hcounteren,  write_hcounteren },
    [CSR_HTVAL] =               { hmode,   read_htval,       write_htval      },
    [CSR_HTINST] =              { hmode,   read_htinst,      write_htinst     },
    [CSR_HGATP] =               { hmode,   read_hgatp,       write_hgatp      },
    [CSR_HTIMEDELTA] =          { hmode,   read_htimedelta,  write_htimedelta },
#if defined(TARGET_RISCV32)
    [CSR_HTIMEDELTAH] =         { hmode,   read_htimedeltah, write_htimedeltah},
#endif

    [CSR_VSSTATUS] =            { hmode,   read_vsstatus,    write_vsstatus   },
    [CSR_VSIP] =                { hmode,   NULL,     NULL,     rmw_vsip       },
    [CSR_VSIE] =                { hmode,   read_vsie,        write_vsie       },
    [CSR_VSTVEC] =              { hmode,   read_vstvec,      write_vstvec     },
    [CSR_VSSCRATCH] =           { hmode,   read_vsscratch,   write_vsscratch  },
    [CSR_VSEPC] =               { hmode,   read_vsepc,       write_vsepc      },
    [CSR_VSCAUSE] =             { hmode,   read_vscause,     write_vscause    },
    [CSR_VSTVAL] =              { hmode,   read_vstval,      write_vstval     },
    [CSR_VSATP] =               { hmode,   read_vsatp,       write_vsatp      },

    [CSR_MTVAL2] =              { hmode,   read_mtval2,      write_mtval2     },
    [CSR_MTINST] =              { hmode,   read_mtinst,      write_mtinst     },

    /* Physical Memory Protection */
    [CSR_PMPCFG0  ... CSR_PMPADDR9] =  { pmp,   read_pmpcfg,  write_pmpcfg   },
    [CSR_PMPADDR0 ... CSR_PMPADDR15] = { pmp,   read_pmpaddr, write_pmpaddr  },

    /* Performance Counters */
    [CSR_HPMCOUNTER3   ... CSR_HPMCOUNTER31] =    { ctr,  read_zero          },
    [CSR_MHPMCOUNTER3  ... CSR_MHPMCOUNTER31] =   { any,  read_zero          },
    [CSR_MHPMEVENT3    ... CSR_MHPMEVENT31] =     { any,  read_zero          },
#if defined(TARGET_RISCV32)
    [CSR_HPMCOUNTER3H  ... CSR_HPMCOUNTER31H] =   { ctr,  read_zero          },
    [CSR_MHPMCOUNTER3H ... CSR_MHPMCOUNTER31H] =  { any,  read_zero          },
#endif
#endif /* !CONFIG_USER_ONLY */
};
