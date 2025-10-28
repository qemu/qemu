#ifndef HW_SYSBUS_H
#define HW_SYSBUS_H

/* Devices attached directly to the main system bus.  */

#include "hw/qdev-core.h"
#include "system/memory.h"
#include "qom/object.h"

#define QDEV_MAX_MMIO 32
#define QDEV_MAX_PIO 32

#define TYPE_SYSTEM_BUS "System"
DECLARE_INSTANCE_CHECKER(BusState, SYSTEM_BUS,
                         TYPE_SYSTEM_BUS)


#define TYPE_SYS_BUS_DEVICE "sys-bus-device"
OBJECT_DECLARE_TYPE(SysBusDevice, SysBusDeviceClass,
                    SYS_BUS_DEVICE)

#define TYPE_DYNAMIC_SYS_BUS_DEVICE "dynamic-sysbus-device"

/**
 * SysBusDeviceClass:
 *
 * SysBusDeviceClass is not overriding #DeviceClass.realize, so derived
 * classes overriding it are not required to invoke its implementation.
 */

#define SYSBUS_DEVICE_GPIO_IRQ "sysbus-irq"

struct SysBusDeviceClass {
    /*< private >*/
    DeviceClass parent_class;

    /*
     * Let the sysbus device format its own non-PIO, non-MMIO unit address.
     *
     * Sometimes a class of SysBusDevices has neither MMIO nor PIO resources,
     * yet instances of it would like to distinguish themselves, in
     * OpenFirmware device paths, from other instances of the same class on the
     * sysbus. For that end we expose this callback.
     *
     * The implementation is not supposed to change *@dev, or incur other
     * observable change.
     *
     * The function returns a dynamically allocated string. On error, NULL
     * should be returned; the unit address portion of the OFW node will be
     * omitted then. (This is not considered a fatal error.)
     */
    char *(*explicit_ofw_unit_address)(const SysBusDevice *dev);
    void (*connect_irq_notifier)(SysBusDevice *dev, qemu_irq irq);
};

struct SysBusDevice {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    int num_mmio;
    struct {
        hwaddr addr;
        MemoryRegion *memory;
    } mmio[QDEV_MAX_MMIO];
    int num_pio;
    uint32_t pio[QDEV_MAX_PIO];
};

typedef void FindSysbusDeviceFunc(SysBusDevice *sbdev, void *opaque);

void sysbus_init_mmio(SysBusDevice *dev, MemoryRegion *memory);
MemoryRegion *sysbus_mmio_get_region(const SysBusDevice *dev, int n);
void sysbus_init_irq(SysBusDevice *dev, qemu_irq *p);
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target);
void sysbus_init_ioports(SysBusDevice *dev, uint32_t ioport, uint32_t size);


bool sysbus_has_irq(const SysBusDevice *dev, int n);
bool sysbus_has_mmio(const SysBusDevice *dev, unsigned int n);
void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq);
bool sysbus_is_irq_connected(const SysBusDevice *dev, int n);
qemu_irq sysbus_get_connected_irq(const SysBusDevice *dev, int n);
void sysbus_mmio_map(SysBusDevice *dev, int n, hwaddr addr);
int sysbus_mmio_map_name(SysBusDevice *dev, const char*name, hwaddr addr);
void sysbus_mmio_map_overlap(SysBusDevice *dev, int n, hwaddr addr,
                             int priority);

bool sysbus_realize(SysBusDevice *dev, Error **errp);
bool sysbus_realize_and_unref(SysBusDevice *dev, Error **errp);

/* Call func for every dynamically created sysbus device in the system */
void foreach_dynamic_sysbus_device(FindSysbusDeviceFunc *func, void *opaque);

/* Legacy helper function for creating devices.  */
DeviceState *sysbus_create_varargs(const char *name,
                                 hwaddr addr, ...);

static inline DeviceState *sysbus_create_simple(const char *name,
                                              hwaddr addr,
                                              qemu_irq irq)
{
    return sysbus_create_varargs(name, addr, irq, NULL);
}

#endif /* HW_SYSBUS_H */
