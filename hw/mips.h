#ifndef HW_MIPS_H
#define HW_MIPS_H
/* Definitions for mips board emulation.  */

#include "memory.h"

/* gt64xxx.c */
PCIBus *gt64120_register(qemu_irq *pic);

/* bonito.c */
PCIBus *bonito_init(qemu_irq *pic);

/* g364fb.c */
int g364fb_mm_init(MemoryRegion *system_memory, target_phys_addr_t vram_base,
                   target_phys_addr_t ctrl_base, int it_shift,
                   qemu_irq irq);

/* mipsnet.c */
void mipsnet_init(int base, qemu_irq irq, NICInfo *nd);

/* jazz_led.c */
void jazz_led_init(target_phys_addr_t base);

/* rc4030.c */
typedef struct rc4030DMAState *rc4030_dma;
void rc4030_dma_memory_rw(void *opaque, target_phys_addr_t addr, uint8_t *buf, int len, int is_write);
void rc4030_dma_read(void *dma, uint8_t *buf, int len);
void rc4030_dma_write(void *dma, uint8_t *buf, int len);

void *rc4030_init(qemu_irq timer, qemu_irq jazz_bus,
                  qemu_irq **irqs, rc4030_dma **dmas);

/* dp8393x.c */
void dp83932_init(NICInfo *nd, target_phys_addr_t base, int it_shift,
                  qemu_irq irq, void* mem_opaque,
                  void (*memory_rw)(void *opaque, target_phys_addr_t addr, uint8_t *buf, int len, int is_write));

#endif
