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
#include "qemu-common.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "mmu.h"

#if !defined(CONFIG_USER_ONLY)

/* Define this to enable MMU debug messages */
/* #define DEBUG_MMU */

#ifdef DEBUG_MMU
#define MMU_LOG(x) x
#else
#define MMU_LOG(x)
#endif

void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType access_type,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = nios2_cpu_handle_mmu_fault(cs, addr, access_type, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            cpu_restore_state(cs, retaddr);
        }
        cpu_loop_exit(cs);
    }
}

void mmu_read_debug(CPUNios2State *env, uint32_t rn)
{
    switch (rn) {
    case CR_TLBACC:
        MMU_LOG(qemu_log("TLBACC READ %08X\n", env->regs[rn]));
        break;

    case CR_TLBMISC:
        MMU_LOG(qemu_log("TLBMISC READ %08X\n", env->regs[rn]));
        break;

    case CR_PTEADDR:
        MMU_LOG(qemu_log("PTEADDR READ %08X\n", env->regs[rn]));
        break;

    default:
        break;
    }
}

/* rw - 0 = read, 1 = write, 2 = fetch.  */
unsigned int mmu_translate(CPUNios2State *env,
                           Nios2MMULookup *lu,
                           target_ulong vaddr, int rw, int mmu_idx)
{
    Nios2CPU *cpu = nios2_env_get_cpu(env);
    int pid = (env->mmu.tlbmisc_wr & CR_TLBMISC_PID_MASK) >> 4;
    int vpn = vaddr >> 12;

    MMU_LOG(qemu_log("mmu_translate vaddr %08X, pid %08X, vpn %08X\n",
                     vaddr, pid, vpn));

    int way;
    for (way = 0; way < cpu->tlb_num_ways; way++) {

        Nios2TLBEntry *entry =
            &env->mmu.tlb[(way * cpu->tlb_num_ways) +
                          (vpn & env->mmu.tlb_entry_mask)];

        MMU_LOG(qemu_log("TLB[%d] TAG %08X, VPN %08X\n",
                         (way * cpu->tlb_num_ways) +
                         (vpn & env->mmu.tlb_entry_mask),
                         entry->tag, (entry->tag >> 12)));

        if (((entry->tag >> 12) != vpn) ||
            (((entry->tag & (1 << 11)) == 0) &&
            ((entry->tag & ((1 << cpu->pid_num_bits) - 1)) != pid))) {
            continue;
        }
        lu->vaddr = vaddr & TARGET_PAGE_MASK;
        lu->paddr = (entry->data & CR_TLBACC_PFN_MASK) << TARGET_PAGE_BITS;
        lu->prot = ((entry->data & CR_TLBACC_R) ? PAGE_READ : 0) |
                   ((entry->data & CR_TLBACC_W) ? PAGE_WRITE : 0) |
                   ((entry->data & CR_TLBACC_X) ? PAGE_EXEC : 0);

        MMU_LOG(qemu_log("HIT TLB[%d] %08X %08X %08X\n",
                         (way * cpu->tlb_num_ways) +
                         (vpn & env->mmu.tlb_entry_mask),
                         lu->vaddr, lu->paddr, lu->prot));
        return 1;
    }
    return 0;
}

static void mmu_flush_pid(CPUNios2State *env, uint32_t pid)
{
    CPUState *cs = ENV_GET_CPU(env);
    Nios2CPU *cpu = nios2_env_get_cpu(env);
    int idx;
    MMU_LOG(qemu_log("TLB Flush PID %d\n", pid));

    for (idx = 0; idx < cpu->tlb_num_entries; idx++) {
        Nios2TLBEntry *entry = &env->mmu.tlb[idx];

        MMU_LOG(qemu_log("TLB[%d] => %08X %08X\n",
                         idx, entry->tag, entry->data));

        if ((entry->tag & (1 << 10)) && (!(entry->tag & (1 << 11))) &&
            ((entry->tag & ((1 << cpu->pid_num_bits) - 1)) == pid)) {
            uint32_t vaddr = entry->tag & TARGET_PAGE_MASK;

            MMU_LOG(qemu_log("TLB Flush Page %08X\n", vaddr));

            tlb_flush_page(cs, vaddr);
        }
    }
}

