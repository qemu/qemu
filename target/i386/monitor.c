/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#include "qapi/qmp/qdict.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qapi-commands-misc.h"
#include "hw/i386/pc.h"

/* Perform linear address sign extension */
static hwaddr addr_canonical(CPUArchState *env, hwaddr addr)
{
#ifdef TARGET_X86_64
    if (env->cr[4] & CR4_LA57_MASK) {
        if (addr & (1ULL << 56)) {
            addr |= (hwaddr)-(1LL << 57);
        }
    } else {
        if (addr & (1ULL << 47)) {
            addr |= (hwaddr)-(1LL << 48);
        }
    }
#endif
    return addr;
}

static void print_pte(Monitor *mon, CPUArchState *env, hwaddr addr,
                      hwaddr pte, hwaddr mask)
{
    addr = addr_canonical(env, addr);

    monitor_printf(mon, HWADDR_FMT_plx ": " HWADDR_FMT_plx
                   " %c%c%c%c%c%c%c%c%c\n",
                   addr,
                   pte & mask,
                   pte & PG_NX_MASK ? 'X' : '-',
                   pte & PG_GLOBAL_MASK ? 'G' : '-',
                   pte & PG_PSE_MASK ? 'P' : '-',
                   pte & PG_DIRTY_MASK ? 'D' : '-',
                   pte & PG_ACCESSED_MASK ? 'A' : '-',
                   pte & PG_PCD_MASK ? 'C' : '-',
                   pte & PG_PWT_MASK ? 'T' : '-',
                   pte & PG_USER_MASK ? 'U' : '-',
                   pte & PG_RW_MASK ? 'W' : '-');
}

static void tlb_info_32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2;
    uint32_t pgd, pde, pte;

    pgd = env->cr[3] & ~0xfff;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, &pde, 4);
        pde = le32_to_cpu(pde);
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                /* 4M pages */
                print_pte(mon, env, (l1 << 22), pde, ~((1 << 21) - 1));
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4, &pte, 4);
                    pte = le32_to_cpu(pte);
                    if (pte & PG_PRESENT_MASK) {
                        print_pte(mon, env, (l1 << 22) + (l2 << 12),
                                  pte & ~PG_PSE_MASK,
                                  ~0xfff);
                    }
                }
            }
        }
    }
}

static void tlb_info_pae32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2, l3;
    uint64_t pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;

    pdp_addr = env->cr[3] & ~0x1f;
    for (l1 = 0; l1 < 4; l1++) {
        cpu_physical_memory_read(pdp_addr + l1 * 8, &pdpe, 8);
        pdpe = le64_to_cpu(pdpe);
        if (pdpe & PG_PRESENT_MASK) {
            pd_addr = pdpe & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pd_addr + l2 * 8, &pde, 8);
                pde = le64_to_cpu(pde);
                if (pde & PG_PRESENT_MASK) {
                    if (pde & PG_PSE_MASK) {
                        /* 2M pages with PAE, CR4.PSE is ignored */
                        print_pte(mon, env, (l1 << 30) + (l2 << 21), pde,
                                  ~((hwaddr)(1 << 20) - 1));
                    } else {
                        pt_addr = pde & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pt_addr + l3 * 8, &pte, 8);
                            pte = le64_to_cpu(pte);
                            if (pte & PG_PRESENT_MASK) {
                                print_pte(mon, env, (l1 << 30) + (l2 << 21)
                                          + (l3 << 12),
                                          pte & ~PG_PSE_MASK,
                                          ~(hwaddr)0xfff);
                            }
                        }
                    }
                }
            }
        }
    }
}

