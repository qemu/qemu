#ifndef HW_MIPS_H
#define HW_MIPS_H
/* Definitions for mips board emulation.  */

/* gt64xxx.c */
PCIBus *pci_gt64120_init(qemu_irq *pic);

/* ds1225y.c */
void *ds1225y_init(target_phys_addr_t mem_base, const char *filename);
void ds1225y_set_protection(void *opaque, int protection);

/* g364fb.c */
int g364fb_mm_init(DisplayState *ds,
                   int vram_size, int it_shift,
                   target_phys_addr_t vram_base, target_phys_addr_t ctrl_base);

/* mipsnet.c */
void mipsnet_init(int base, qemu_irq irq, NICInfo *nd);

/* jazz_led.c */
extern void jazz_led_init(DisplayState *ds, target_phys_addr_t base);

/* mips_int.c */
extern void cpu_mips_irq_init_cpu(CPUState *env);

/* mips_timer.c */
extern void cpu_mips_clock_init(CPUState *);

/* rc4030.c */
qemu_irq *rc4030_init(qemu_irq timer, qemu_irq jazz_bus);

#endif
