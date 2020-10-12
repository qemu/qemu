/* QEMU Synchronous Serial Interface support.  */

/*
 * In principle SSI is a point-point interface.  As such the qemu
 * implementation has a single peripheral on a "bus".
 * However it is fairly common for boards to have multiple peripherals
 * connected to a single master, and select devices with an external
 * chip select.  This is implemented in qemu by having an explicit mux device.
 * It is assumed that master and peripheral are both using the same transfer
 * width.
 */

#ifndef QEMU_SSI_H
#define QEMU_SSI_H

#include "hw/qdev-core.h"
#include "qom/object.h"

typedef enum SSICSMode SSICSMode;

#define TYPE_SSI_PERIPHERAL "ssi-peripheral"
OBJECT_DECLARE_TYPE(SSIPeripheral, SSIPeripheralClass,
                    SSI_PERIPHERAL)

#define SSI_GPIO_CS "ssi-gpio-cs"

enum SSICSMode {
    SSI_CS_NONE = 0,
    SSI_CS_LOW,
    SSI_CS_HIGH,
};

/* Peripherals.  */
struct SSIPeripheralClass {
    DeviceClass parent_class;

    void (*realize)(SSIPeripheral *dev, Error **errp);

    /* if you have standard or no CS behaviour, just override transfer.
     * This is called when the device cs is active (true by default).
     */
    uint32_t (*transfer)(SSIPeripheral *dev, uint32_t val);
    /* called when the CS line changes. Optional, devices only need to implement
     * this if they have side effects associated with the cs line (beyond
     * tristating the txrx lines).
     */
    int (*set_cs)(SSIPeripheral *dev, bool select);
    /* define whether or not CS exists and is active low/high */
    SSICSMode cs_polarity;

    /* if you have non-standard CS behaviour override this to take control
     * of the CS behaviour at the device level. transfer, set_cs, and
     * cs_polarity are unused if this is overwritten. Transfer_raw will
     * always be called for the device for every txrx access to the parent bus
     */
    uint32_t (*transfer_raw)(SSIPeripheral *dev, uint32_t val);
};

struct SSIPeripheral {
    DeviceState parent_obj;

    /* Chip select state */
    bool cs;
};

extern const VMStateDescription vmstate_ssi_peripheral;

#define VMSTATE_SSI_PERIPHERAL(_field, _state) {                     \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(SSIPeripheral),                             \
    .vmsd       = &vmstate_ssi_peripheral,                           \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, SSIPeripheral), \
}

DeviceState *ssi_create_peripheral(SSIBus *bus, const char *name);
/**
 * ssi_realize_and_unref: realize and unref an SSI peripheral
 * @dev: SSI peripheral to realize
 * @bus: SSI bus to put it on
 * @errp: error pointer
 *
 * Call 'realize' on @dev, put it on the specified @bus, and drop the
 * reference to it. Errors are reported via @errp and by returning
 * false.
 *
 * This function is useful if you have created @dev via qdev_new()
 * (which takes a reference to the device it returns to you), so that
 * you can set properties on it before realizing it. If you don't need
 * to set properties then ssi_create_peripheral() is probably better (as it
 * does the create, init and realize in one step).
 *
 * If you are embedding the SSI peripheral into another QOM device and
 * initialized it via some variant on object_initialize_child() then
 * do not use this function, because that family of functions arrange
 * for the only reference to the child device to be held by the parent
 * via the child<> property, and so the reference-count-drop done here
 * would be incorrect.  (Instead you would want ssi_realize(), which
 * doesn't currently exist but would be trivial to create if we had
 * any code that wanted it.)
 */
bool ssi_realize_and_unref(DeviceState *dev, SSIBus *bus, Error **errp);

/* Master interface.  */
SSIBus *ssi_create_bus(DeviceState *parent, const char *name);

uint32_t ssi_transfer(SSIBus *bus, uint32_t val);

#endif
