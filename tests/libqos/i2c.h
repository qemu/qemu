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

#include <stdint.h>

typedef struct I2CAdapter I2CAdapter;
struct I2CAdapter {
    void (*send)(I2CAdapter *adapter, uint8_t addr,
                 const uint8_t *buf, uint16_t len);
    void (*recv)(I2CAdapter *adapter, uint8_t addr,
                 uint8_t *buf, uint16_t len);
};

void i2c_send(I2CAdapter *i2c, uint8_t addr,
              const uint8_t *buf, uint16_t len);
void i2c_recv(I2CAdapter *i2c, uint8_t addr,
              uint8_t *buf, uint16_t len);

/* libi2c-omap.c */
I2CAdapter *omap_i2c_create(uint64_t addr);

#endif
