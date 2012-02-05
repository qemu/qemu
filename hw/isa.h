#ifndef HW_ISA_H
#define HW_ISA_H

/* ISA bus */

#include "ioport.h"
#include "memory.h"
#include "qdev.h"

#define ISA_NUM_IRQS 16

typedef struct ISADevice ISADevice;

#define TYPE_ISA_DEVICE "isa-device"
#define ISA_DEVICE(obj) \
     OBJECT_CHECK(ISADevice, (obj), TYPE_ISA_DEVICE)
#define ISA_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(ISADeviceClass, (klass), TYPE_ISA_DEVICE)
#define ISA_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ISADeviceClass, (obj), TYPE_ISA_DEVICE)

typedef struct ISADeviceClass {
    DeviceClass parent_class;
    int (*init)(ISADevice *dev);
} ISADeviceClass;

struct ISABus {
    BusState qbus;
    MemoryRegion *address_space_io;
    qemu_irq *irqs;
};

struct ISADevice {
    DeviceState qdev;
    uint32_t isairq[2];
    int nirqs;
    int ioport_id;
};

ISABus *isa_bus_new(DeviceState *dev, MemoryRegion *address_space_io);
void isa_bus_irqs(ISABus *bus, qemu_irq *irqs);
qemu_irq isa_get_irq(ISADevice *dev, int isairq);
void isa_init_irq(ISADevice *dev, qemu_irq *p, int isairq);
MemoryRegion *isa_address_space(ISADevice *dev);
ISADevice *isa_create(ISABus *bus, const char *name);
ISADevice *isa_try_create(ISABus *bus, const char *name);
ISADevice *isa_create_simple(ISABus *bus, const char *name);

/**
 * isa_register_ioport: Install an I/O port region on the ISA bus.
 *
 * Register an I/O port region via memory_region_add_subregion
 * inside the ISA I/O address space.
 *
 * @dev: the ISADevice against which these are registered; may be NULL.
 * @io: the #MemoryRegion being registered.
 * @start: the base I/O port.
 */
void isa_register_ioport(ISADevice *dev, MemoryRegion *io, uint16_t start);

/**
 * isa_register_portio_list: Initialize a set of ISA io ports
 *
 * Several ISA devices have many dis-joint I/O ports.  Worse, these I/O
 * ports can be interleaved with I/O ports from other devices.  This
 * function makes it easy to create multiple MemoryRegions for a single
 * device and use the legacy portio routines.
 *
 * @dev: the ISADevice against which these are registered; may be NULL.
 * @start: the base I/O port against which the portio->offset is applied.
 * @portio: the ports, sorted by offset.
 * @opaque: passed into the old_portio callbacks.
 * @name: passed into memory_region_init_io.
 */
void isa_register_portio_list(ISADevice *dev, uint16_t start,
                              const MemoryRegionPortio *portio,
                              void *opaque, const char *name);

extern target_phys_addr_t isa_mem_base;

void isa_mmio_setup(MemoryRegion *mr, target_phys_addr_t size);
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
