#ifndef QEMU_I2C_H
#define QEMU_I2C_H

/* The QEMU I2C implementation only supports simple transfers that complete
   immediately.  It does not support slave devices that need to be able to
   defer their response (eg. CPU slave interfaces where the data is supplied
   by the device driver in response to an interrupt).  */

enum i2c_event {
    I2C_START_RECV,
    I2C_START_SEND,
    I2C_FINISH,
    I2C_NACK /* Masker NACKed a recieve byte.  */
};

typedef struct i2c_slave i2c_slave;

/* Master to slave.  */
typedef int (*i2c_send_cb)(i2c_slave *s, uint8_t data);
/* Slave to master.  */
typedef int (*i2c_recv_cb)(i2c_slave *s);
/* Notify the slave of a bus state change.  */
typedef void (*i2c_event_cb)(i2c_slave *s, enum i2c_event event);

struct i2c_slave
{
    /* Callbacks to be set by the device.  */
    i2c_event_cb event;
    i2c_recv_cb recv;
    i2c_send_cb send;

    /* Remaining fields for internal use by the I2C code.  */
    int address;
    void *next;
};

typedef struct i2c_bus i2c_bus;

i2c_bus *i2c_init_bus(void);
i2c_slave *i2c_slave_init(i2c_bus *bus, int address, int size);
void i2c_set_slave_address(i2c_slave *dev, int address);
int i2c_bus_busy(i2c_bus *bus);
int i2c_start_transfer(i2c_bus *bus, int address, int recv);
void i2c_end_transfer(i2c_bus *bus);
void i2c_nack(i2c_bus *bus);
int i2c_send(i2c_bus *bus, uint8_t data);
int i2c_recv(i2c_bus *bus);
void i2c_bus_save(QEMUFile *f, i2c_bus *bus);
void i2c_bus_load(QEMUFile *f, i2c_bus *bus);
void i2c_slave_save(QEMUFile *f, i2c_slave *dev);
void i2c_slave_load(QEMUFile *f, i2c_slave *dev);

/* max7310.c */
i2c_slave *max7310_init(i2c_bus *bus);
void max7310_reset(i2c_slave *i2c);
qemu_irq *max7310_gpio_in_get(i2c_slave *i2c);
void max7310_gpio_out_set(i2c_slave *i2c, int line, qemu_irq handler);

/* wm8750.c */
i2c_slave *wm8750_init(i2c_bus *bus, AudioState *audio);
void wm8750_reset(i2c_slave *i2c);
void wm8750_data_req_set(i2c_slave *i2c,
                void (*data_req)(void *, int, int), void *opaque);
void wm8750_dac_dat(void *opaque, uint32_t sample);
uint32_t wm8750_adc_dat(void *opaque);

#endif
