#ifndef HW_MIPS_H
#define HW_MIPS_H
/* Definitions for mips board emulation.  */

/* gt64xxx.c */
PCIBus *pci_gt64120_init(qemu_irq *pic);

/* ds1225y.c */
void *ds1225y_init(target_phys_addr_t mem_base, const char *filename);
void ds1225y_set_protection(void *opaque, int protection);

/* g364fb.c */
int g364fb_mm_init(uint8_t *vram, ram_addr_t vram_offset,
                   int vram_size, target_phys_addr_t vram_base,
                   target_phys_addr_t ctrl_base, int it_shift,
                   qemu_irq irq);

/* mipsnet.c */
void mipsnet_init(int base, qemu_irq irq, NICInfo *nd);

/* jazz_led.c */
extern void jazz_led_init(target_phys_addr_t base);

/* mips_int.c */
extern void cpu_mips_irq_init_cpu(CPUState *env);

/* mips_timer.c */
extern void cpu_mips_clock_init(CPUState *);

/* rc4030.c */
typedef struct rc4030DMAState *rc4030_dma;
typedef void (*rc4030_dma_function)(void *dma, uint8_t *buf, int len);
qemu_irq *rc4030_init(qemu_irq timer, qemu_irq jazz_bus,
                      rc4030_dma **dmas,
                      rc4030_dma_function *dma_read, rc4030_dma_function *dma_write);

#endif
