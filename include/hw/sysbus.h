#ifndef HW_SYSBUS_H
#define HW_SYSBUS_H

/* Devices attached directly to the main system bus.  */

#include "hw/qdev-core.h"
#include "exec/memory.h"

#define QDEV_MAX_MMIO 32
#define QDEV_MAX_PIO 32

#define TYPE_SYSTEM_BUS "System"
#define SYSTEM_BUS(obj) OBJECT_CHECK(BusState, (obj), TYPE_SYSTEM_BUS)

typedef struct SysBusDevice SysBusDevice;

#define TYPE_SYS_BUS_DEVICE "sys-bus-device"
#define SYS_BUS_DEVICE(obj) \
     OBJECT_CHECK(SysBusDevice, (obj), TYPE_SYS_BUS_DEVICE)
#define SYS_BUS_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SysBusDeviceClass, (klass), TYPE_SYS_BUS_DEVICE)
#define SYS_BUS_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SysBusDeviceClass, (obj), TYPE_SYS_BUS_DEVICE)

/**
 * SysBusDeviceClass:
 *
 * SysBusDeviceClass is not overriding #DeviceClass.realize, so derived
 * classes overriding it are not required to invoke its implementation.
 */

#define SYSBUS_DEVICE_GPIO_IRQ "sysbus-irq"

typedef struct SysBusDeviceClass {
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
} SysBusDeviceClass;

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
MemoryRegion *sysbus_mmio_get_region(SysBusDevice *dev, int n);
void sysbus_init_irq(SysBusDevice *dev, qemu_irq *p);
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target);
void sysbus_init_ioports(SysBusDevice *dev, uint32_t ioport, uint32_t size);


bool sysbus_has_irq(SysBusDevice *dev, int n);
bool sysbus_has_mmio(SysBusDevice *dev, unsigned int n);
void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq);
bool sysbus_is_irq_connected(SysBusDevice *dev, int n);
qemu_irq sysbus_get_connected_irq(SysBusDevice *dev, int n);
void sysbus_mmio_map(SysBusDevice *dev, int n, hwaddr addr);
void sysbus_mmio_map_overlap(SysBusDevice *dev, int n, hwaddr addr,
                             int priority);
void sysbus_mmio_unmap(SysBusDevice *dev, int n);
void sysbus_add_io(SysBusDevice *dev, hwaddr addr,
                   MemoryRegion *mem);
MemoryRegion *sysbus_address_space(SysBusDevice *dev);

/**
 * sysbus_init_child_obj:
 * @parent: The parent object
 * @childname: Used as name of the "child<>" property in the parent
 * @child: A pointer to the memory to be used for the object.
 * @childsize: The maximum size available at @child for the object.
 * @childtype: The name of the type of the object to instantiate.
 *
 * This function will initialize an object and attach it to the main system
 * bus. The memory for the object should have already been allocated. The
 * object will then be added as child to the given parent. The returned object
 * has a reference count of 1 (for the "child<...>" property from the parent),
 * so the object will be finalized automatically when the parent gets removed.
 */
void sysbus_init_child_obj(Object *parent, const char *childname, void *child,
                           size_t childsize, const char *childtype);

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
