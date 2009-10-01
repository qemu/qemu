#ifndef HW_MCF_H
#define HW_MCF_H
/* Motorola ColdFire device prototypes.  */

/* mcf_uart.c */
uint32_t mcf_uart_read(void *opaque, a_target_phys_addr addr);
void mcf_uart_write(void *opaque, a_target_phys_addr addr, uint32_t val);
void *mcf_uart_init(qemu_irq irq, CharDriverState *chr);
void mcf_uart_mm_init(a_target_phys_addr base, qemu_irq irq,
                      CharDriverState *chr);

/* mcf_intc.c */
qemu_irq *mcf_intc_init(a_target_phys_addr base, CPUState *env);

/* mcf_fec.c */
void mcf_fec_init(NICInfo *nd, a_target_phys_addr base, qemu_irq *irq);

/* mcf5206.c */
qemu_irq *mcf5206_init(uint32_t base, CPUState *env);

#endif
