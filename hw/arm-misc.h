/*
 * Misc ARM declarations
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 *
 */

#ifndef ARM_MISC_H
#define ARM_MISC_H 1

/* The CPU is also modeled as an interrupt controller.  */
#define ARM_PIC_CPU_IRQ 0
#define ARM_PIC_CPU_FIQ 1
qemu_irq *arm_pic_init_cpu(CPUState *env);

/* armv7m.c */
qemu_irq *armv7m_init(int flash_size, int sram_size,
                      const char *kernel_filename, const char *cpu_model);

/* arm_boot.c */

void arm_load_kernel(CPUState *env, int ram_size, const char *kernel_filename,
                     const char *kernel_cmdline, const char *initrd_filename,
                     int board_id, target_phys_addr_t loader_start);

/* armv7m_nvic.c */
qemu_irq *armv7m_nvic_init(CPUState *env);

#endif /* !ARM_MISC_H */

