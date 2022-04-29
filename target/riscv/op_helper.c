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
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

/* Exceptions processing helpers */
G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      uint32_t exception, uintptr_t pc)
{
    CPUState *cs = env_cpu(env);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPURISCVState *env, uint32_t exception)
{
    riscv_raise_exception(env, exception, 0);
}

target_ulong helper_csrr(CPURISCVState *env, int csr)
{
    /*
     * The seed CSR must be accessed with a read-write instruction. A
     * read-only instruction such as CSRRS/CSRRC with rs1=x0 or CSRRSI/
     * CSRRCI with uimm=0 will raise an illegal instruction exception.
     */
    if (csr == CSR_SEED) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong val = 0;
    RISCVException ret = riscv_csrrw(env, csr, &val, 0, 0);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

void helper_csrw(CPURISCVState *env, int csr, target_ulong src)
{
    target_ulong mask = env->xl == MXL_RV32 ? UINT32_MAX : (target_ulong)-1;
    RISCVException ret = riscv_csrrw(env, csr, NULL, src, mask);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw(CPURISCVState *env, int csr,
                          target_ulong src, target_ulong write_mask)
{
    target_ulong val = 0;
    RISCVException ret = riscv_csrrw(env, csr, &val, src, write_mask);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

target_ulong helper_csrr_i128(CPURISCVState *env, int csr)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrrw_i128(env, csr, &rv,
                                          int128_zero(),
                                          int128_zero());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}

void helper_csrw_i128(CPURISCVState *env, int csr,
                      target_ulong srcl, target_ulong srch)
{
    RISCVException ret = riscv_csrrw_i128(env, csr, NULL,
                                          int128_make128(srcl, srch),
                                          UINT128_MAX);

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw_i128(CPURISCVState *env, int csr,
                       target_ulong srcl, target_ulong srch,
                       target_ulong maskl, target_ulong maskh)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrrw_i128(env, csr, &rv,
                                          int128_make128(srcl, srch),
                                          int128_make128(maskl, maskh));

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}

#ifndef CONFIG_USER_ONLY

target_ulong helper_sret(CPURISCVState *env)
{
    uint64_t mstatus;
    target_ulong prev_priv, prev_virt;

    if (!(env->priv >= PRV_S)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->sepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    if (get_field(env->mstatus, MSTATUS_TSR) && !(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    if (riscv_has_ext(env, RVH) && riscv_cpu_virt_enabled(env) &&
        get_field(env->hstatus, HSTATUS_VTSR)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    mstatus = env->mstatus;

    if (riscv_has_ext(env, RVH) && !riscv_cpu_virt_enabled(env)) {
        /* We support Hypervisor extensions and virtulisation is disabled */
        target_ulong hstatus = env->hstatus;

        prev_priv = get_field(mstatus, MSTATUS_SPP);
        prev_virt = get_field(hstatus, HSTATUS_SPV);

        hstatus = set_field(hstatus, HSTATUS_SPV, 0);
        mstatus = set_field(mstatus, MSTATUS_SPP, 0);
        mstatus = set_field(mstatus, SSTATUS_SIE,
                            get_field(mstatus, SSTATUS_SPIE));
        mstatus = set_field(mstatus, SSTATUS_SPIE, 1);

        env->mstatus = mstatus;
        env->hstatus = hstatus;

        if (prev_virt) {
            riscv_cpu_swap_hypervisor_regs(env);
        }

        riscv_cpu_set_virt_enabled(env, prev_virt);
    } else {
        prev_priv = get_field(mstatus, MSTATUS_SPP);

        mstatus = set_field(mstatus, MSTATUS_SIE,
                            get_field(mstatus, MSTATUS_SPIE));
        mstatus = set_field(mstatus, MSTATUS_SPIE, 1);
        mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);
        env->mstatus = mstatus;
    }

    riscv_cpu_set_mode(env, prev_priv);

    return retpc;
}

target_ulong helper_mret(CPURISCVState *env)
{
    if (!(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong retpc = env->mepc;
    if (!riscv_has_ext(env, RVC) && (retpc & 0x3)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ADDR_MIS, GETPC());
    }

    uint64_t mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);

    if (riscv_feature(env, RISCV_FEATURE_PMP) &&
        !pmp_get_num_rules(env) && (prev_priv != PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong prev_virt = get_field(env->mstatus, MSTATUS_MPV);
    mstatus = set_field(mstatus, MSTATUS_MIE,
                        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_MPP, PRV_U);
    mstatus = set_field(mstatus, MSTATUS_MPV, 0);
    env->mstatus = mstatus;
    riscv_cpu_set_mode(env, prev_priv);

    if (riscv_has_ext(env, RVH)) {
        if (prev_virt) {
            riscv_cpu_swap_hypervisor_regs(env);
        }

        riscv_cpu_set_virt_enabled(env, prev_virt);
    }

    return retpc;
}

void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    bool rvs = riscv_has_ext(env, RVS);
    bool prv_u = env->priv == PRV_U;
    bool prv_s = env->priv == PRV_S;

    if (((prv_s || (!rvs && prv_u)) && get_field(env->mstatus, MSTATUS_TW)) ||
        (rvs && prv_u && !riscv_cpu_virt_enabled(env))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (riscv_cpu_virt_enabled(env) && (prv_u ||
        (prv_s && get_field(env->hstatus, HSTATUS_VTW)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cpu_loop_exit(cs);
    }
}

void helper_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    if (!(env->priv >= PRV_S) ||
        (env->priv == PRV_S &&
         get_field(env->mstatus, MSTATUS_TVM))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (riscv_has_ext(env, RVH) && riscv_cpu_virt_enabled(env) &&
               get_field(env->hstatus, HSTATUS_VTVM)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        tlb_flush(cs);
    }
}

void helper_hyp_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);

    if (env->priv == PRV_S && riscv_cpu_virt_enabled(env)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    if (env->priv == PRV_M ||
        (env->priv == PRV_S && !riscv_cpu_virt_enabled(env))) {
        tlb_flush(cs);
        return;
    }

    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
}

void helper_hyp_gvma_tlb_flush(CPURISCVState *env)
{
    if (env->priv == PRV_S && !riscv_cpu_virt_enabled(env) &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    helper_hyp_tlb_flush(env);
}

target_ulong helper_hyp_hlvx_hu(CPURISCVState *env, target_ulong address)
{
    int mmu_idx = cpu_mmu_index(env, true) | TB_FLAGS_PRIV_HYP_ACCESS_MASK;

    return cpu_lduw_mmuidx_ra(env, address, mmu_idx, GETPC());
}

target_ulong helper_hyp_hlvx_wu(CPURISCVState *env, target_ulong address)
{
    int mmu_idx = cpu_mmu_index(env, true) | TB_FLAGS_PRIV_HYP_ACCESS_MASK;

    return cpu_ldl_mmuidx_ra(env, address, mmu_idx, GETPC());
}

#endif /* !CONFIG_USER_ONLY */
