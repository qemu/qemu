#ifndef HW_ISA_H
#define HW_ISA_H

/* ISA bus */

#include "exec/memory.h"
#include "exec/ioport.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

#define ISA_NUM_IRQS 16

#define TYPE_ISA_DEVICE "isa-device"
OBJECT_DECLARE_TYPE(ISADevice, ISADeviceClass, ISA_DEVICE)

#define TYPE_ISA_BUS "ISA"
OBJECT_DECLARE_SIMPLE_TYPE(ISABus, ISA_BUS)

#define TYPE_APPLE_SMC "isa-applesmc"
#define APPLESMC_MAX_DATA_LENGTH       32
#define APPLESMC_PROP_IO_BASE "iobase"

static inline uint16_t applesmc_port(void)
{
    Object *obj = object_resolve_path_type("", TYPE_APPLE_SMC, NULL);

    if (obj) {
        return object_property_get_uint(obj, APPLESMC_PROP_IO_BASE, NULL);
    }
    return 0;
}

#define TYPE_ISADMA "isa-dma"

typedef struct IsaDmaClass IsaDmaClass;
DECLARE_CLASS_CHECKERS(IsaDmaClass, ISADMA,
                       TYPE_ISADMA)
#define ISADMA(obj) \
    INTERFACE_CHECK(IsaDma, (obj), TYPE_ISADMA)

typedef enum {
    ISADMA_TRANSFER_VERIFY,
    ISADMA_TRANSFER_READ,
    ISADMA_TRANSFER_WRITE,
    ISADMA_TRANSFER_ILLEGAL,
} IsaDmaTransferMode;

typedef int (*IsaDmaTransferHandler)(void *opaque, int nchan, int pos,
                                     int size);

struct IsaDmaClass {
    InterfaceClass parent;

    bool (*has_autoinitialization)(IsaDma *obj, int nchan);
    int (*read_memory)(IsaDma *obj, int nchan, void *buf, int pos, int len);
    int (*write_memory)(IsaDma *obj, int nchan, void *buf, int pos, int len);
    void (*hold_DREQ)(IsaDma *obj, int nchan);
    void (*release_DREQ)(IsaDma *obj, int nchan);
    void (*schedule)(IsaDma *obj);
    void (*register_channel)(IsaDma *obj, int nchan,
                             IsaDmaTransferHandler transfer_handler,
                             void *opaque);
};

struct ISADeviceClass {
    DeviceClass parent_class;
    void (*build_aml)(ISADevice *dev, Aml *scope);
};

struct ISABus {
    /*< private >*/
    BusState parent_obj;
    /*< public >*/

    MemoryRegion *address_space;
    MemoryRegion *address_space_io;
    qemu_irq *irqs;
    IsaDma *dma[2];
};

struct ISADevice {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    int8_t isairq[2];      /* -1 = unassigned */
    int nirqs;
    int ioport_id;
};

ISABus *isa_bus_new(DeviceState *dev, MemoryRegion *address_space,
                    MemoryRegion *address_space_io, Error **errp);
void isa_bus_irqs(ISABus *bus, qemu_irq *irqs);
qemu_irq isa_get_irq(ISADevice *dev, unsigned isairq);
void isa_init_irq(ISADevice *dev, qemu_irq *p, unsigned isairq);
void isa_connect_gpio_out(ISADevice *isadev, int gpioirq, unsigned isairq);
void isa_bus_dma(ISABus *bus, IsaDma *dma8, IsaDma *dma16);
IsaDma *isa_get_dma(ISABus *bus, int nchan);
MemoryRegion *isa_address_space(ISADevice *dev);
MemoryRegion *isa_address_space_io(ISADevice *dev);
ISADevice *isa_new(const char *name);
ISADevice *isa_try_new(const char *name);
bool isa_realize_and_unref(ISADevice *dev, ISABus *bus, Error **errp);
ISADevice *isa_create_simple(ISABus *bus, const char *name);

ISADevice *isa_vga_init(ISABus *bus);
void isa_build_aml(ISABus *bus, Aml *scope);

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
 * @piolist: the PortioList associated with the io ports
 * @start: the base I/O port against which the portio->offset is applied.
 * @portio: the ports, sorted by offset.
 * @opaque: passed into the portio callbacks.
 * @name: passed into memory_region_init_io.
 *
 * Returns: 0 on success, negative error code otherwise (e.g. if the
 *          ISA bus is not available)
 */
int isa_register_portio_list(ISADevice *dev,
                             PortioList *piolist,
                             uint16_t start,
                             const MemoryRegionPortio *portio,
                             void *opaque, const char *name);

static inline ISABus *isa_bus_from_device(ISADevice *d)
{
    return ISA_BUS(qdev_get_parent_bus(DEVICE(d)));
}

#define TYPE_PIIX4_PCI_DEVICE "piix4-isa"

#endif
