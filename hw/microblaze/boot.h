#ifndef MICROBLAZE_BOOT_H
#define MICROBLAZE_BOOT_H


void microblaze_load_kernel(MicroBlazeCPU *cpu, bool is_little_endian,
                            hwaddr ddr_base, uint32_t ramsize,
                            const char *initrd_filename,
                            const char *dtb_filename,
                            void (*machine_cpu_reset)(MicroBlazeCPU *));

#endif /* MICROBLAZE_BOOT_H */
