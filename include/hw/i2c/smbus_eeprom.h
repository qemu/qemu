/*
 * QEMU SMBus EEPROM API
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

#ifndef HW_SMBUS_EEPROM_H
#define HW_SMBUS_EEPROM_H

#include "exec/cpu-common.h"
#include "hw/i2c/i2c.h"

void smbus_eeprom_init_one(I2CBus *bus, uint8_t address, uint8_t *eeprom_buf);
void smbus_eeprom_init(I2CBus *bus, int nb_eeprom,
                       const uint8_t *eeprom_spd, int size);

enum sdram_type { SDR = 0x4, DDR = 0x7, DDR2 = 0x8 };
uint8_t *spd_data_generate(enum sdram_type type, ram_addr_t size);

#endif
