#ifndef BITBANG_I2C_H
#define BITBANG_I2C_H

#include "hw/i2c/i2c.h"

typedef struct bitbang_i2c_interface bitbang_i2c_interface;

#define BITBANG_I2C_SDA 0
#define BITBANG_I2C_SCL 1

bitbang_i2c_interface *bitbang_i2c_init(i2c_bus *bus);
int bitbang_i2c_set(bitbang_i2c_interface *i2c, int line, int level);

#endif
