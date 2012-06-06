#ifndef __MICROBLAZE_BOOT__
#define __MICROBLAZE_BOOT__

#include "hw.h"

void microblaze_load_kernel(MicroBlazeCPU *cpu, target_phys_addr_t ddr_base,
                            uint32_t ramsize, const char *dtb_filename,
                            void (*machine_cpu_reset)(MicroBlazeCPU *));

#endif /* __MICROBLAZE_BOOT __ */
