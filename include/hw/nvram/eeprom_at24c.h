/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef EEPROM_AT24C_H
#define EEPROM_AT24C_H

#include "hw/i2c/i2c.h"

/*
 * Create and realize an AT24C EEPROM device on the heap.
 * @bus: I2C bus to put it on
 * @address: I2C address of the EEPROM slave when put on a bus
 * @rom_size: size of the EEPROM
 *
 * Create the device state structure, initialize it, put it on the specified
 * @bus, and drop the reference to it (the device is realized).
 */
I2CSlave *at24c_eeprom_init(I2CBus *bus, uint8_t address, uint32_t rom_size);


/*
 * Create and realize an AT24C EEPROM device on the heap with initial data.
 * @bus: I2C bus to put it on
 * @address: I2C address of the EEPROM slave when put on a bus
 * @rom_size: size of the EEPROM
 * @init_rom: Array of bytes to initialize EEPROM memory with
 * @init_rom_size: Size of @init_rom, must be less than or equal to @rom_size
 *
 * Create the device state structure, initialize it, put it on the specified
 * @bus, and drop the reference to it (the device is realized). Copies the data
 * from @init_rom to the beginning of the EEPROM memory buffer.
 */
I2CSlave *at24c_eeprom_init_rom(I2CBus *bus, uint8_t address, uint32_t rom_size,
                                const uint8_t *init_rom, uint32_t init_rom_size);

#endif
