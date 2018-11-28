#ifndef BITBANG_I2C_H
#define BITBANG_I2C_H

#include "hw/i2c/i2c.h"

#define BITBANG_I2C_SDA 0
#define BITBANG_I2C_SCL 1

bitbang_i2c_interface *bitbang_i2c_init(I2CBus *bus);
int bitbang_i2c_set(bitbang_i2c_interface *i2c, int line, int level);

#endif
