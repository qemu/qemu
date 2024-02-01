/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU -- internal functions and types
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_INTERNALS_H
#define LOONGARCH_INTERNALS_H

#define FCMP_LT   0b0001  /* fp0 < fp1 */
#define FCMP_EQ   0b0010  /* fp0 = fp1 */
#define FCMP_UN   0b0100  /* unordered */
#define FCMP_GT   0b1000  /* fp0 > fp1 */

#define TARGET_PHYS_MASK MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS)
#define TARGET_VIRT_MASK MAKE_64BIT_MASK(0, TARGET_VIRT_ADDR_SPACE_BITS)

/* Global bit used for lddir/ldpte */
#define LOONGARCH_PAGE_HUGE_SHIFT   6
/* Global bit for huge page */
#define LOONGARCH_HGLOBAL_SHIFT     12

void loongarch_translate_init(void);

void loongarch_cpu_dump_state(CPUState *cpu, FILE *f, int flags);

void G_NORETURN do_raise_exception(CPULoongArchState *env,
                                   uint32_t exception,
                                   uintptr_t pc);

const char *loongarch_exception_name(int32_t exception);

#ifdef CONFIG_TCG
int ieee_ex_to_loongarch(int xcpt);
void restore_fp_status(CPULoongArchState *env);
#endif

#ifndef CONFIG_USER_ONLY
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

extern const VMStateDescription vmstate_loongarch_cpu;

void loongarch_cpu_set_irq(void *opaque, int irq, int level);

void loongarch_constant_timer_cb(void *opaque);
uint64_t cpu_loongarch_get_constant_timer_counter(LoongArchCPU *cpu);
uint64_t cpu_loongarch_get_constant_timer_ticks(LoongArchCPU *cpu);
void cpu_loongarch_store_constant_timer_config(LoongArchCPU *cpu,
                                               uint64_t value);
bool loongarch_tlb_search(CPULoongArchState *env, target_ulong vaddr,
                          int *index);
int get_physical_address(CPULoongArchState *env, hwaddr *physical,
                         int *prot, target_ulong address,
                         MMUAccessType access_type, int mmu_idx);
hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#ifdef CONFIG_TCG
bool loongarch_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                            MMUAccessType access_type, int mmu_idx,
                            bool probe, uintptr_t retaddr);
#endif
#endif /* !CONFIG_USER_ONLY */

uint64_t read_fcc(CPULoongArchState *env);
void write_fcc(CPULoongArchState *env, uint64_t val);

int loongarch_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n);
int loongarch_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);
void loongarch_cpu_register_gdb_regs_for_features(CPUState *cs);

#endif
