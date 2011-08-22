#ifndef MICROBLAZE_PIC_CPU_H
#define MICROBLAZE_PIC_CPU_H

#include "qemu-common.h"

qemu_irq *microblaze_pic_init_cpu(CPUState *env);

#endif /*  MICROBLAZE_PIC_CPU_H */
