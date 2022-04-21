/*
 * Altera Nios II MMU emulation for qemu.
 *
 * Copyright (C) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "mmu.h"
#include "exec/helper-proto.h"
#include "trace/trace-target_nios2.h"


/* rw - 0 = read, 1 = write, 2 = fetch.  */
unsigned int mmu_translate(CPUNios2State *env,
                           Nios2MMULookup *lu,
                           target_ulong vaddr, int rw, int mmu_idx)
{
    Nios2CPU *cpu = env_archcpu(env);
    int pid = FIELD_EX32(env->mmu.tlbmisc_wr, CR_TLBMISC, PID);
    int vpn = vaddr >> 12;
    int way, n_ways = cpu->tlb_num_ways;

    for (way = 0; way < n_ways; way++) {
        uint32_t index = (way * n_ways) + (vpn & env->mmu.tlb_entry_mask);
        Nios2TLBEntry *entry = &env->mmu.tlb[index];

        if (((entry->tag >> 12) != vpn) ||
            (((entry->tag & (1 << 11)) == 0) &&
            ((entry->tag & ((1 << cpu->pid_num_bits) - 1)) != pid))) {
            trace_nios2_mmu_translate_miss(vaddr, pid, index, entry->tag);
            continue;
        }

        lu->vaddr = vaddr & TARGET_PAGE_MASK;
        lu->paddr = FIELD_EX32(entry->data, CR_TLBACC, PFN) << TARGET_PAGE_BITS;
        lu->prot = ((entry->data & CR_TLBACC_R) ? PAGE_READ : 0) |
                   ((entry->data & CR_TLBACC_W) ? PAGE_WRITE : 0) |
                   ((entry->data & CR_TLBACC_X) ? PAGE_EXEC : 0);

        trace_nios2_mmu_translate_hit(vaddr, pid, index, lu->paddr, lu->prot);
        return 1;
    }
    return 0;
}

static void mmu_flush_pid(CPUNios2State *env, uint32_t pid)
{
    CPUState *cs = env_cpu(env);
    Nios2CPU *cpu = env_archcpu(env);
    int idx;

    for (idx = 0; idx < cpu->tlb_num_entries; idx++) {
        Nios2TLBEntry *entry = &env->mmu.tlb[idx];

        if ((entry->tag & (1 << 10)) && (!(entry->tag & (1 << 11))) &&
            ((entry->tag & ((1 << cpu->pid_num_bits) - 1)) == pid)) {
            uint32_t vaddr = entry->tag & TARGET_PAGE_MASK;

            trace_nios2_mmu_flush_pid_hit(pid, idx, vaddr);
            tlb_flush_page(cs, vaddr);
        } else {
            trace_nios2_mmu_flush_pid_miss(pid, idx, entry->tag);
        }
    }
}

void helper_mmu_write_tlbacc(CPUNios2State *env, uint32_t v)
{
    CPUState *cs = env_cpu(env);
    Nios2CPU *cpu = env_archcpu(env);

    trace_nios2_mmu_write_tlbacc(FIELD_EX32(v, CR_TLBACC, IG),
                                 (v & CR_TLBACC_C) ? 'C' : '.',
                                 (v & CR_TLBACC_R) ? 'R' : '.',
                                 (v & CR_TLBACC_W) ? 'W' : '.',
                                 (v & CR_TLBACC_X) ? 'X' : '.',
                                 (v & CR_TLBACC_G) ? 'G' : '.',
                                 FIELD_EX32(v, CR_TLBACC, PFN));

    /* if tlbmisc.WE == 1 then trigger a TLB write on writes to TLBACC */
    if (env->ctrl[CR_TLBMISC] & CR_TLBMISC_WE) {
        int way = FIELD_EX32(env->ctrl[CR_TLBMISC], CR_TLBMISC, WAY);
        int vpn = FIELD_EX32(env->mmu.pteaddr_wr, CR_PTEADDR, VPN);
        int pid = FIELD_EX32(env->mmu.tlbmisc_wr, CR_TLBMISC, PID);
        int g = FIELD_EX32(v, CR_TLBACC, G);
        int valid = FIELD_EX32(vpn, CR_TLBACC, PFN) < 0xC0000;
        Nios2TLBEntry *entry =
            &env->mmu.tlb[(way * cpu->tlb_num_ways) +
                          (vpn & env->mmu.tlb_entry_mask)];
        uint32_t newTag = (vpn << 12) | (g << 11) | (valid << 10) | pid;
        uint32_t newData = v & (CR_TLBACC_C | CR_TLBACC_R | CR_TLBACC_W |
                                CR_TLBACC_X | R_CR_TLBACC_PFN_MASK);

        if ((entry->tag != newTag) || (entry->data != newData)) {
            if (entry->tag & (1 << 10)) {
                /* Flush existing entry */
                tlb_flush_page(cs, entry->tag & TARGET_PAGE_MASK);
            }
            entry->tag = newTag;
            entry->data = newData;
        }
        /* Auto-increment tlbmisc.WAY */
        env->ctrl[CR_TLBMISC] = FIELD_DP32(env->ctrl[CR_TLBMISC],
                                           CR_TLBMISC, WAY,
                                           (way + 1) & (cpu->tlb_num_ways - 1));
    }

    /* Writes to TLBACC don't change the read-back value */
    env->mmu.tlbacc_wr = v;
}