#ifdef TARGET_X86_64
static void tlb_info_la48(Monitor *mon, CPUArchState *env,
        uint64_t l0, uint64_t pml4_addr)
{
    uint64_t l1, l2, l3, l4;
    uint64_t pml4e, pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;

    for (l1 = 0; l1 < 512; l1++) {
        cpu_physical_memory_read(pml4_addr + l1 * 8, &pml4e, 8);
        pml4e = le64_to_cpu(pml4e);
        if (!(pml4e & PG_PRESENT_MASK)) {
            continue;
        }

        pdp_addr = pml4e & 0x3fffffffff000ULL;
        for (l2 = 0; l2 < 512; l2++) {
            cpu_physical_memory_read(pdp_addr + l2 * 8, &pdpe, 8);
            pdpe = le64_to_cpu(pdpe);
            if (!(pdpe & PG_PRESENT_MASK)) {
                continue;
            }

            if (pdpe & PG_PSE_MASK) {
                /* 1G pages, CR4.PSE is ignored */
                print_pte(mon, env, (l0 << 48) + (l1 << 39) + (l2 << 30),
                        pdpe, 0x3ffffc0000000ULL);
                continue;
            }

            pd_addr = pdpe & 0x3fffffffff000ULL;
            for (l3 = 0; l3 < 512; l3++) {
                cpu_physical_memory_read(pd_addr + l3 * 8, &pde, 8);
                pde = le64_to_cpu(pde);
                if (!(pde & PG_PRESENT_MASK)) {
                    continue;
                }

                if (pde & PG_PSE_MASK) {
                    /* 2M pages, CR4.PSE is ignored */
                    print_pte(mon, env, (l0 << 48) + (l1 << 39) + (l2 << 30) +
                            (l3 << 21), pde, 0x3ffffffe00000ULL);
                    continue;
                }

                pt_addr = pde & 0x3fffffffff000ULL;
                for (l4 = 0; l4 < 512; l4++) {
                    cpu_physical_memory_read(pt_addr
                            + l4 * 8,
                            &pte, 8);
                    pte = le64_to_cpu(pte);
                    if (pte & PG_PRESENT_MASK) {
                        print_pte(mon, env, (l0 << 48) + (l1 << 39) +
                                (l2 << 30) + (l3 << 21) + (l4 << 12),
                                pte & ~PG_PSE_MASK, 0x3fffffffff000ULL);
                    }
                }
            }
        }
    }
}

static void tlb_info_la57(Monitor *mon, CPUArchState *env)
{
    uint64_t l0;
    uint64_t pml5e;
    uint64_t pml5_addr;

    pml5_addr = env->cr[3] & 0x3fffffffff000ULL;
    for (l0 = 0; l0 < 512; l0++) {
        cpu_physical_memory_read(pml5_addr + l0 * 8, &pml5e, 8);
        pml5e = le64_to_cpu(pml5e);
        if (pml5e & PG_PRESENT_MASK) {
            tlb_info_la48(mon, env, l0, pml5e & 0x3fffffffff000ULL);
        }
    }
}
#endif /* TARGET_X86_64 */

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return;
    }
    if (env->cr[4] & CR4_PAE_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            if (env->cr[4] & CR4_LA57_MASK) {
                tlb_info_la57(mon, env);
            } else {
                tlb_info_la48(mon, env, 0, env->cr[3] & 0x3fffffffff000ULL);
            }
        } else
#endif
        {
            tlb_info_pae32(mon, env);
        }
    } else {
        tlb_info_32(mon, env);
    }
}

static void mem_print(Monitor *mon, CPUArchState *env,
                      hwaddr *pstart, int *plast_prot,
                      hwaddr end, int prot)
{
    int prot1;
    prot1 = *plast_prot;
    if (prot != prot1) {
        if (*pstart != -1) {
            monitor_printf(mon, HWADDR_FMT_plx "-" HWADDR_FMT_plx " "
                           HWADDR_FMT_plx " %c%c%c\n",
                           addr_canonical(env, *pstart),
                           addr_canonical(env, end),
                           addr_canonical(env, end - *pstart),
                           prot1 & PG_USER_MASK ? 'u' : '-',
                           'r',
                           prot1 & PG_RW_MASK ? 'w' : '-');
        }
        if (prot != 0)
            *pstart = end;
        else
            *pstart = -1;
        *plast_prot = prot;
    }
}

