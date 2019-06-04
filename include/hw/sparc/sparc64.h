#ifndef HW_SPARC_SPARC64_H
#define HW_SPARC_SPARC64_H

#define IVEC_MAX             0x40

SPARCCPU *sparc64_cpu_devinit(const char *cpu_type, uint64_t prom_addr);

void sparc64_cpu_set_ivec_irq(void *opaque, int irq, int level);

#endif
