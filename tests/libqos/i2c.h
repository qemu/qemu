/*
 * I2C libqos
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBQOS_I2C_H
#define LIBQOS_I2C_H

#include "libqtest.h"
#include "libqos/qgraph.h"

typedef struct I2CAdapter I2CAdapter;
struct I2CAdapter {
    void (*send)(I2CAdapter *adapter, uint8_t addr,
                 const uint8_t *buf, uint16_t len);
    void (*recv)(I2CAdapter *adapter, uint8_t addr,
                 uint8_t *buf, uint16_t len);

    QTestState *qts;
};

typedef struct QI2CDevice QI2CDevice;
struct QI2CDevice {
    /*
     * For now, all devices are simple enough that there is no need for
     * them to define their own constructor and get_driver functions.
     * Therefore, QOSGraphObject is included directly in QI2CDevice;
     * the tests expect to get a QI2CDevice rather than doing something
     * like obj->get_driver("i2c-device").
     *
     * In fact there is no i2c-device interface even, because there are
     * no generic I2C tests).
     */
    QOSGraphObject obj;
    I2CAdapter *bus;
};

void *i2c_device_create(void *i2c_bus, QGuestAllocator *alloc, void *addr);

void i2c_send(I2CAdapter *i2c, uint8_t addr,
              const uint8_t *buf, uint16_t len);
void i2c_recv(I2CAdapter *i2c, uint8_t addr,
              uint8_t *buf, uint16_t len);

void i2c_read_block(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
                    uint8_t *buf, uint16_t len);
void i2c_write_block(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
                     const uint8_t *buf, uint16_t len);
uint8_t i2c_get8(I2CAdapter *i2c, uint8_t addr, uint8_t reg);
uint16_t i2c_get16(I2CAdapter *i2c, uint8_t addr, uint8_t reg);
void i2c_set8(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
              uint8_t value);
void i2c_set16(I2CAdapter *i2c, uint8_t addr, uint8_t reg,
               uint16_t value);

/* i2c-omap.c */
typedef struct OMAPI2C {
    QOSGraphObject obj;
    I2CAdapter parent;

    uint64_t addr;
} OMAPI2C;

void omap_i2c_init(OMAPI2C *s, QTestState *qts, uint64_t addr);

/* i2c-imx.c */
typedef struct IMXI2C {
    QOSGraphObject obj;
    I2CAdapter parent;

    uint64_t addr;
} IMXI2C;

void imx_i2c_init(IMXI2C *s, QTestState *qts, uint64_t addr);
I2CAdapter *imx_i2c_create(QTestState *qts, uint64_t addr);
void imx_i2c_free(I2CAdapter *i2c);

#endif
