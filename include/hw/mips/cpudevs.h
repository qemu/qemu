#ifndef HW_MIPS_CPUDEVS_H
#define HW_MIPS_CPUDEVS_H

#include "target/mips/cpu-qom.h"

/* Definitions for MIPS CPU internal devices.  */

/* addr.c */
uint64_t cpu_mips_kseg0_to_phys(void *opaque, uint64_t addr);
uint64_t cpu_mips_phys_to_kseg0(void *opaque, uint64_t addr);
uint64_t cpu_mips_kvm_um_phys_to_kseg0(void *opaque, uint64_t addr);
bool mips_um_ksegs_enabled(void);
void mips_um_ksegs_enable(void);

/* mips_int.c */
void cpu_mips_irq_init_cpu(MIPSCPU *cpu);

/* mips_timer.c */
void cpu_mips_clock_init(MIPSCPU *cpu);

#endif
