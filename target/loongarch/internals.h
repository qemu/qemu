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
void loongarch_translate_code(CPUState *cs, TranslationBlock *tb,
                              int *max_insns, vaddr pc, void *host_pc);

void G_NORETURN do_raise_exception(CPULoongArchState *env,
                                   uint32_t exception,
                                   uintptr_t pc);

#ifdef CONFIG_TCG
int ieee_ex_to_loongarch(int xcpt);
void restore_fp_status(CPULoongArchState *env);
#endif

#ifndef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_loongarch_cpu;

void loongarch_cpu_set_irq(void *opaque, int irq, int level);

void loongarch_constant_timer_cb(void *opaque);
uint64_t cpu_loongarch_get_constant_timer_counter(LoongArchCPU *cpu);
uint64_t cpu_loongarch_get_constant_timer_ticks(LoongArchCPU *cpu);
void cpu_loongarch_store_constant_timer_config(LoongArchCPU *cpu,
                                               uint64_t value);
bool loongarch_cpu_has_work(CPUState *cs);
bool cpu_loongarch_hw_interrupts_pending(CPULoongArchState *env);
#endif /* !CONFIG_USER_ONLY */

uint64_t read_fcc(CPULoongArchState *env);
void write_fcc(CPULoongArchState *env, uint64_t val);

int loongarch_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n);
int loongarch_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);
void loongarch_cpu_register_gdb_regs_for_features(CPUState *cs);
int loongarch_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                                   int cpuid, DumpState *s);

#endif
