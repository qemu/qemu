#ifndef QEMU_I2C_H
#define QEMU_I2C_H

#include "qdev.h"

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

typedef struct I2CSlaveClass
{
    DeviceClass parent_class;

    /* Callbacks provided by the device.  */
    int (*init)(I2CSlave *dev);

    /* Master to slave.  */
    int (*send)(I2CSlave *s, uint8_t data);

    /* Slave to master.  */
    int (*recv)(I2CSlave *s);

    /* Notify the slave of a bus state change.  */
    void (*event)(I2CSlave *s, enum i2c_event event);
} I2CSlaveClass;

struct I2CSlave
{
    DeviceState qdev;

    /* Remaining fields for internal use by the I2C code.  */
    uint8_t address;
};

i2c_bus *i2c_init_bus(DeviceState *parent, const char *name);
void i2c_set_slave_address(I2CSlave *dev, uint8_t address);
int i2c_bus_busy(i2c_bus *bus);
int i2c_start_transfer(i2c_bus *bus, uint8_t address, int recv);
void i2c_end_transfer(i2c_bus *bus);
void i2c_nack(i2c_bus *bus);
int i2c_send(i2c_bus *bus, uint8_t data);
int i2c_recv(i2c_bus *bus);

#define I2C_SLAVE_FROM_QDEV(dev) DO_UPCAST(I2CSlave, qdev, dev)
#define FROM_I2C_SLAVE(type, dev) DO_UPCAST(type, i2c, dev)

DeviceState *i2c_create_slave(i2c_bus *bus, const char *name, uint8_t addr);

/* wm8750.c */
void wm8750_data_req_set(DeviceState *dev,
                void (*data_req)(void *, int, int), void *opaque);
void wm8750_dac_dat(void *opaque, uint32_t sample);
uint32_t wm8750_adc_dat(void *opaque);
void *wm8750_dac_buffer(void *opaque, int samples);
void wm8750_dac_commit(void *opaque);
void wm8750_set_bclk_in(void *opaque, int new_hz);

/* tmp105.c */
void tmp105_set(I2CSlave *i2c, int temp);

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
