#ifndef HW_ISA_H
#define HW_ISA_H

/* ISA bus */

#include "ioport.h"
#include "qdev.h"

typedef struct ISABus ISABus;
typedef struct ISADevice ISADevice;
typedef struct ISADeviceInfo ISADeviceInfo;

struct ISADevice {
    DeviceState qdev;
    uint32_t isairq[2];
    int nirqs;
    uint16_t ioports[32];
    int nioports;
};

typedef int (*isa_qdev_initfn)(ISADevice *dev);
struct ISADeviceInfo {
    DeviceInfo qdev;
    isa_qdev_initfn init;
};

ISABus *isa_bus_new(DeviceState *dev);
void isa_bus_irqs(qemu_irq *irqs);
qemu_irq isa_get_irq(int isairq);
void isa_init_irq(ISADevice *dev, qemu_irq *p, int isairq);
void isa_init_ioport(ISADevice *dev, uint16_t ioport);
void isa_init_ioport_range(ISADevice *dev, uint16_t start, uint16_t length);
void isa_qdev_register(ISADeviceInfo *info);
ISADevice *isa_create(const char *name);
ISADevice *isa_try_create(const char *name);
ISADevice *isa_create_simple(const char *name);

extern target_phys_addr_t isa_mem_base;

void isa_mmio_init(target_phys_addr_t base, target_phys_addr_t size);

/* dma.c */
int DMA_get_channel_mode (int nchan);
int DMA_read_memory (int nchan, void *buf, int pos, int size);
int DMA_write_memory (int nchan, void *buf, int pos, int size);
void DMA_hold_DREQ (int nchan);
void DMA_release_DREQ (int nchan);
void DMA_schedule(int nchan);
void DMA_init(int high_page_enable, qemu_irq *cpu_request_exit);
void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler,
                           void *opaque);
#endif