void mmu_write(CPUNios2State *env, uint32_t rn, uint32_t v)
{
    CPUState *cs = ENV_GET_CPU(env);
    Nios2CPU *cpu = nios2_env_get_cpu(env);

    MMU_LOG(qemu_log("mmu_write %08X = %08X\n", rn, v));

    switch (rn) {
    case CR_TLBACC:
        MMU_LOG(qemu_log("TLBACC: IG %02X, FLAGS %c%c%c%c%c, PFN %05X\n",
                         v >> CR_TLBACC_IGN_SHIFT,
                         (v & CR_TLBACC_C) ? 'C' : '.',
                         (v & CR_TLBACC_R) ? 'R' : '.',
                         (v & CR_TLBACC_W) ? 'W' : '.',
                         (v & CR_TLBACC_X) ? 'X' : '.',
                         (v & CR_TLBACC_G) ? 'G' : '.',
                         v & CR_TLBACC_PFN_MASK));

        /* if tlbmisc.WE == 1 then trigger a TLB write on writes to TLBACC */
        if (env->regs[CR_TLBMISC] & CR_TLBMISC_WR) {
            int way = (env->regs[CR_TLBMISC] >> CR_TLBMISC_WAY_SHIFT);
            int vpn = (env->mmu.pteaddr_wr & CR_PTEADDR_VPN_MASK) >> 2;
            int pid = (env->mmu.tlbmisc_wr & CR_TLBMISC_PID_MASK) >> 4;
            int g = (v & CR_TLBACC_G) ? 1 : 0;
            int valid = ((vpn & CR_TLBACC_PFN_MASK) < 0xC0000) ? 1 : 0;
            Nios2TLBEntry *entry =
                &env->mmu.tlb[(way * cpu->tlb_num_ways) +
                              (vpn & env->mmu.tlb_entry_mask)];
            uint32_t newTag = (vpn << 12) | (g << 11) | (valid << 10) | pid;
            uint32_t newData = v & (CR_TLBACC_C | CR_TLBACC_R | CR_TLBACC_W |
                                    CR_TLBACC_X | CR_TLBACC_PFN_MASK);

            if ((entry->tag != newTag) || (entry->data != newData)) {
                if (entry->tag & (1 << 10)) {
                    /* Flush existing entry */
                    MMU_LOG(qemu_log("TLB Flush Page (OLD) %08X\n",
                                     entry->tag & TARGET_PAGE_MASK));
                    tlb_flush_page(cs, entry->tag & TARGET_PAGE_MASK);
                }
                entry->tag = newTag;
                entry->data = newData;
                MMU_LOG(qemu_log("TLB[%d] = %08X %08X\n",
                                 (way * cpu->tlb_num_ways) +
                                 (vpn & env->mmu.tlb_entry_mask),
                                 entry->tag, entry->data));
            }
            /* Auto-increment tlbmisc.WAY */
            env->regs[CR_TLBMISC] =
                (env->regs[CR_TLBMISC] & ~CR_TLBMISC_WAY_MASK) |
                (((way + 1) & (cpu->tlb_num_ways - 1)) <<
                 CR_TLBMISC_WAY_SHIFT);
        }

        /* Writes to TLBACC don't change the read-back value */
        env->mmu.tlbacc_wr = v;
        break;

    case CR_TLBMISC:
        MMU_LOG(qemu_log("TLBMISC: WAY %X, FLAGS %c%c%c%c%c%c, PID %04X\n",
                         v >> CR_TLBMISC_WAY_SHIFT,
                         (v & CR_TLBMISC_RD) ? 'R' : '.',
                         (v & CR_TLBMISC_WR) ? 'W' : '.',
                         (v & CR_TLBMISC_DBL) ? '2' : '.',
                         (v & CR_TLBMISC_BAD) ? 'B' : '.',
                         (v & CR_TLBMISC_PERM) ? 'P' : '.',
                         (v & CR_TLBMISC_D) ? 'D' : '.',
                         (v & CR_TLBMISC_PID_MASK) >> 4));

        if ((v & CR_TLBMISC_PID_MASK) !=
            (env->mmu.tlbmisc_wr & CR_TLBMISC_PID_MASK)) {
            mmu_flush_pid(env, (env->mmu.tlbmisc_wr & CR_TLBMISC_PID_MASK) >>
                               CR_TLBMISC_PID_SHIFT);
        }
        /* if tlbmisc.RD == 1 then trigger a TLB read on writes to TLBMISC */
        if (v & CR_TLBMISC_RD) {
            int way = (v >> CR_TLBMISC_WAY_SHIFT);
            int vpn = (env->mmu.pteaddr_wr & CR_PTEADDR_VPN_MASK) >> 2;
            Nios2TLBEntry *entry =
                &env->mmu.tlb[(way * cpu->tlb_num_ways) +
                              (vpn & env->mmu.tlb_entry_mask)];

            env->regs[CR_TLBACC] &= CR_TLBACC_IGN_MASK;
            env->regs[CR_TLBACC] |= entry->data;
            env->regs[CR_TLBACC] |= (entry->tag & (1 << 11)) ? CR_TLBACC_G : 0;
            env->regs[CR_TLBMISC] =
                (v & ~CR_TLBMISC_PID_MASK) |
                ((entry->tag & ((1 << cpu->pid_num_bits) - 1)) <<
                 CR_TLBMISC_PID_SHIFT);
            env->regs[CR_PTEADDR] &= ~CR_PTEADDR_VPN_MASK;
            env->regs[CR_PTEADDR] |= (entry->tag >> 12) << CR_PTEADDR_VPN_SHIFT;
            MMU_LOG(qemu_log("TLB READ way %d, vpn %05X, tag %08X, data %08X, "
                             "tlbacc %08X, tlbmisc %08X, pteaddr %08X\n",
                             way, vpn, entry->tag, entry->data,
                             env->regs[CR_TLBACC], env->regs[CR_TLBMISC],
                             env->regs[CR_PTEADDR]));
        } else {
            env->regs[CR_TLBMISC] = v;
        }

        env->mmu.tlbmisc_wr = v;
        break;

    case CR_PTEADDR:
        MMU_LOG(qemu_log("PTEADDR: PTBASE %03X, VPN %05X\n",
                         v >> CR_PTEADDR_PTBASE_SHIFT,
                         (v & CR_PTEADDR_VPN_MASK) >> CR_PTEADDR_VPN_SHIFT));

        /* Writes to PTEADDR don't change the read-back VPN value */
        env->regs[CR_PTEADDR] = (v & ~CR_PTEADDR_VPN_MASK) |
                                (env->regs[CR_PTEADDR] & CR_PTEADDR_VPN_MASK);
        env->mmu.pteaddr_wr = v;
        break;

    default:
        break;
    }
}

