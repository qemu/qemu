#ifndef QEMU_SHARPSL_H
#define QEMU_SHARPSL_H

/* zaurus.c */
struct scoop_info_s *scoop_init(struct pxa2xx_state_s *cpu,
                int instance, target_phys_addr_t target_base);
void scoop_gpio_set(void *opaque, int line, int level);
qemu_irq *scoop_gpio_in_get(struct scoop_info_s *s);
void scoop_gpio_out_set(struct scoop_info_s *s, int line,
                qemu_irq handler);

#define SL_PXA_PARAM_BASE	0xa0000a00
void sl_bootparam_write(uint32_t ptr);

#endif