static void mem_info_32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2;
    int prot, last_prot;
    uint32_t pgd, pde, pte;
    hwaddr start, end;

    pgd = env->cr[3] & ~0xfff;
    last_prot = 0;
    start = -1;
    for(l1 = 0; l1 < 1024; l1++) {
        cpu_physical_memory_read(pgd + l1 * 4, &pde, 4);
        pde = le32_to_cpu(pde);
        end = l1 << 22;
        if (pde & PG_PRESENT_MASK) {
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                prot = pde & (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                mem_print(mon, env, &start, &last_prot, end, prot);
            } else {
                for(l2 = 0; l2 < 1024; l2++) {
                    cpu_physical_memory_read((pde & ~0xfff) + l2 * 4, &pte, 4);
                    pte = le32_to_cpu(pte);
                    end = (l1 << 22) + (l2 << 12);
                    if (pte & PG_PRESENT_MASK) {
                        prot = pte & pde &
                            (PG_USER_MASK | PG_RW_MASK | PG_PRESENT_MASK);
                    } else {
                        prot = 0;
                    }
                    mem_print(mon, env, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_print(mon, env, &start, &last_prot, end, prot);
        }
    }
    /* Flush last range */
    mem_print(mon, env, &start, &last_prot, (hwaddr)1 << 32, 0);
}

static void mem_info_pae32(Monitor *mon, CPUArchState *env)
{
    unsigned int l1, l2, l3;
    int prot, last_prot;
    uint64_t pdpe, pde, pte;
    uint64_t pdp_addr, pd_addr, pt_addr;
    hwaddr start, end;

    pdp_addr = env->cr[3] & ~0x1f;
    last_prot = 0;
    start = -1;
    for (l1 = 0; l1 < 4; l1++) {
        cpu_physical_memory_read(pdp_addr + l1 * 8, &pdpe, 8);
        pdpe = le64_to_cpu(pdpe);
        end = l1 << 30;
        if (pdpe & PG_PRESENT_MASK) {
            pd_addr = pdpe & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pd_addr + l2 * 8, &pde, 8);
                pde = le64_to_cpu(pde);
                end = (l1 << 30) + (l2 << 21);
                if (pde & PG_PRESENT_MASK) {
                    if (pde & PG_PSE_MASK) {
                        prot = pde & (PG_USER_MASK | PG_RW_MASK |
                                      PG_PRESENT_MASK);
                        mem_print(mon, env, &start, &last_prot, end, prot);
                    } else {
                        pt_addr = pde & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pt_addr + l3 * 8, &pte, 8);
                            pte = le64_to_cpu(pte);
                            end = (l1 << 30) + (l2 << 21) + (l3 << 12);
                            if (pte & PG_PRESENT_MASK) {
                                prot = pte & pde & (PG_USER_MASK | PG_RW_MASK |
                                                    PG_PRESENT_MASK);
                            } else {
                                prot = 0;
                            }
                            mem_print(mon, env, &start, &last_prot, end, prot);
                        }
                    }
                } else {
                    prot = 0;
                    mem_print(mon, env, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_print(mon, env, &start, &last_prot, end, prot);
        }
    }
    /* Flush last range */
    mem_print(mon, env, &start, &last_prot, (hwaddr)1 << 32, 0);
}


#ifdef TARGET_X86_64
static void mem_info_la48(Monitor *mon, CPUArchState *env)
{
    int prot, last_prot;
    uint64_t l1, l2, l3, l4;
    uint64_t pml4e, pdpe, pde, pte;
    uint64_t pml4_addr, pdp_addr, pd_addr, pt_addr, start, end;

    pml4_addr = env->cr[3] & 0x3fffffffff000ULL;
    last_prot = 0;
    start = -1;
    for (l1 = 0; l1 < 512; l1++) {
        cpu_physical_memory_read(pml4_addr + l1 * 8, &pml4e, 8);
        pml4e = le64_to_cpu(pml4e);
        end = l1 << 39;
        if (pml4e & PG_PRESENT_MASK) {
            pdp_addr = pml4e & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pdp_addr + l2 * 8, &pdpe, 8);
                pdpe = le64_to_cpu(pdpe);
                end = (l1 << 39) + (l2 << 30);
                if (pdpe & PG_PRESENT_MASK) {
                    if (pdpe & PG_PSE_MASK) {
                        prot = pdpe & (PG_USER_MASK | PG_RW_MASK |
                                       PG_PRESENT_MASK);
                        prot &= pml4e;
                        mem_print(mon, env, &start, &last_prot, end, prot);
                    } else {
                        pd_addr = pdpe & 0x3fffffffff000ULL;
                        for (l3 = 0; l3 < 512; l3++) {
                            cpu_physical_memory_read(pd_addr + l3 * 8, &pde, 8);
                            pde = le64_to_cpu(pde);
                            end = (l1 << 39) + (l2 << 30) + (l3 << 21);
                            if (pde & PG_PRESENT_MASK) {
                                if (pde & PG_PSE_MASK) {
                                    prot = pde & (PG_USER_MASK | PG_RW_MASK |
                                                  PG_PRESENT_MASK);
                                    prot &= pml4e & pdpe;
                                    mem_print(mon, env, &start,
                                              &last_prot, end, prot);
                                } else {
                                    pt_addr = pde & 0x3fffffffff000ULL;
                                    for (l4 = 0; l4 < 512; l4++) {
                                        cpu_physical_memory_read(pt_addr
                                                                 + l4 * 8,
                                                                 &pte, 8);
                                        pte = le64_to_cpu(pte);
                                        end = (l1 << 39) + (l2 << 30) +
                                            (l3 << 21) + (l4 << 12);
                                        if (pte & PG_PRESENT_MASK) {
                                            prot = pte & (PG_USER_MASK | PG_RW_MASK |
                                                          PG_PRESENT_MASK);
                                            prot &= pml4e & pdpe & pde;
                                        } else {
                                            prot = 0;
                                        }
                                        mem_print(mon, env, &start,
                                                  &last_prot, end, prot);
                                    }
                                }
                            } else {
                                prot = 0;
                                mem_print(mon, env, &start,
                                          &last_prot, end, prot);
                            }
                        }
                    }
                } else {
                    prot = 0;
                    mem_print(mon, env, &start, &last_prot, end, prot);
                }
            }
        } else {
            prot = 0;
            mem_print(mon, env, &start, &last_prot, end, prot);
        }
    }
    /* Flush last range */
    mem_print(mon, env, &start, &last_prot, (hwaddr)1 << 48, 0);
}

