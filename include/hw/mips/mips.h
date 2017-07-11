#ifndef HW_MIPS_H
#define HW_MIPS_H
/* Definitions for mips board emulation.  */

/* Kernels can be configured with 64KB pages */
#define INITRD_PAGE_MASK (~((1 << 16) - 1))

#include "exec/memory.h"
#include "hw/irq.h"

/* gt64xxx.c */
PCIBus *gt64120_register(qemu_irq *pic);

/* bonito.c */
PCIBus *bonito_init(qemu_irq *pic);

/* rc4030.c */
typedef struct rc4030DMAState *rc4030_dma;
void rc4030_dma_read(void *dma, uint8_t *buf, int len);
void rc4030_dma_write(void *dma, uint8_t *buf, int len);

DeviceState *rc4030_init(rc4030_dma **dmas, IOMMUMemoryRegion **dma_mr);

#endif
