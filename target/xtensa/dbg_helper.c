/*
 * Copyright (c) 2011 - 2019, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/address-spaces.h"

static void tb_invalidate_virtual_addr(CPUXtensaState *env, uint32_t vaddr)
{
    uint32_t paddr;
    uint32_t page_size;
    unsigned access;
    int ret = xtensa_get_physical_addr(env, false, vaddr, 2, 0,
                                       &paddr, &page_size, &access);
    if (ret == 0) {
        tb_invalidate_phys_addr(&address_space_memory, paddr,
                                MEMTXATTRS_UNSPECIFIED);
    }
}

void HELPER(wsr_ibreakenable)(CPUXtensaState *env, uint32_t v)
{
    uint32_t change = v ^ env->sregs[IBREAKENABLE];
    unsigned i;

    for (i = 0; i < env->config->nibreak; ++i) {
        if (change & (1 << i)) {
            tb_invalidate_virtual_addr(env, env->sregs[IBREAKA + i]);
        }
    }
    env->sregs[IBREAKENABLE] = v & ((1 << env->config->nibreak) - 1);
}

void HELPER(wsr_ibreaka)(CPUXtensaState *env, uint32_t i, uint32_t v)
{
    if (env->sregs[IBREAKENABLE] & (1 << i) && env->sregs[IBREAKA + i] != v) {
        tb_invalidate_virtual_addr(env, env->sregs[IBREAKA + i]);
        tb_invalidate_virtual_addr(env, v);
    }
    env->sregs[IBREAKA + i] = v;
}

static void set_dbreak(CPUXtensaState *env, unsigned i, uint32_t dbreaka,
        uint32_t dbreakc)
{
    CPUState *cs = env_cpu(env);
    int flags = BP_CPU | BP_STOP_BEFORE_ACCESS;
    uint32_t mask = dbreakc | ~DBREAKC_MASK;

    if (env->cpu_watchpoint[i]) {
        cpu_watchpoint_remove_by_ref(cs, env->cpu_watchpoint[i]);
    }
    if (dbreakc & DBREAKC_SB) {
        flags |= BP_MEM_WRITE;
    }
    if (dbreakc & DBREAKC_LB) {
        flags |= BP_MEM_READ;
    }
    /* contiguous mask after inversion is one less than some power of 2 */
    if ((~mask + 1) & ~mask) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "DBREAKC mask is not contiguous: 0x%08x\n", dbreakc);
        /* cut mask after the first zero bit */
        mask = 0xffffffff << (32 - clo32(mask));
    }
    if (cpu_watchpoint_insert(cs, dbreaka & mask, ~mask + 1,
                              flags, &env->cpu_watchpoint[i])) {
        env->cpu_watchpoint[i] = NULL;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Failed to set data breakpoint at 0x%08x/%d\n",
                      dbreaka & mask, ~mask + 1);
    }
}

void HELPER(wsr_dbreaka)(CPUXtensaState *env, uint32_t i, uint32_t v)
{
    uint32_t dbreakc = env->sregs[DBREAKC + i];

    if ((dbreakc & DBREAKC_SB_LB) &&
        env->sregs[DBREAKA + i] != v) {
        set_dbreak(env, i, v, dbreakc);
    }
    env->sregs[DBREAKA + i] = v;
}

void HELPER(wsr_dbreakc)(CPUXtensaState *env, uint32_t i, uint32_t v)
{
    if ((env->sregs[DBREAKC + i] ^ v) & (DBREAKC_SB_LB | DBREAKC_MASK)) {
        if (v & DBREAKC_SB_LB) {
            set_dbreak(env, i, env->sregs[DBREAKA + i], v);
        } else {
            if (env->cpu_watchpoint[i]) {
                CPUState *cs = env_cpu(env);

                cpu_watchpoint_remove_by_ref(cs, env->cpu_watchpoint[i]);
                env->cpu_watchpoint[i] = NULL;
            }
        }
    }
    env->sregs[DBREAKC + i] = v;
}
