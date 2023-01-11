#ifndef BITBANG_I2C_H
#define BITBANG_I2C_H

#include "hw/i2c/i2c.h"

#define TYPE_GPIO_I2C "gpio_i2c"

typedef struct bitbang_i2c_interface bitbang_i2c_interface;

#define BITBANG_I2C_SDA 0
#define BITBANG_I2C_SCL 1

typedef enum bitbang_i2c_state {
    STOPPED = 0,
    SENDING_BIT7,
    SENDING_BIT6,
    SENDING_BIT5,
    SENDING_BIT4,
    SENDING_BIT3,
    SENDING_BIT2,
    SENDING_BIT1,
    SENDING_BIT0,
    WAITING_FOR_ACK,
    RECEIVING_BIT7,
    RECEIVING_BIT6,
    RECEIVING_BIT5,
    RECEIVING_BIT4,
    RECEIVING_BIT3,
    RECEIVING_BIT2,
    RECEIVING_BIT1,
    RECEIVING_BIT0,
    SENDING_ACK,
    SENT_NACK
} bitbang_i2c_state;

struct bitbang_i2c_interface {
    I2CBus *bus;
    bitbang_i2c_state state;
    int last_data;
    int last_clock;
    int device_out;
    uint8_t buffer;
    int current_addr;
};

/**
 * bitbang_i2c_init: in-place initialize the bitbang_i2c_interface struct
 */
void bitbang_i2c_init(bitbang_i2c_interface *s, I2CBus *bus);
int bitbang_i2c_set(bitbang_i2c_interface *i2c, int line, int level);

#endif
