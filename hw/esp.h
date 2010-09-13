#ifndef QEMU_HW_ESP_H
#define QEMU_HW_ESP_H

/* esp.c */
#define ESP_MAX_DEVS 7
typedef void (*ESPDMAMemoryReadWriteFunc)(void *opaque, uint8_t *buf, int len);
void esp_init(target_phys_addr_t espaddr, int it_shift,
              ESPDMAMemoryReadWriteFunc dma_memory_read,
              ESPDMAMemoryReadWriteFunc dma_memory_write,
              void *dma_opaque, qemu_irq irq, qemu_irq *reset,
              qemu_irq *dma_enable);

#endif
