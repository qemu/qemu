#ifndef SPARC32_DMA_H
#define SPARC32_DMA_H

#include "hw/sysbus.h"
#include "hw/scsi/esp.h"
#include "hw/net/lance.h"

#define DMA_REGS 4

#define TYPE_SPARC32_DMA_DEVICE "sparc32-dma-device"
#define SPARC32_DMA_DEVICE(obj) OBJECT_CHECK(DMADeviceState, (obj), \
                                             TYPE_SPARC32_DMA_DEVICE)

typedef struct DMADeviceState DMADeviceState;

struct DMADeviceState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t dmaregs[DMA_REGS];
    qemu_irq irq;
    void *iommu;
    qemu_irq gpio[2];
};

#define TYPE_SPARC32_ESPDMA_DEVICE "sparc32-espdma"
#define SPARC32_ESPDMA_DEVICE(obj) OBJECT_CHECK(ESPDMADeviceState, (obj), \
                                                TYPE_SPARC32_ESPDMA_DEVICE)

typedef struct ESPDMADeviceState {
    DMADeviceState parent_obj;

    SysBusESPState *esp;
} ESPDMADeviceState;

#define TYPE_SPARC32_LEDMA_DEVICE "sparc32-ledma"
#define SPARC32_LEDMA_DEVICE(obj) OBJECT_CHECK(LEDMADeviceState, (obj), \
                                               TYPE_SPARC32_LEDMA_DEVICE)

typedef struct LEDMADeviceState {
    DMADeviceState parent_obj;

    SysBusPCNetState *lance;
} LEDMADeviceState;

#define TYPE_SPARC32_DMA "sparc32-dma"
#define SPARC32_DMA(obj) OBJECT_CHECK(SPARC32DMAState, (obj), \
                                      TYPE_SPARC32_DMA)

typedef struct SPARC32DMAState {
    SysBusDevice parent_obj;

    MemoryRegion dmamem;
    MemoryRegion ledma_alias;
    ESPDMADeviceState *espdma;
    LEDMADeviceState *ledma;
} SPARC32DMAState;

/* sparc32_dma.c */
void ledma_memory_read(void *opaque, hwaddr addr,
                       uint8_t *buf, int len, int do_bswap);
void ledma_memory_write(void *opaque, hwaddr addr,
                        uint8_t *buf, int len, int do_bswap);
void espdma_memory_read(void *opaque, uint8_t *buf, int len);
void espdma_memory_write(void *opaque, uint8_t *buf, int len);

#endif
