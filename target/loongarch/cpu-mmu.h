/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch CPU parameters for QEMU.
 *
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_MMU_H
#define LOONGARCH_CPU_MMU_H

typedef enum TLBRet {
    TLBRET_MATCH,
    TLBRET_BADADDR,
    TLBRET_NOMATCH,
    TLBRET_INVALID,
    TLBRET_DIRTY,
    TLBRET_RI,
    TLBRET_XI,
    TLBRET_PE,
} TLBRet;

typedef struct MMUContext {
    vaddr         addr;
    uint64_t      pte;
    hwaddr        physical;
    int           ps;  /* page size shift */
    int           prot;
} MMUContext;

bool check_ps(CPULoongArchState *ent, uint8_t ps);
TLBRet loongarch_check_pte(CPULoongArchState *env, MMUContext *context,
                           MMUAccessType access_type, int mmu_idx);
TLBRet get_physical_address(CPULoongArchState *env, MMUContext *context,
                            MMUAccessType access_type, int mmu_idx,
                            int is_debug);
void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                        uint64_t *dir_width, unsigned int level);
hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#endif  /* LOONGARCH_CPU_MMU_H */
