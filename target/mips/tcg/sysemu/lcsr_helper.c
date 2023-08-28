/*
 * Loongson CSR instructions translation routines
 *
 *  Copyright (c) 2023 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

#define GET_MEMTXATTRS(cas) \
        ((MemTxAttrs){.requester_id = env_cpu(cas)->cpu_index})

uint64_t helper_lcsr_rdcsr(CPUMIPSState *env, target_ulong r_addr)
{
    return address_space_ldl(&env->iocsr.as, r_addr,
                             GET_MEMTXATTRS(env), NULL);
}

uint64_t helper_lcsr_drdcsr(CPUMIPSState *env, target_ulong r_addr)
{
    return address_space_ldq(&env->iocsr.as, r_addr,
                             GET_MEMTXATTRS(env), NULL);
}

void helper_lcsr_wrcsr(CPUMIPSState *env, target_ulong w_addr,
                      target_ulong val)
{
    address_space_stl(&env->iocsr.as, w_addr,
                      val, GET_MEMTXATTRS(env), NULL);
}

void helper_lcsr_dwrcsr(CPUMIPSState *env, target_ulong w_addr,
                      target_ulong val)
{
    address_space_stq(&env->iocsr.as, w_addr,
                      val, GET_MEMTXATTRS(env), NULL);
}
