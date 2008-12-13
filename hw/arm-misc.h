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
struct arm_boot_info {
    int ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    target_phys_addr_t loader_start;
    int nb_cpus;
    int board_id;
    int (*atag_board)(struct arm_boot_info *info, void *p);
};
void arm_load_kernel(CPUState *env, struct arm_boot_info *info);

/* armv7m_nvic.c */

/* Multiplication factor to convert from system clock ticks to qemu timer
   ticks.  */
extern int system_clock_scale;
qemu_irq *armv7m_nvic_init(CPUState *env);

/* stellaris_enent.c */
void stellaris_enet_init(NICInfo *nd, uint32_t base, qemu_irq irq);

#endif /* !ARM_MISC_H */
