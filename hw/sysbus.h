#ifndef HW_SYSBUS_H
#define HW_SYSBUS_H 1

/* Devices attached directly to the main system bus.  */

#include "qdev.h"
#include "memory.h"

#define QDEV_MAX_MMIO 32
#define QDEV_MAX_PIO 32
#define QDEV_MAX_IRQ 256

typedef struct SysBusDevice SysBusDevice;
typedef void (*mmio_mapfunc)(SysBusDevice *dev, target_phys_addr_t addr);

struct SysBusDevice {
    DeviceState qdev;
    int num_irq;
    qemu_irq irqs[QDEV_MAX_IRQ];
    qemu_irq *irqp[QDEV_MAX_IRQ];
    int num_mmio;
    struct {
        target_phys_addr_t addr;
        target_phys_addr_t size;
        mmio_mapfunc cb;
        mmio_mapfunc unmap;
        ram_addr_t iofunc;
        MemoryRegion *memory;
    } mmio[QDEV_MAX_MMIO];
    int num_pio;
    pio_addr_t pio[QDEV_MAX_PIO];
};

typedef int (*sysbus_initfn)(SysBusDevice *dev);

/* Macros to compensate for lack of type inheritance in C.  */
#define sysbus_from_qdev(dev) ((SysBusDevice *)(dev))
#define FROM_SYSBUS(type, dev) DO_UPCAST(type, busdev, dev)

typedef struct {
    DeviceInfo qdev;
    sysbus_initfn init;
} SysBusDeviceInfo;

void sysbus_register_dev(const char *name, size_t size, sysbus_initfn init);
void sysbus_register_withprop(SysBusDeviceInfo *info);
void *sysbus_new(void);
void sysbus_init_mmio(SysBusDevice *dev, target_phys_addr_t size,
                      ram_addr_t iofunc);
void sysbus_init_mmio_cb2(SysBusDevice *dev,
                          mmio_mapfunc cb, mmio_mapfunc unmap);
void sysbus_init_mmio_region(SysBusDevice *dev, MemoryRegion *memory);
MemoryRegion *sysbus_mmio_get_region(SysBusDevice *dev, int n);
void sysbus_init_irq(SysBusDevice *dev, qemu_irq *p);
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target);
void sysbus_init_ioports(SysBusDevice *dev, pio_addr_t ioport, pio_addr_t size);


void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq);
void sysbus_mmio_map(SysBusDevice *dev, int n, target_phys_addr_t addr);
void sysbus_add_memory(SysBusDevice *dev, target_phys_addr_t addr,
                       MemoryRegion *mem);
void sysbus_del_memory(SysBusDevice *dev, MemoryRegion *mem);
void sysbus_add_io(SysBusDevice *dev, target_phys_addr_t addr,
                   MemoryRegion *mem);
void sysbus_del_io(SysBusDevice *dev, MemoryRegion *mem);

/* Legacy helper function for creating devices.  */
DeviceState *sysbus_create_varargs(const char *name,
                                 target_phys_addr_t addr, ...);
DeviceState *sysbus_try_create_varargs(const char *name,
                                       target_phys_addr_t addr, ...);
static inline DeviceState *sysbus_create_simple(const char *name,
                                              target_phys_addr_t addr,
                                              qemu_irq irq)
{
    return sysbus_create_varargs(name, addr, irq, NULL);
}

static inline DeviceState *sysbus_try_create_simple(const char *name,
                                                    target_phys_addr_t addr,
                                                    qemu_irq irq)
{
    return sysbus_try_create_varargs(name, addr, irq, NULL);
}

#endif /* !HW_SYSBUS_H */
