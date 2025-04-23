/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch TCG interface
 *
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */
#ifndef TARGET_LOONGARCH_TCG_LOONGARCH_H
#define TARGET_LOONGARCH_TCG_LOONGARCH_H
#include "cpu.h"

void loongarch_csr_translate_init(void);

bool loongarch_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                            MMUAccessType access_type, int mmu_idx,
                            bool probe, uintptr_t retaddr);

int loongarch_get_addr_from_tlb(CPULoongArchState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                MMUAccessType access_type, int mmu_idx);

#endif  /* TARGET_LOONGARCH_TCG_LOONGARCH_H */
