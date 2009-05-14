#ifndef QDEV_H
#define QDEV_H

#include "hw.h"

typedef struct DeviceType DeviceType;

typedef struct DeviceProperty DeviceProperty;

typedef struct ChildBusList ChildBusList;

/* This structure should not be accessed directly.  We declare it here
   so that it can be embedded in individual device state structures.  */
struct DeviceState
{
    const char *name;
    DeviceType *type;
    void *bus;
    DeviceProperty *props;
    int num_irq_sink;
    qemu_irq *irq_sink;
    int num_gpio_out;
    qemu_irq *gpio_out;
    int num_gpio_in;
    qemu_irq *gpio_in;
    ChildBusList *child_bus;
};

/*** Board API.  This should go away once we have a machine config file.  ***/

DeviceState *qdev_create(void *bus, const char *name);
void qdev_init(DeviceState *dev);

/* Set properties between creation and init.  */
void qdev_set_prop_int(DeviceState *dev, const char *name, int value);
void qdev_set_prop_ptr(DeviceState *dev, const char *name, void *value);

qemu_irq qdev_get_irq_sink(DeviceState *dev, int n);
qemu_irq qdev_get_gpio_in(DeviceState *dev, int n);
void qdev_connect_gpio_out(DeviceState *dev, int n, qemu_irq pin);

void *qdev_get_child_bus(DeviceState *dev, const char *name);

/*** Device API.  ***/

typedef void (*qdev_initfn)(DeviceState *dev, void *opaque);

DeviceType *qdev_register(const char *name, int size, qdev_initfn init,
                          void *opaque);

/* Register device properties.  */
void qdev_init_irq_sink(DeviceState *dev, qemu_irq_handler handler, int nirq);
void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n);
void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n);
void qdev_attach_child_bus(DeviceState *dev, const char *name, void *bus);

CharDriverState *qdev_init_chardev(DeviceState *dev);

void *qdev_get_bus(DeviceState *dev);
uint64_t qdev_get_prop_int(DeviceState *dev, const char *name, uint64_t def);
void *qdev_get_prop_ptr(DeviceState *dev, const char *name);

/* Convery from a base type to a parent type, with compile time checking.  */
#ifdef __GNUC__
#define DO_UPCAST(type, field, dev) ( __extension__ ( { \
    char __attribute__((unused)) offset_must_be_zero[ \
        -offsetof(type, field)]; \
    container_of(dev, type, field);}))
#else
#define DO_UPCAST(type, field, dev) container_of(dev, type, field)
#endif

#endif
