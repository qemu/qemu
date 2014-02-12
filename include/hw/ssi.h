/* QEMU Synchronous Serial Interface support.  */

/* In principle SSI is a point-point interface.  As such the qemu
   implementation has a single slave device on a "bus".
   However it is fairly common for boards to have multiple slaves
   connected to a single master, and select devices with an external
   chip select.  This is implemented in qemu by having an explicit mux device.
   It is assumed that master and slave are both using the same transfer width.
   */

#ifndef QEMU_SSI_H
#define QEMU_SSI_H

#include "hw/qdev.h"

typedef struct SSISlave SSISlave;

#define TYPE_SSI_SLAVE "ssi-slave"
#define SSI_SLAVE(obj) \
     OBJECT_CHECK(SSISlave, (obj), TYPE_SSI_SLAVE)
#define SSI_SLAVE_CLASS(klass) \
     OBJECT_CLASS_CHECK(SSISlaveClass, (klass), TYPE_SSI_SLAVE)
#define SSI_SLAVE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SSISlaveClass, (obj), TYPE_SSI_SLAVE)

typedef enum {
    SSI_CS_NONE = 0,
    SSI_CS_LOW,
    SSI_CS_HIGH,
} SSICSMode;

/* Slave devices.  */
typedef struct SSISlaveClass {
    DeviceClass parent_class;

    int (*init)(SSISlave *dev);

    /* if you have standard or no CS behaviour, just override transfer.
     * This is called when the device cs is active (true by default).
     */
    uint32_t (*transfer)(SSISlave *dev, uint32_t val);
    /* called when the CS line changes. Optional, devices only need to implement
     * this if they have side effects associated with the cs line (beyond
     * tristating the txrx lines).
     */
    int (*set_cs)(SSISlave *dev, bool select);
    /* define whether or not CS exists and is active low/high */
    SSICSMode cs_polarity;

    /* if you have non-standard CS behaviour override this to take control
     * of the CS behaviour at the device level. transfer, set_cs, and
     * cs_polarity are unused if this is overwritten. Transfer_raw will
     * always be called for the device for every txrx access to the parent bus
     */
    uint32_t (*transfer_raw)(SSISlave *dev, uint32_t val);
} SSISlaveClass;

struct SSISlave {
    DeviceState parent_obj;

    /* Chip select state */
    bool cs;
};

#define FROM_SSI_SLAVE(type, dev) DO_UPCAST(type, ssidev, dev)

extern const VMStateDescription vmstate_ssi_slave;

#define VMSTATE_SSI_SLAVE(_field, _state) {                          \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(SSISlave),                                  \
    .vmsd       = &vmstate_ssi_slave,                                \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, SSISlave),    \
}

DeviceState *ssi_create_slave(SSIBus *bus, const char *name);
DeviceState *ssi_create_slave_no_init(SSIBus *bus, const char *name);

/* Master interface.  */
SSIBus *ssi_create_bus(DeviceState *parent, const char *name);

uint32_t ssi_transfer(SSIBus *bus, uint32_t val);

/* Automatically connect all children nodes a spi controller as slaves */
void ssi_auto_connect_slaves(DeviceState *parent, qemu_irq *cs_lines,
                             SSIBus *bus);

/* max111x.c */
void max111x_set_input(DeviceState *dev, int line, uint8_t value);

#endif
