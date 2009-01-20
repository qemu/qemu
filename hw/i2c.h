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
    I2C_NACK /* Masker NACKed a receive byte.  */
};

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
    i2c_bus *bus;
};

i2c_bus *i2c_init_bus(void);
i2c_slave *i2c_slave_init(i2c_bus *bus, int address, int size);
void i2c_set_slave_address(i2c_slave *dev, int address);
int i2c_bus_busy(i2c_bus *bus);
int i2c_start_transfer(i2c_bus *bus, int address, int recv);
void i2c_end_transfer(i2c_bus *bus);
void i2c_nack(i2c_bus *bus);
int i2c_send(i2c_bus *bus, uint8_t data);
int i2c_recv(i2c_bus *bus);
void i2c_slave_save(QEMUFile *f, i2c_slave *dev);
void i2c_slave_load(QEMUFile *f, i2c_slave *dev);

/* max111x.c */
struct max111x_s;
uint32_t max111x_read(void *opaque);
void max111x_write(void *opaque, uint32_t value);
struct max111x_s *max1110_init(qemu_irq cb);
struct max111x_s *max1111_init(qemu_irq cb);
void max111x_set_input(struct max111x_s *s, int line, uint8_t value);

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
void *wm8750_dac_buffer(void *opaque, int samples);
void wm8750_dac_commit(void *opaque);
void wm8750_set_bclk_in(void *opaque, int new_hz);

/* ssd0303.c */
void ssd0303_init(i2c_bus *bus, int address);

/* twl92230.c */
i2c_slave *twl92230_init(i2c_bus *bus, qemu_irq irq);
qemu_irq *twl92230_gpio_in_get(i2c_slave *i2c);
void twl92230_gpio_out_set(i2c_slave *i2c, int line, qemu_irq handler);

/* tmp105.c */
struct i2c_slave *tmp105_init(i2c_bus *bus, qemu_irq alarm);
void tmp105_reset(i2c_slave *i2c);
void tmp105_set(i2c_slave *i2c, int temp);

/* lm832x.c */
struct i2c_slave *lm8323_init(i2c_bus *bus, qemu_irq nirq);
void lm832x_key_event(struct i2c_slave *i2c, int key, int state);

#endif
