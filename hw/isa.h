#ifndef HW_ISA_H
#define HW_ISA_H
/* ISA bus */

#include "ioport.h"

extern target_phys_addr_t isa_mem_base;

void isa_mmio_init(target_phys_addr_t base, target_phys_addr_t size);

/* dma.c */
int DMA_get_channel_mode (int nchan);
int DMA_read_memory (int nchan, void *buf, int pos, int size);
int DMA_write_memory (int nchan, void *buf, int pos, int size);
void DMA_hold_DREQ (int nchan);
void DMA_release_DREQ (int nchan);
void DMA_schedule(int nchan);
void DMA_init (int high_page_enable);
void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler,
                           void *opaque);
#endif
