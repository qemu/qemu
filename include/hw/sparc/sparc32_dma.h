#ifndef SPARC32_DMA_H
#define SPARC32_DMA_H

#include "hw/sysbus.h"
#include "hw/scsi/esp.h"
#include "hw/net/lance.h"
#include "qom/object.h"

#define DMA_REGS 4

#define TYPE_SPARC32_DMA_DEVICE "sparc32-dma-device"
OBJECT_DECLARE_SIMPLE_TYPE(DMADeviceState, SPARC32_DMA_DEVICE)


struct DMADeviceState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t dmaregs[DMA_REGS];
    qemu_irq irq;
    void *iommu;
    qemu_irq gpio[2];
};

#define TYPE_SPARC32_ESPDMA_DEVICE "sparc32-espdma"
OBJECT_DECLARE_SIMPLE_TYPE(ESPDMADeviceState, SPARC32_ESPDMA_DEVICE)

struct ESPDMADeviceState {
    DMADeviceState parent_obj;

    SysBusESPState esp;
};

#define TYPE_SPARC32_LEDMA_DEVICE "sparc32-ledma"
OBJECT_DECLARE_SIMPLE_TYPE(LEDMADeviceState, SPARC32_LEDMA_DEVICE)

struct LEDMADeviceState {
    DMADeviceState parent_obj;

    SysBusPCNetState lance;
};

#define TYPE_SPARC32_DMA "sparc32-dma"
OBJECT_DECLARE_SIMPLE_TYPE(SPARC32DMAState, SPARC32_DMA)

struct SPARC32DMAState {
    SysBusDevice parent_obj;

    MemoryRegion dmamem;
    MemoryRegion ledma_alias;
    ESPDMADeviceState espdma;
    LEDMADeviceState ledma;
};

/* sparc32_dma.c */
void ledma_memory_read(void *opaque, hwaddr addr,
                       uint8_t *buf, int len, int do_bswap);
void ledma_memory_write(void *opaque, hwaddr addr,
                        uint8_t *buf, int len, int do_bswap);
void espdma_memory_read(void *opaque, uint8_t *buf, int len);
void espdma_memory_write(void *opaque, uint8_t *buf, int len);

#endif
