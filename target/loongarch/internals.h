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

void loongarch_translate_init(void);

void loongarch_cpu_dump_state(CPUState *cpu, FILE *f, int flags);

void G_NORETURN do_raise_exception(CPULoongArchState *env,
                                   uint32_t exception,
                                   uintptr_t pc);

const char *loongarch_exception_name(int32_t exception);

void restore_fp_status(CPULoongArchState *env);

extern const VMStateDescription vmstate_loongarch_cpu;

bool loongarch_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                            MMUAccessType access_type, int mmu_idx,
                            bool probe, uintptr_t retaddr);

hwaddr loongarch_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#endif
