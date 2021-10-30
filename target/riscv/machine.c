/*
 * RISC-V VMState Description
 *
 * Copyright (c) 2020 Huawei Technologies Co., Ltd
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
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "migration/cpu.h"

static bool pmp_needed(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;

    return riscv_feature(env, RISCV_FEATURE_PMP);
}

static int pmp_post_load(void *opaque, int version_id)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;
    int i;

    for (i = 0; i < MAX_RISCV_PMPS; i++) {
        pmp_update_rule_addr(env, i);
    }
    pmp_update_rule_nums(env);

    return 0;
}

static const VMStateDescription vmstate_pmp_entry = {
    .name = "cpu/pmp/entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(addr_reg, pmp_entry_t),
        VMSTATE_UINT8(cfg_reg, pmp_entry_t),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pmp = {
    .name = "cpu/pmp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pmp_needed,
    .post_load = pmp_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.pmp_state.pmp, RISCVCPU, MAX_RISCV_PMPS,
                             0, vmstate_pmp_entry, pmp_entry_t),
        VMSTATE_END_OF_LIST()
    }
};

static bool hyper_needed(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;

    return riscv_has_ext(env, RVH);
}

static const VMStateDescription vmstate_hyper = {
    .name = "cpu/hyper",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = hyper_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(env.hstatus, RISCVCPU),
        VMSTATE_UINTTL(env.hedeleg, RISCVCPU),
        VMSTATE_UINTTL(env.hideleg, RISCVCPU),
        VMSTATE_UINTTL(env.hcounteren, RISCVCPU),
        VMSTATE_UINTTL(env.htval, RISCVCPU),
        VMSTATE_UINTTL(env.htinst, RISCVCPU),
        VMSTATE_UINTTL(env.hgatp, RISCVCPU),
        VMSTATE_UINT64(env.htimedelta, RISCVCPU),

        VMSTATE_UINT64(env.vsstatus, RISCVCPU),
        VMSTATE_UINTTL(env.vstvec, RISCVCPU),
        VMSTATE_UINTTL(env.vsscratch, RISCVCPU),
        VMSTATE_UINTTL(env.vsepc, RISCVCPU),
        VMSTATE_UINTTL(env.vscause, RISCVCPU),
        VMSTATE_UINTTL(env.vstval, RISCVCPU),
        VMSTATE_UINTTL(env.vsatp, RISCVCPU),

        VMSTATE_UINTTL(env.mtval2, RISCVCPU),
        VMSTATE_UINTTL(env.mtinst, RISCVCPU),

        VMSTATE_UINTTL(env.stvec_hs, RISCVCPU),
        VMSTATE_UINTTL(env.sscratch_hs, RISCVCPU),
        VMSTATE_UINTTL(env.sepc_hs, RISCVCPU),
        VMSTATE_UINTTL(env.scause_hs, RISCVCPU),
        VMSTATE_UINTTL(env.stval_hs, RISCVCPU),
        VMSTATE_UINTTL(env.satp_hs, RISCVCPU),
        VMSTATE_UINT64(env.mstatus_hs, RISCVCPU),

        VMSTATE_END_OF_LIST()
    }
};

static bool vector_needed(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;

    return riscv_has_ext(env, RVV);
}

static const VMStateDescription vmstate_vector = {
    .name = "cpu/vector",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vector_needed,
    .fields = (VMStateField[]) {
            VMSTATE_UINT64_ARRAY(env.vreg, RISCVCPU, 32 * RV_VLEN_MAX / 64),
            VMSTATE_UINTTL(env.vxrm, RISCVCPU),
            VMSTATE_UINTTL(env.vxsat, RISCVCPU),
            VMSTATE_UINTTL(env.vl, RISCVCPU),
            VMSTATE_UINTTL(env.vstart, RISCVCPU),
            VMSTATE_UINTTL(env.vtype, RISCVCPU),
            VMSTATE_END_OF_LIST()
        }
};

static bool pointermasking_needed(void *opaque)
{
    RISCVCPU *cpu = opaque;
    CPURISCVState *env = &cpu->env;

    return riscv_has_ext(env, RVJ);
}

static const VMStateDescription vmstate_pointermasking = {
    .name = "cpu/pointer_masking",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pointermasking_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(env.mmte, RISCVCPU),
        VMSTATE_UINTTL(env.mpmmask, RISCVCPU),
        VMSTATE_UINTTL(env.mpmbase, RISCVCPU),
        VMSTATE_UINTTL(env.spmmask, RISCVCPU),
        VMSTATE_UINTTL(env.spmbase, RISCVCPU),
        VMSTATE_UINTTL(env.upmmask, RISCVCPU),
        VMSTATE_UINTTL(env.upmbase, RISCVCPU),

        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_riscv_cpu = {
    .name = "cpu",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.gpr, RISCVCPU, 32),
        VMSTATE_UINT64_ARRAY(env.fpr, RISCVCPU, 32),
        VMSTATE_UINTTL(env.pc, RISCVCPU),
        VMSTATE_UINTTL(env.load_res, RISCVCPU),
        VMSTATE_UINTTL(env.load_val, RISCVCPU),
        VMSTATE_UINTTL(env.frm, RISCVCPU),
        VMSTATE_UINTTL(env.badaddr, RISCVCPU),
        VMSTATE_UINTTL(env.guest_phys_fault_addr, RISCVCPU),
        VMSTATE_UINTTL(env.priv_ver, RISCVCPU),
        VMSTATE_UINTTL(env.vext_ver, RISCVCPU),
        VMSTATE_UINT32(env.misa_mxl, RISCVCPU),
        VMSTATE_UINT32(env.misa_ext, RISCVCPU),
        VMSTATE_UINT32(env.misa_mxl_max, RISCVCPU),
        VMSTATE_UINT32(env.misa_ext_mask, RISCVCPU),
        VMSTATE_UINT32(env.features, RISCVCPU),
        VMSTATE_UINTTL(env.priv, RISCVCPU),
        VMSTATE_UINTTL(env.virt, RISCVCPU),
        VMSTATE_UINTTL(env.resetvec, RISCVCPU),
        VMSTATE_UINTTL(env.mhartid, RISCVCPU),
        VMSTATE_UINT64(env.mstatus, RISCVCPU),
        VMSTATE_UINTTL(env.mip, RISCVCPU),
        VMSTATE_UINT32(env.miclaim, RISCVCPU),
        VMSTATE_UINTTL(env.mie, RISCVCPU),
        VMSTATE_UINTTL(env.mideleg, RISCVCPU),
        VMSTATE_UINTTL(env.satp, RISCVCPU),
        VMSTATE_UINTTL(env.stval, RISCVCPU),
        VMSTATE_UINTTL(env.medeleg, RISCVCPU),
        VMSTATE_UINTTL(env.stvec, RISCVCPU),
        VMSTATE_UINTTL(env.sepc, RISCVCPU),
        VMSTATE_UINTTL(env.scause, RISCVCPU),
        VMSTATE_UINTTL(env.mtvec, RISCVCPU),
        VMSTATE_UINTTL(env.mepc, RISCVCPU),
        VMSTATE_UINTTL(env.mcause, RISCVCPU),
        VMSTATE_UINTTL(env.mtval, RISCVCPU),
        VMSTATE_UINTTL(env.scounteren, RISCVCPU),
        VMSTATE_UINTTL(env.mcounteren, RISCVCPU),
        VMSTATE_UINTTL(env.sscratch, RISCVCPU),
        VMSTATE_UINTTL(env.mscratch, RISCVCPU),
        VMSTATE_UINT64(env.mfromhost, RISCVCPU),
        VMSTATE_UINT64(env.mtohost, RISCVCPU),
        VMSTATE_UINT64(env.timecmp, RISCVCPU),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_pmp,
        &vmstate_hyper,
        &vmstate_vector,
        &vmstate_pointermasking,
        NULL
    }
};
