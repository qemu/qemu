#ifndef HW_ISA_H
#define HW_ISA_H
/* ISA bus */

extern target_phys_addr_t isa_mem_base;

int register_ioport_read(int start, int length, int size,
                         IOPortReadFunc *func, void *opaque);
int register_ioport_write(int start, int length, int size,
                          IOPortWriteFunc *func, void *opaque);
void isa_unassign_ioport(int start, int length);

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
