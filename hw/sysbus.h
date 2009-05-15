#ifndef HW_SYSBUS_H
#define HW_SYSBUS_H 1

/* Devices attached directly to the main system bus.  */

#include "qdev.h"

#define QDEV_MAX_MMIO 5
#define QDEV_MAX_IRQ 32

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
        int iofunc;
    } mmio[QDEV_MAX_MMIO];
};

typedef void (*sysbus_initfn)(SysBusDevice *dev);

/* Macros to compensate for lack of type inheritance in C.  */
#define sysbus_from_qdev(dev) ((SysBusDevice *)(dev))
#define FROM_SYSBUS(type, dev) DO_UPCAST(type, busdev, dev)

void sysbus_register_dev(const char *name, size_t size, sysbus_initfn init);
void *sysbus_new(void);
void sysbus_init_mmio(SysBusDevice *dev, target_phys_addr_t size, int iofunc);
void sysbus_init_mmio_cb(SysBusDevice *dev, target_phys_addr_t size,
                            mmio_mapfunc cb);
void sysbus_init_irq(SysBusDevice *dev, qemu_irq *p);
void sysbus_pass_irq(SysBusDevice *dev, SysBusDevice *target);


void sysbus_connect_irq(SysBusDevice *dev, int n, qemu_irq irq);
void sysbus_mmio_map(SysBusDevice *dev, int n, target_phys_addr_t addr);

/* Legacy helper function for creating devices.  */
DeviceState *sysbus_create_varargs(const char *name,
                                 target_phys_addr_t addr, ...);
static inline DeviceState *sysbus_create_simple(const char *name,
                                              target_phys_addr_t addr,
                                              qemu_irq irq)
{
    return sysbus_create_varargs(name, addr, irq, NULL);
}

#endif /* !HW_SYSBUS_H */