static void mem_info_la57(Monitor *mon, CPUArchState *env)
{
    int prot, last_prot;
    uint64_t l0, l1, l2, l3, l4;
    uint64_t pml5e, pml4e, pdpe, pde, pte;
    uint64_t pml5_addr, pml4_addr, pdp_addr, pd_addr, pt_addr, start, end;

    pml5_addr = env->cr[3] & 0x3fffffffff000ULL;
    last_prot = 0;
    start = -1;
    for (l0 = 0; l0 < 512; l0++) {
        cpu_physical_memory_read(pml5_addr + l0 * 8, &pml5e, 8);
        pml5e = le64_to_cpu(pml5e);
        end = l0 << 48;
        if (!(pml5e & PG_PRESENT_MASK)) {
            prot = 0;
            mem_print(mon, env, &start, &last_prot, end, prot);
            continue;
        }

        pml4_addr = pml5e & 0x3fffffffff000ULL;
        for (l1 = 0; l1 < 512; l1++) {
            cpu_physical_memory_read(pml4_addr + l1 * 8, &pml4e, 8);
            pml4e = le64_to_cpu(pml4e);
            end = (l0 << 48) + (l1 << 39);
            if (!(pml4e & PG_PRESENT_MASK)) {
                prot = 0;
                mem_print(mon, env, &start, &last_prot, end, prot);
                continue;
            }

            pdp_addr = pml4e & 0x3fffffffff000ULL;
            for (l2 = 0; l2 < 512; l2++) {
                cpu_physical_memory_read(pdp_addr + l2 * 8, &pdpe, 8);
                pdpe = le64_to_cpu(pdpe);
                end = (l0 << 48) + (l1 << 39) + (l2 << 30);
                if (pdpe & PG_PRESENT_MASK) {
                    prot = 0;
                    mem_print(mon, env, &start, &last_prot, end, prot);
                    continue;
                }

                if (pdpe & PG_PSE_MASK) {
                    prot = pdpe & (PG_USER_MASK | PG_RW_MASK |
                            PG_PRESENT_MASK);
                    prot &= pml5e & pml4e;
                    mem_print(mon, env, &start, &last_prot, end, prot);
                    continue;
                }

                pd_addr = pdpe & 0x3fffffffff000ULL;
                for (l3 = 0; l3 < 512; l3++) {
                    cpu_physical_memory_read(pd_addr + l3 * 8, &pde, 8);
                    pde = le64_to_cpu(pde);
                    end = (l0 << 48) + (l1 << 39) + (l2 << 30) + (l3 << 21);
                    if (pde & PG_PRESENT_MASK) {
                        prot = 0;
                        mem_print(mon, env, &start, &last_prot, end, prot);
                        continue;
                    }

                    if (pde & PG_PSE_MASK) {
                        prot = pde & (PG_USER_MASK | PG_RW_MASK |
                                PG_PRESENT_MASK);
                        prot &= pml5e & pml4e & pdpe;
                        mem_print(mon, env, &start, &last_prot, end, prot);
                        continue;
                    }

                    pt_addr = pde & 0x3fffffffff000ULL;
                    for (l4 = 0; l4 < 512; l4++) {
                        cpu_physical_memory_read(pt_addr + l4 * 8, &pte, 8);
                        pte = le64_to_cpu(pte);
                        end = (l0 << 48) + (l1 << 39) + (l2 << 30) +
                            (l3 << 21) + (l4 << 12);
                        if (pte & PG_PRESENT_MASK) {
                            prot = pte & (PG_USER_MASK | PG_RW_MASK |
                                    PG_PRESENT_MASK);
                            prot &= pml5e & pml4e & pdpe & pde;
                        } else {
                            prot = 0;
                        }
                        mem_print(mon, env, &start, &last_prot, end, prot);
                    }
                }
            }
        }
    }
    /* Flush last range */
    mem_print(mon, env, &start, &last_prot, (hwaddr)1 << 57, 0);
}
#endif /* TARGET_X86_64 */

