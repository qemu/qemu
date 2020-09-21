/*
 * QEMU IndustryPack emulation
 *
 * Copyright (C) 2012 Igalia, S.L.
 * Author: Alberto Garcia <berto@igalia.com>
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any
 * later version.
 */

#ifndef QEMU_IPACK_H
#define QEMU_IPACK_H

#include "hw/qdev-core.h"
#include "qom/object.h"


#define TYPE_IPACK_BUS "IndustryPack"
OBJECT_DECLARE_SIMPLE_TYPE(IPackBus, IPACK_BUS)

struct IPackBus {
    /*< private >*/
    BusState parent_obj;

    /* All fields are private */
    uint8_t n_slots;
    uint8_t free_slot;
    qemu_irq_handler set_irq;
};


#define TYPE_IPACK_DEVICE "ipack-device"
OBJECT_DECLARE_TYPE(IPackDevice, IPackDeviceClass,
                    IPACK_DEVICE)

struct IPackDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    DeviceRealize realize;
    DeviceUnrealize unrealize;

    uint16_t (*io_read)(IPackDevice *dev, uint8_t addr);
    void (*io_write)(IPackDevice *dev, uint8_t addr, uint16_t val);

    uint16_t (*id_read)(IPackDevice *dev, uint8_t addr);
    void (*id_write)(IPackDevice *dev, uint8_t addr, uint16_t val);

    uint16_t (*int_read)(IPackDevice *dev, uint8_t addr);
    void (*int_write)(IPackDevice *dev, uint8_t addr, uint16_t val);

    uint16_t (*mem_read16)(IPackDevice *dev, uint32_t addr);
    void (*mem_write16)(IPackDevice *dev, uint32_t addr, uint16_t val);

    uint8_t (*mem_read8)(IPackDevice *dev, uint32_t addr);
    void (*mem_write8)(IPackDevice *dev, uint32_t addr, uint8_t val);
};

struct IPackDevice {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    int32_t slot;
    /* IRQ objects for the IndustryPack INT0# and INT1# */
    qemu_irq *irq;
};

extern const VMStateDescription vmstate_ipack_device;

#define VMSTATE_IPACK_DEVICE(_field, _state)                            \
    VMSTATE_STRUCT(_field, _state, 1, vmstate_ipack_device, IPackDevice)

IPackDevice *ipack_device_find(IPackBus *bus, int32_t slot);
void ipack_bus_new_inplace(IPackBus *bus, size_t bus_size,
                           DeviceState *parent,
                           const char *name, uint8_t n_slots,
                           qemu_irq_handler handler);

#endif
