/*
 * QEMU RISC-V CSRs
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_CSR_H
#define RISCV_CSR_H

#include "exec/target_long.h"
#include "cpu_bits.h"

target_ulong riscv_new_csr_seed(target_ulong new_value,
                                target_ulong write_mask);

RISCVException riscv_csrr(CPURISCVState *env, int csrno,
                          target_ulong *ret_value);

RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value, target_ulong new_value,
                           target_ulong write_mask, uintptr_t ra);
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask);

static inline void riscv_csr_write(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    riscv_csrrw(env, csrno, NULL, val, MAKE_64BIT_MASK(0, TARGET_LONG_BITS), 0);
}

static inline target_ulong riscv_csr_read(CPURISCVState *env, int csrno)
{
    target_ulong val = 0;
    riscv_csrr(env, csrno, &val);
    return val;
}

typedef RISCVException (*riscv_csr_predicate_fn)(CPURISCVState *env,
                                                 int csrno);
typedef RISCVException (*riscv_csr_read_fn)(CPURISCVState *env, int csrno,
                                            target_ulong *ret_value);
typedef RISCVException (*riscv_csr_write_fn)(CPURISCVState *env, int csrno,
                                             target_ulong new_value,
                                             uintptr_t ra);
typedef RISCVException (*riscv_csr_op_fn)(CPURISCVState *env, int csrno,
                                          target_ulong *ret_value,
                                          target_ulong new_value,
                                          target_ulong write_mask);

RISCVException riscv_csrr_i128(CPURISCVState *env, int csrno,
                               Int128 *ret_value);
RISCVException riscv_csrrw_i128(CPURISCVState *env, int csrno,
                                Int128 *ret_value, Int128 new_value,
                                Int128 write_mask, uintptr_t ra);

typedef RISCVException (*riscv_csr_read128_fn)(CPURISCVState *env, int csrno,
                                               Int128 *ret_value);
typedef RISCVException (*riscv_csr_write128_fn)(CPURISCVState *env, int csrno,
                                             Int128 new_value);

typedef struct {
    const char *name;
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
    riscv_csr_read128_fn read128;
    riscv_csr_write128_fn write128;
    /* The default priv spec version should be PRIV_VERSION_1_10_0 (i.e 0) */
    uint32_t min_priv_ver;
} riscv_csr_operations;

struct RISCVCSR {
    int csrno;
    bool (*insertion_test)(RISCVCPU *cpu);
    riscv_csr_operations csr_ops;
};

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

/* CSR function table */
extern riscv_csr_operations csr_ops[CSR_TABLE_SIZE];

bool riscv_csr_is_fpu(int csrno);
bool riscv_csr_is_vpu(int csrno);

void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops);
void riscv_set_csr_ops(int csrno, const riscv_csr_operations *ops);

/* In th_csr.c */
extern const RISCVCSR th_csr_list[];

/* Implemented in mips_csr.c */
extern const RISCVCSR mips_csr_list[];

#endif /* RISCV_CSR_H */
