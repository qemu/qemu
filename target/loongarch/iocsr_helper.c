/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * Helpers for IOCSR reads/writes
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"

#define GET_MEMTXATTRS(cas) \
        ((MemTxAttrs){.requester_id = env_cpu(cas)->cpu_index})

uint64_t helper_iocsrrd_b(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_ldub(&env->address_space_iocsr, r_addr,
                              GET_MEMTXATTRS(env), NULL);
}

uint64_t helper_iocsrrd_h(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_lduw(&env->address_space_iocsr, r_addr,
                              GET_MEMTXATTRS(env), NULL);
}

uint64_t helper_iocsrrd_w(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_ldl(&env->address_space_iocsr, r_addr,
                             GET_MEMTXATTRS(env), NULL);
}

uint64_t helper_iocsrrd_d(CPULoongArchState *env, target_ulong r_addr)
{
    return address_space_ldq(&env->address_space_iocsr, r_addr,
                             GET_MEMTXATTRS(env), NULL);
}

void helper_iocsrwr_b(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    address_space_stb(&env->address_space_iocsr, w_addr,
                      val, GET_MEMTXATTRS(env), NULL);
}

void helper_iocsrwr_h(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    address_space_stw(&env->address_space_iocsr, w_addr,
                      val, GET_MEMTXATTRS(env), NULL);
}

void helper_iocsrwr_w(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    address_space_stl(&env->address_space_iocsr, w_addr,
                      val, GET_MEMTXATTRS(env), NULL);
}

void helper_iocsrwr_d(CPULoongArchState *env, target_ulong w_addr,
                      target_ulong val)
{
    address_space_stq(&env->address_space_iocsr, w_addr,
                      val, GET_MEMTXATTRS(env), NULL);
}
