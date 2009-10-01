#ifndef SPARC32_DMA_H
#define SPARC32_DMA_H

/* sparc32_dma.c */
void ledma_memory_read(void *opaque, a_target_phys_addr addr,
                       uint8_t *buf, int len, int do_bswap);
void ledma_memory_write(void *opaque, a_target_phys_addr addr,
                        uint8_t *buf, int len, int do_bswap);
void espdma_memory_read(void *opaque, uint8_t *buf, int len);
void espdma_memory_write(void *opaque, uint8_t *buf, int len);

#endif
