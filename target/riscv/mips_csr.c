/*
 * MIPS-specific CSRs.
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_vendorid.h"

/* Static MIPS CSR state storage */
static struct {
    uint64_t tvec;
    uint64_t config[12];
    uint64_t pmacfg[16];
} mips_csr_state;

/* MIPS CSR */
#define CSR_MIPSTVEC        0x7c0
#define CSR_MIPSCONFIG0     0x7d0
#define CSR_MIPSCONFIG1     0x7d1
#define CSR_MIPSCONFIG2     0x7d2
#define CSR_MIPSCONFIG3     0x7d3
#define CSR_MIPSCONFIG4     0x7d4
#define CSR_MIPSCONFIG5     0x7d5
#define CSR_MIPSCONFIG6     0x7d6
#define CSR_MIPSCONFIG7     0x7d7
#define CSR_MIPSCONFIG8     0x7d8
#define CSR_MIPSCONFIG9     0x7d9
#define CSR_MIPSCONFIG10    0x7da
#define CSR_MIPSCONFIG11    0x7db
#define CSR_MIPSPMACFG0     0x7e0
#define CSR_MIPSPMACFG1     0x7e1
#define CSR_MIPSPMACFG2     0x7e2
#define CSR_MIPSPMACFG3     0x7e3
#define CSR_MIPSPMACFG4     0x7e4
#define CSR_MIPSPMACFG5     0x7e5
#define CSR_MIPSPMACFG6     0x7e6
#define CSR_MIPSPMACFG7     0x7e7
#define CSR_MIPSPMACFG8     0x7e8
#define CSR_MIPSPMACFG9     0x7e9
#define CSR_MIPSPMACFG10    0x7ea
#define CSR_MIPSPMACFG11    0x7eb
#define CSR_MIPSPMACFG12    0x7ec
#define CSR_MIPSPMACFG13    0x7ed
#define CSR_MIPSPMACFG14    0x7ee
#define CSR_MIPSPMACFG15    0x7ef

static RISCVException any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static RISCVException read_mipstvec(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = mips_csr_state.tvec;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mipstvec(CPURISCVState *env, int csrno,
                                     target_ulong val, uintptr_t ra)
{
    mips_csr_state.tvec = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mipsconfig(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = mips_csr_state.config[csrno - CSR_MIPSCONFIG0];
    return RISCV_EXCP_NONE;
}

static RISCVException write_mipsconfig(CPURISCVState *env, int csrno,
                                       target_ulong val, uintptr_t ra)
{
    mips_csr_state.config[csrno - CSR_MIPSCONFIG0] = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mipspmacfg(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = mips_csr_state.pmacfg[csrno - CSR_MIPSPMACFG0];
    return RISCV_EXCP_NONE;
}

static RISCVException write_mipspmacfg(CPURISCVState *env, int csrno,
                                       target_ulong val, uintptr_t ra)
{
    mips_csr_state.pmacfg[csrno - CSR_MIPSPMACFG0] = val;
    return RISCV_EXCP_NONE;
}

const RISCVCSR mips_csr_list[] = {
    {
        .csrno = CSR_MIPSTVEC,
        .csr_ops = { "mipstvec", any, read_mipstvec, write_mipstvec }
    },
    {
        .csrno = CSR_MIPSCONFIG0,
        .csr_ops = { "mipsconfig0", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG1,
        .csr_ops = { "mipsconfig1", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG2,
        .csr_ops = { "mipsconfig2", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG3,
        .csr_ops = { "mipsconfig3", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG4,
        .csr_ops = { "mipsconfig4", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG5,
        .csr_ops = { "mipsconfig5", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG6,
        .csr_ops = { "mipsconfig6", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG7,
        .csr_ops = { "mipsconfig7", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG8,
        .csr_ops = { "mipsconfig8", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG9,
        .csr_ops = { "mipsconfig9", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG10,
        .csr_ops = { "mipsconfig10", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSCONFIG11,
        .csr_ops = { "mipsconfig11", any, read_mipsconfig, write_mipsconfig }
    },
    {
        .csrno = CSR_MIPSPMACFG0,
        .csr_ops = { "mipspmacfg0", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG1,
        .csr_ops = { "mipspmacfg1", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG2,
        .csr_ops = { "mipspmacfg2", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG3,
        .csr_ops = { "mipspmacfg3", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG4,
        .csr_ops = { "mipspmacfg4", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG5,
        .csr_ops = { "mipspmacfg5", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG6,
        .csr_ops = { "mipspmacfg6", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG7,
        .csr_ops = { "mipspmacfg7", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG8,
        .csr_ops = { "mipspmacfg8", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG9,
        .csr_ops = { "mipspmacfg9", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG10,
        .csr_ops = { "mipspmacfg10", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG11,
        .csr_ops = { "mipspmacfg11", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG12,
        .csr_ops = { "mipspmacfg12", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG13,
        .csr_ops = { "mipspmacfg13", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG14,
        .csr_ops = { "mipspmacfg14", any, read_mipspmacfg, write_mipspmacfg }
    },
    {
        .csrno = CSR_MIPSPMACFG15,
        .csr_ops = { "mipspmacfg15", any, read_mipspmacfg, write_mipspmacfg }
    },
    { },
};