void hmp_info_mem(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (!(env->cr[0] & CR0_PG_MASK)) {
        monitor_printf(mon, "PG disabled\n");
        return;
    }
    if (env->cr[4] & CR4_PAE_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            if (env->cr[4] & CR4_LA57_MASK) {
                mem_info_la57(mon, env);
            } else {
                mem_info_la48(mon, env);
            }
        } else
#endif
        {
            mem_info_pae32(mon, env);
        }
    } else {
        mem_info_32(mon, env);
    }
}

void hmp_mce(Monitor *mon, const QDict *qdict)
{
    X86CPU *cpu;
    CPUState *cs;
    int cpu_index = qdict_get_int(qdict, "cpu_index");
    int bank = qdict_get_int(qdict, "bank");
    uint64_t status = qdict_get_int(qdict, "status");
    uint64_t mcg_status = qdict_get_int(qdict, "mcg_status");
    uint64_t addr = qdict_get_int(qdict, "addr");
    uint64_t misc = qdict_get_int(qdict, "misc");
    int flags = MCE_INJECT_UNCOND_AO;

    if (qdict_get_try_bool(qdict, "broadcast", false)) {
        flags |= MCE_INJECT_BROADCAST;
    }
    cs = qemu_get_cpu(cpu_index);
    if (cs != NULL) {
        cpu = X86_CPU(cs);
        cpu_x86_inject_mce(mon, cpu, bank, status, mcg_status, addr, misc,
                           flags);
    }
}

static target_long monitor_get_pc(Monitor *mon, const struct MonitorDef *md,
                                  int val)
{
    CPUArchState *env = mon_get_cpu_env(mon);
    return env->eip + env->segs[R_CS].base;
}

const MonitorDef monitor_defs[] = {
#define SEG(name, seg) \
    { name, offsetof(CPUX86State, segs[seg].selector), NULL, MD_I32 },\
    { name ".base", offsetof(CPUX86State, segs[seg].base) },\
    { name ".limit", offsetof(CPUX86State, segs[seg].limit), NULL, MD_I32 },

    { "eax", offsetof(CPUX86State, regs[0]) },
    { "ecx", offsetof(CPUX86State, regs[1]) },
    { "edx", offsetof(CPUX86State, regs[2]) },
    { "ebx", offsetof(CPUX86State, regs[3]) },
    { "esp|sp", offsetof(CPUX86State, regs[4]) },
    { "ebp|fp", offsetof(CPUX86State, regs[5]) },
    { "esi", offsetof(CPUX86State, regs[6]) },
    { "edi", offsetof(CPUX86State, regs[7]) },
#ifdef TARGET_X86_64
    { "r8", offsetof(CPUX86State, regs[8]) },
    { "r9", offsetof(CPUX86State, regs[9]) },
    { "r10", offsetof(CPUX86State, regs[10]) },
    { "r11", offsetof(CPUX86State, regs[11]) },
    { "r12", offsetof(CPUX86State, regs[12]) },
    { "r13", offsetof(CPUX86State, regs[13]) },
    { "r14", offsetof(CPUX86State, regs[14]) },
    { "r15", offsetof(CPUX86State, regs[15]) },
#endif
    { "eflags", offsetof(CPUX86State, eflags) },
    { "eip", offsetof(CPUX86State, eip) },
    SEG("cs", R_CS)
    SEG("ds", R_DS)
    SEG("es", R_ES)
    SEG("ss", R_SS)
    SEG("fs", R_FS)
    SEG("gs", R_GS)
    { "pc", 0, monitor_get_pc, },
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}

void hmp_info_local_apic(Monitor *mon, const QDict *qdict)
{
    CPUState *cs;

    if (qdict_haskey(qdict, "apic-id")) {
        int id = qdict_get_try_int(qdict, "apic-id", 0);

        cs = cpu_by_arch_id(id);
        if (cs) {
            cpu_synchronize_state(cs);
        }
    } else {
        cs = mon_get_cpu(mon);
    }


    if (!cs) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    x86_cpu_dump_local_apic_state(cs, CPU_DUMP_FPU);
}
