/*
 * Neorv32-specific CSR.
 *
 * Copyright (c) 2025 Michael Levit
 *
 * Author:
 *   Michael Levit <michael@videogpu.com>
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

#define    CSR_MXISA    (0xfc0)

static RISCVException smode(CPURISCVState *env, int csrno)
{
	return RISCV_EXCP_NONE;
}

static RISCVException read_neorv32_xisa(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
	/* We don't support any extension for now on QEMU */
    *val = 0x00;
    return RISCV_EXCP_NONE;
}

static bool test_neorv32_mvendorid(RISCVCPU *cpu)
{
    return cpu->cfg.mvendorid == NEORV32_VENDOR_ID;
}

const RISCVCSR neorv32_csr_list[] = {
    {
        .csrno = CSR_MXISA,
        .insertion_test = test_neorv32_mvendorid,
        .csr_ops = { "neorv32.xisa", smode, read_neorv32_xisa }
    },
    { }
};