void helper_mmu_write_tlbmisc(CPUNios2State *env, uint32_t v)
{
    Nios2CPU *cpu = env_archcpu(env);
    uint32_t new_pid = FIELD_EX32(v, CR_TLBMISC, PID);
    uint32_t old_pid = FIELD_EX32(env->mmu.tlbmisc_wr, CR_TLBMISC, PID);
    uint32_t way = FIELD_EX32(v, CR_TLBMISC, WAY);

    trace_nios2_mmu_write_tlbmisc(way,
                                  (v & CR_TLBMISC_RD) ? 'R' : '.',
                                  (v & CR_TLBMISC_WE) ? 'W' : '.',
                                  (v & CR_TLBMISC_DBL) ? '2' : '.',
                                  (v & CR_TLBMISC_BAD) ? 'B' : '.',
                                  (v & CR_TLBMISC_PERM) ? 'P' : '.',
                                  (v & CR_TLBMISC_D) ? 'D' : '.',
                                  new_pid);

    if (new_pid != old_pid) {
        mmu_flush_pid(env, old_pid);
    }

    /* if tlbmisc.RD == 1 then trigger a TLB read on writes to TLBMISC */
    if (v & CR_TLBMISC_RD) {
        int vpn = FIELD_EX32(env->mmu.pteaddr_wr, CR_PTEADDR, VPN);
        Nios2TLBEntry *entry =
            &env->mmu.tlb[(way * cpu->tlb_num_ways) +
                          (vpn & env->mmu.tlb_entry_mask)];

        env->ctrl[CR_TLBACC] &= R_CR_TLBACC_IG_MASK;
        env->ctrl[CR_TLBACC] |= entry->data;
        env->ctrl[CR_TLBACC] |= (entry->tag & (1 << 11)) ? CR_TLBACC_G : 0;
        env->ctrl[CR_TLBMISC] = FIELD_DP32(v, CR_TLBMISC, PID,
                                           entry->tag &
                                           ((1 << cpu->pid_num_bits) - 1));
        env->ctrl[CR_PTEADDR] = FIELD_DP32(env->ctrl[CR_PTEADDR],
                                           CR_PTEADDR, VPN,
                                           entry->tag >> TARGET_PAGE_BITS);
    } else {
        env->ctrl[CR_TLBMISC] = v;
    }

    env->mmu.tlbmisc_wr = v;
}

void helper_mmu_write_pteaddr(CPUNios2State *env, uint32_t v)
{
    trace_nios2_mmu_write_pteaddr(FIELD_EX32(v, CR_PTEADDR, PTBASE),
                                  FIELD_EX32(v, CR_PTEADDR, VPN));

    /* Writes to PTEADDR don't change the read-back VPN value */
    env->ctrl[CR_PTEADDR] = ((v & ~R_CR_PTEADDR_VPN_MASK) |
                             (env->ctrl[CR_PTEADDR] & R_CR_PTEADDR_VPN_MASK));
    env->mmu.pteaddr_wr = v;
}

void mmu_init(CPUNios2State *env)
{
    Nios2CPU *cpu = env_archcpu(env);
    Nios2MMU *mmu = &env->mmu;

    mmu->tlb_entry_mask = (cpu->tlb_num_entries / cpu->tlb_num_ways) - 1;
    mmu->tlb = g_new0(Nios2TLBEntry, cpu->tlb_num_entries);
}

void dump_mmu(CPUNios2State *env)
{
    Nios2CPU *cpu = env_archcpu(env);
    int i;

    qemu_printf("MMU: ways %d, entries %d, pid bits %d\n",
                cpu->tlb_num_ways, cpu->tlb_num_entries,
                cpu->pid_num_bits);

    for (i = 0; i < cpu->tlb_num_entries; i++) {
        Nios2TLBEntry *entry = &env->mmu.tlb[i];
        qemu_printf("TLB[%d] = %08X %08X %c VPN %05X "
                    "PID %02X %c PFN %05X %c%c%c%c\n",
                    i, entry->tag, entry->data,
                    (entry->tag & (1 << 10)) ? 'V' : '-',
                    entry->tag >> 12,
                    entry->tag & ((1 << cpu->pid_num_bits) - 1),
                    (entry->tag & (1 << 11)) ? 'G' : '-',
                    FIELD_EX32(entry->data, CR_TLBACC, PFN),
                    (entry->data & CR_TLBACC_C) ? 'C' : '-',
                    (entry->data & CR_TLBACC_R) ? 'R' : '-',
                    (entry->data & CR_TLBACC_W) ? 'W' : '-',
                    (entry->data & CR_TLBACC_X) ? 'X' : '-');
    }
}
