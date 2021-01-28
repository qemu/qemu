#ifndef HW_MIPS_CPUDEVS_H
#define HW_MIPS_CPUDEVS_H

#include "target/mips/cpu-qom.h"

/* Definitions for MIPS CPU internal devices.  */

/* mips_int.c */
void cpu_mips_irq_init_cpu(MIPSCPU *cpu);

/* mips_timer.c */
void cpu_mips_clock_init(MIPSCPU *cpu);

#endif
