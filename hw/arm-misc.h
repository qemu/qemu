/*
 * Misc ARM declarations
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 *
 */

#ifndef ARM_MISC_H
#define ARM_MISC_H 1

#include "memory.h"
#include "hw/irq.h"

/* The CPU is also modeled as an interrupt controller.  */
#define ARM_PIC_CPU_IRQ 0
#define ARM_PIC_CPU_FIQ 1
qemu_irq *arm_pic_init_cpu(ARMCPU *cpu);

/* armv7m.c */
qemu_irq *armv7m_init(MemoryRegion *address_space_mem,
                      int flash_size, int sram_size,
                      const char *kernel_filename, const char *cpu_model);

/* arm_boot.c */
struct arm_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    const char *kernel_cmdline;
    const char *initrd_filename;
    const char *dtb_filename;
    hwaddr loader_start;
    /* multicore boards that use the default secondary core boot functions
     * need to put the address of the secondary boot code, the boot reg,
     * and the GIC address in the next 3 values, respectively. boards that
     * have their own boot functions can use these values as they want.
     */
    hwaddr smp_loader_start;
    hwaddr smp_bootreg_addr;
    hwaddr gic_cpu_if_addr;
    int nb_cpus;
    int board_id;
    int (*atag_board)(const struct arm_boot_info *info, void *p);
    /* multicore boards that use the default secondary core boot functions
     * can ignore these two function calls. If the default functions won't
     * work, then write_secondary_boot() should write a suitable blob of
     * code mimicking the secondary CPU startup process used by the board's
     * boot loader/boot ROM code, and secondary_cpu_reset_hook() should
     * perform any necessary CPU reset handling and set the PC for the
     * secondary CPUs to point at this boot blob.
     */
    void (*write_secondary_boot)(ARMCPU *cpu,
                                 const struct arm_boot_info *info);
    void (*secondary_cpu_reset_hook)(ARMCPU *cpu,
                                     const struct arm_boot_info *info);
    /* Used internally by arm_boot.c */
    int is_linux;
    hwaddr initrd_start;
    hwaddr initrd_size;
    hwaddr entry;
};
void arm_load_kernel(ARMCPU *cpu, struct arm_boot_info *info);

/* Multiplication factor to convert from system clock ticks to qemu timer
   ticks.  */
extern int system_clock_scale;

#endif /* !ARM_MISC_H */
