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
    int           tlb_index;
    int           mmu_index;
    uint64_t      pte_buddy[2];
} MMUContext;

static inline bool cpu_has_ptw(CPULoongArchState *env)
{
    return !!FIELD_EX64(env->CSR_PWCH, CSR_PWCH, HPTW_EN);
}

static inline bool pte_present(CPULoongArchState *env, uint64_t entry)
{
    uint8_t present;

    if (cpu_has_ptw(env)) {
        present = FIELD_EX64(entry, TLBENTRY, P);
    } else {
        present = FIELD_EX64(entry, TLBENTRY, V);
    }

    return !!present;
}

static inline bool pte_write(CPULoongArchState *env, uint64_t entry)
{
    uint8_t writable;

    if (cpu_has_ptw(env)) {
        writable = FIELD_EX64(entry, TLBENTRY, W);
    } else {
        writable = FIELD_EX64(entry, TLBENTRY, D);
    }

    return !!writable;
}

/*
 * The folloing functions should be called with PTW enable checked
 * With hardware PTW enabled
 *   Bit D will be set by hardware with write access
 *   Bit A will be set by hardware with read/intruction fetch access
 */
static inline uint64_t pte_mkaccess(uint64_t entry)
{
    return FIELD_DP64(entry, TLBENTRY, V, 1);
}

static inline uint64_t pte_mkdirty(uint64_t entry)
{
    return FIELD_DP64(entry, TLBENTRY, D, 1);
}

static inline bool pte_access(uint64_t entry)
{
    return !!FIELD_EX64(entry, TLBENTRY, V);
}

static inline bool pte_dirty(uint64_t entry)
{
    return !!FIELD_EX64(entry, TLBENTRY, D);
}

bool check_ps(CPULoongArchState *ent, uint8_t ps);
TLBRet loongarch_check_pte(CPULoongArchState *env, MMUContext *context,
                           MMUAccessType access_type, int mmu_idx);
TLBRet get_physical_address(CPULoongArchState *env, MMUContext *context,
                            MMUAccessType access_type, int mmu_idx,
                            int is_debug);
TLBRet loongarch_ptw(CPULoongArchState *env, MMUContext *context,
                     int access_type, int mmu_idx, int debug);
void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                        uint64_t *dir_width, unsigned int level);
hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#endif  /* LOONGARCH_CPU_MMU_H */
