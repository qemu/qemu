/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch CPU parameters for QEMU.
 *
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_MMU_H
#define LOONGARCH_CPU_MMU_H

enum {
    TLBRET_MATCH = 0,
    TLBRET_BADADDR = 1,
    TLBRET_NOMATCH = 2,
    TLBRET_INVALID = 3,
    TLBRET_DIRTY = 4,
    TLBRET_RI = 5,
    TLBRET_XI = 6,
    TLBRET_PE = 7,
};

bool check_ps(CPULoongArchState *ent, uint8_t ps);
int get_physical_address(CPULoongArchState *env, hwaddr *physical,
                         int *prot, target_ulong address,
                         MMUAccessType access_type, int mmu_idx, int is_debug);
void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                               uint64_t *dir_width, target_ulong level);
hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#endif  /* LOONGARCH_CPU_MMU_H */
