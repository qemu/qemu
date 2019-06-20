#ifndef QEMU_I2C_H
#define QEMU_I2C_H

#include "hw/qdev.h"

/* The QEMU I2C implementation only supports simple transfers that complete
   immediately.  It does not support slave devices that need to be able to
   defer their response (eg. CPU slave interfaces where the data is supplied
   by the device driver in response to an interrupt).  */

enum i2c_event {
    I2C_START_RECV,
    I2C_START_SEND,
    I2C_FINISH,
    I2C_NACK /* Masker NACKed a receive byte.  */
};

typedef struct I2CSlave I2CSlave;

#define TYPE_I2C_SLAVE "i2c-slave"
#define I2C_SLAVE(obj) \
     OBJECT_CHECK(I2CSlave, (obj), TYPE_I2C_SLAVE)
#define I2C_SLAVE_CLASS(klass) \
     OBJECT_CLASS_CHECK(I2CSlaveClass, (klass), TYPE_I2C_SLAVE)
#define I2C_SLAVE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(I2CSlaveClass, (obj), TYPE_I2C_SLAVE)

typedef struct I2CSlaveClass {
    DeviceClass parent_class;

    /* Master to slave. Returns non-zero for a NAK, 0 for success. */
    int (*send)(I2CSlave *s, uint8_t data);

    /*
     * Slave to master.  This cannot fail, the device should always
     * return something here.
     */
    uint8_t (*recv)(I2CSlave *s);

    /*
     * Notify the slave of a bus state change.  For start event,
     * returns non-zero to NAK an operation.  For other events the
     * return code is not used and should be zero.
     */
    int (*event)(I2CSlave *s, enum i2c_event event);
} I2CSlaveClass;

struct I2CSlave {
    DeviceState qdev;

    /* Remaining fields for internal use by the I2C code.  */
    uint8_t address;
};

#define TYPE_I2C_BUS "i2c-bus"
#define I2C_BUS(obj) OBJECT_CHECK(I2CBus, (obj), TYPE_I2C_BUS)

typedef struct I2CNode I2CNode;

struct I2CNode {
    I2CSlave *elt;
    QLIST_ENTRY(I2CNode) next;
};

struct I2CBus {
    BusState qbus;
    QLIST_HEAD(, I2CNode) current_devs;
    uint8_t saved_address;
    bool broadcast;
};

I2CBus *i2c_init_bus(DeviceState *parent, const char *name);
void i2c_set_slave_address(I2CSlave *dev, uint8_t address);
int i2c_bus_busy(I2CBus *bus);
int i2c_start_transfer(I2CBus *bus, uint8_t address, int recv);
void i2c_end_transfer(I2CBus *bus);
void i2c_nack(I2CBus *bus);
int i2c_send_recv(I2CBus *bus, uint8_t *data, bool send);
int i2c_send(I2CBus *bus, uint8_t data);
uint8_t i2c_recv(I2CBus *bus);

DeviceState *i2c_create_slave(I2CBus *bus, const char *name, uint8_t addr);

/* lm832x.c */
void lm832x_key_event(DeviceState *dev, int key, int state);

extern const VMStateDescription vmstate_i2c_slave;

#define VMSTATE_I2C_SLAVE(_field, _state) {                          \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(I2CSlave),                                  \
    .vmsd       = &vmstate_i2c_slave,                                \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, I2CSlave),    \
}

#endif
