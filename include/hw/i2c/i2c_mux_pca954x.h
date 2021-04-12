#ifndef QEMU_I2C_MUX_PCA954X
#define QEMU_I2C_MUX_PCA954X

#include "hw/i2c/i2c.h"

#define TYPE_PCA9546 "pca9546"
#define TYPE_PCA9548 "pca9548"

/**
 * Retrieves the i2c bus associated with the specified channel on this i2c
 * mux.
 * @mux: an i2c mux device.
 * @channel: the i2c channel requested
 *
 * Returns: a pointer to the associated i2c bus.
 */
I2CBus *pca954x_i2c_get_bus(I2CSlave *mux, uint8_t channel);

#endif
