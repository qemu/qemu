
SPARCCPU *sparc64_cpu_devinit(const char *cpu_model,
                              const char *dflt_cpu_model, uint64_t prom_addr);

void sparc64_cpu_set_ivec_irq(void *opaque, int irq, int level);
