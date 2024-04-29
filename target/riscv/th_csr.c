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

typedef struct {
    int csrno;
    int (*insertion_test)(RISCVCPU *cpu);
    riscv_csr_operations csr_ops;
} riscv_csr;

static RISCVException smode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static int test_thead_mvendorid(RISCVCPU *cpu)
{
    if (cpu->cfg.mvendorid != THEAD_VENDOR_ID) {
        return -1;
    }

    return 0;
}

static RISCVException read_th_sxstatus(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    /* We don't set MAEE here, because QEMU does not implement MAEE. */
    *val = TH_SXSTATUS_UCME | TH_SXSTATUS_THEADISAEE;
    return RISCV_EXCP_NONE;
}

static riscv_csr th_csr_list[] = {
    {
        .csrno = CSR_TH_SXSTATUS,
        .insertion_test = test_thead_mvendorid,
        .csr_ops = { "th.sxstatus", smode, read_th_sxstatus }
    }
};

void th_register_custom_csrs(RISCVCPU *cpu)
{
    for (size_t i = 0; i < ARRAY_SIZE(th_csr_list); i++) {
        int csrno = th_csr_list[i].csrno;
        riscv_csr_operations *csr_ops = &th_csr_list[i].csr_ops;
        if (!th_csr_list[i].insertion_test(cpu)) {
            riscv_set_csr_ops(csrno, csr_ops);
        }
    }
}
