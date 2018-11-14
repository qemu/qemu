/*
 * QEMU SMBus host (master) API
 *
 * Copyright (c) 2007 Arastra, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_SMBUS_MASTER_H
#define HW_SMBUS_MASTER_H

#include "hw/i2c/i2c.h"

/* Master device commands.  */
int smbus_quick_command(I2CBus *bus, uint8_t addr, int read);
int smbus_receive_byte(I2CBus *bus, uint8_t addr);
int smbus_send_byte(I2CBus *bus, uint8_t addr, uint8_t data);
int smbus_read_byte(I2CBus *bus, uint8_t addr, uint8_t command);
int smbus_write_byte(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t data);
int smbus_read_word(I2CBus *bus, uint8_t addr, uint8_t command);
int smbus_write_word(I2CBus *bus, uint8_t addr, uint8_t command, uint16_t data);

/*
 * Do a block transfer from an I2C device.  If recv_len is set, then the
 * first received byte is a length field and is used to know how much data
 * to receive.  Otherwise receive "len" bytes.  If send_cmd is set, send
 * the command byte first before receiving the data.
 */
int smbus_read_block(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t *data,
                     int len, bool recv_len, bool send_cmd);

/*
 * Do a block transfer to an I2C device.  If send_len is set, send the
 * "len" value before the data.
 */
int smbus_write_block(I2CBus *bus, uint8_t addr, uint8_t command, uint8_t *data,
                      int len, bool send_len);

#endif
