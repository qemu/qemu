/*
 * T-Head-specific CSRs.
 *
 * Copyright (c) 2024 VRULL GmbH
 * Copyright (c) 2025 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * For more information, see XuanTie-C908-UserManual_xrvm_20240530.pdf
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
#include "cpu_vendorid.h"
#include "target/riscv/csr.h"

/* Extended M-mode control registers of T-Head */
#define CSR_TH_MXSTATUS        0x7c0
#define CSR_TH_MHCR            0x7c1
#define CSR_TH_MCOR            0x7c2
#define CSR_TH_MCCR2           0x7c3
#define CSR_TH_MHINT           0x7c5
#define CSR_TH_MRVBR           0x7c7
#define CSR_TH_MCOUNTERWEN     0x7c9
#define CSR_TH_MCOUNTERINTEN   0x7ca
#define CSR_TH_MCOUNTEROF      0x7cb
#define CSR_TH_MCINS           0x7d2
#define CSR_TH_MCINDEX         0x7d3
#define CSR_TH_MCDATA0         0x7d4
#define CSR_TH_MCDATA1         0x7d5
#define CSR_TH_MSMPR           0x7f3
#define CSR_TH_CPUID           0xfc0
#define CSR_TH_MAPBADDR        0xfc1

/* TH_MXSTATUS bits */
#define TH_MXSTATUS_UCME        BIT(16)
#define TH_MXSTATUS_MAEE        BIT(21)
#define TH_MXSTATUS_THEADISAEE  BIT(22)

/* Extended S-mode control registers of T-Head */
#define CSR_TH_SXSTATUS        0x5c0
#define CSR_TH_SHCR            0x5c1
#define CSR_TH_SCER2           0x5c2
#define CSR_TH_SCER            0x5c3
#define CSR_TH_SCOUNTERINTEN   0x5c4
#define CSR_TH_SCOUNTEROF      0x5c5
#define CSR_TH_SCYCLE          0x5e0
#define CSR_TH_SHPMCOUNTER3    0x5e3
#define CSR_TH_SHPMCOUNTER4    0x5e4
#define CSR_TH_SHPMCOUNTER5    0x5e5
#define CSR_TH_SHPMCOUNTER6    0x5e6
#define CSR_TH_SHPMCOUNTER7    0x5e7
#define CSR_TH_SHPMCOUNTER8    0x5e8
#define CSR_TH_SHPMCOUNTER9    0x5e9
#define CSR_TH_SHPMCOUNTER10   0x5ea
#define CSR_TH_SHPMCOUNTER11   0x5eb
#define CSR_TH_SHPMCOUNTER12   0x5ec
#define CSR_TH_SHPMCOUNTER13   0x5ed
#define CSR_TH_SHPMCOUNTER14   0x5ee
#define CSR_TH_SHPMCOUNTER15   0x5ef
#define CSR_TH_SHPMCOUNTER16   0x5f0
#define CSR_TH_SHPMCOUNTER17   0x5f1
#define CSR_TH_SHPMCOUNTER18   0x5f2
#define CSR_TH_SHPMCOUNTER19   0x5f3
#define CSR_TH_SHPMCOUNTER20   0x5f4
#define CSR_TH_SHPMCOUNTER21   0x5f5
#define CSR_TH_SHPMCOUNTER22   0x5f6
#define CSR_TH_SHPMCOUNTER23   0x5f7
#define CSR_TH_SHPMCOUNTER24   0x5f8
#define CSR_TH_SHPMCOUNTER25   0x5f9
#define CSR_TH_SHPMCOUNTER26   0x5fa
#define CSR_TH_SHPMCOUNTER27   0x5fb
#define CSR_TH_SHPMCOUNTER28   0x5fc
#define CSR_TH_SHPMCOUNTER29   0x5fd
#define CSR_TH_SHPMCOUNTER30   0x5fe
#define CSR_TH_SHPMCOUNTER31   0x5ff
#define CSR_TH_SMIR            0x9c0
#define CSR_TH_SMLO0           0x9c1
#define CSR_TH_SMEH            0x9c2
#define CSR_TH_SMCIR           0x9c3

/* Extended U-mode control registers of T-Head */
#define CSR_TH_FXCR            0x800

/* TH_SXSTATUS bits */
#define TH_SXSTATUS_UCME        BIT(16)
#define TH_SXSTATUS_MAEE        BIT(21)
#define TH_SXSTATUS_THEADISAEE  BIT(22)

static RISCVException mmode(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static RISCVException smode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static bool test_thead_mvendorid(RISCVCPU *cpu)
{
    return cpu->cfg.mvendorid == THEAD_VENDOR_ID;
}

static RISCVException read_th_mxstatus(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    /* We don't set MAEE here, because QEMU does not implement MAEE. */
    *val = TH_MXSTATUS_UCME | TH_MXSTATUS_THEADISAEE;
    return RISCV_EXCP_NONE;
}

static RISCVException read_unimp_th_csr(CPURISCVState *env, int csrno,
                                        target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException read_th_sxstatus(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    /* We don't set MAEE here, because QEMU does not implement MAEE. */
    *val = TH_SXSTATUS_UCME | TH_SXSTATUS_THEADISAEE;
    return RISCV_EXCP_NONE;
}

const RISCVCSR th_csr_list[] = {
    {
        .csrno = CSR_TH_MXSTATUS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mxstatus", mmode, read_th_mxstatus }
    },
    {
        .csrno = CSR_TH_MHCR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mhcr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcor", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCCR2,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mccr2", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MHINT,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mhint", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MRVBR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mrvbr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOUNTERWEN,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcounterwen", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOUNTERINTEN,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcounterinten", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCOUNTEROF,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcounterof", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCINS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcins", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCINDEX,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcindex", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCDATA0,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcdata0", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MCDATA1,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mcdata1", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MSMPR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.msmpr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_CPUID,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.cpuid", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_MAPBADDR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.mapbaddr", mmode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SXSTATUS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.sxstatus", smode, read_th_sxstatus }
    },
    {
        .csrno = CSR_TH_SHCR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shcr", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCER2,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scer2", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCER,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scer", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCOUNTERINTEN,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scounterinten", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCOUNTEROF,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scounterof", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SCYCLE,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.scycle", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER3,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter3", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER4,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter4", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER5,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter5", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER6,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter6", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER7,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter7", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER8,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter8", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER9,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter9", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER10,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter10", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER11,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter11", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER12,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter12", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER13,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter13", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER14,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter14", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER15,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter15", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER16,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter16", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER17,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter17", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER18,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter18", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER19,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter19", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER20,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter20", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER21,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter21", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER22,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter22", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER23,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter23", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER24,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter24", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER25,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter25", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER26,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter26", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER27,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter27", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER28,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter28", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER29,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter29", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER30,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter30", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SHPMCOUNTER31,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.shpmcounter31", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMIR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smir", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMLO0,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smlo0", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMEH,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smeh", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_SMCIR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.smcir", smode, read_unimp_th_csr }
    },
    {
        .csrno = CSR_TH_FXCR,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.fxcr", any, read_unimp_th_csr }
    },
    { }
};
