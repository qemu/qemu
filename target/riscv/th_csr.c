/*
 * T-Head-specific CSRs.
 *
 * Copyright (c) 2024 VRULL GmbH
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

#define CSR_TH_SXSTATUS 0x5c0

/* TH_SXSTATUS bits */
#define TH_SXSTATUS_UCME        BIT(16)
#define TH_SXSTATUS_MAEE        BIT(21)
#define TH_SXSTATUS_THEADISAEE  BIT(22)

static RISCVException smode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static bool test_thead_mvendorid(RISCVCPU *cpu)
{
    return cpu->cfg.mvendorid == THEAD_VENDOR_ID;
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
        .csrno = CSR_TH_SXSTATUS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.sxstatus", smode, read_th_sxstatus }
    },
    { }
};
