#ifndef HW_MAC128K_H
#define HW_MAC128K_H

struct MemoryRegion;

/* iwm.c */
void iwm_init(MemoryRegion *sysmem, uint32_t base, M68kCPU *cpu);

#endif
