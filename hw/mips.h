#ifndef HW_MIPS_H
#define HW_MIPS_H
/* Definitions for mips board emulation.  */

/* gt64xxx.c */
PCIBus *pci_gt64120_init(qemu_irq *pic);

/* ds1225y.c */
typedef struct ds1225y_t ds1225y_t;
ds1225y_t *ds1225y_init(target_phys_addr_t mem_base, const char *filename);

/* mipsnet.c */
void mipsnet_init(int base, qemu_irq irq, NICInfo *nd);

/* jazz_led.c */
extern void jazz_led_init(DisplayState *ds, target_phys_addr_t base);

/* mips_int.c */
extern void cpu_mips_irq_init_cpu(CPUState *env);

/* mips_timer.c */
extern void cpu_mips_clock_init(CPUState *);
extern void cpu_mips_irqctrl_init (void);

#endif