void mmu_init(CPUNios2State *env)
{
    Nios2CPU *cpu = nios2_env_get_cpu(env);
    Nios2MMU *mmu = &env->mmu;

    MMU_LOG(qemu_log("mmu_init\n"));

    mmu->tlb_entry_mask = (cpu->tlb_num_entries / cpu->tlb_num_ways) - 1;
    mmu->tlb = g_new0(Nios2TLBEntry, cpu->tlb_num_entries);
}

void dump_mmu(FILE *f, fprintf_function cpu_fprintf, CPUNios2State *env)
{
    Nios2CPU *cpu = nios2_env_get_cpu(env);
    int i;

    cpu_fprintf(f, "MMU: ways %d, entries %d, pid bits %d\n",
                cpu->tlb_num_ways, cpu->tlb_num_entries,
                cpu->pid_num_bits);

    for (i = 0; i < cpu->tlb_num_entries; i++) {
        Nios2TLBEntry *entry = &env->mmu.tlb[i];
        cpu_fprintf(f, "TLB[%d] = %08X %08X %c VPN %05X "
                    "PID %02X %c PFN %05X %c%c%c%c\n",
                    i, entry->tag, entry->data,
                    (entry->tag & (1 << 10)) ? 'V' : '-',
                    entry->tag >> 12,
                    entry->tag & ((1 << cpu->pid_num_bits) - 1),
                    (entry->tag & (1 << 11)) ? 'G' : '-',
                    entry->data & CR_TLBACC_PFN_MASK,
                    (entry->data & CR_TLBACC_C) ? 'C' : '-',
                    (entry->data & CR_TLBACC_R) ? 'R' : '-',
                    (entry->data & CR_TLBACC_W) ? 'W' : '-',
                    (entry->data & CR_TLBACC_X) ? 'X' : '-');
    }
}

#endif /* !CONFIG_USER_ONLY */
