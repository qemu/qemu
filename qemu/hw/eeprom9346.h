/*
 * QEMU i82559 (EEPRO100) emulation
 *
 * Copyright (c) 2006 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef EEPROM9346_H
#define EEPROM9346_H

#include "vl.h"

typedef struct EEprom9346 eeprom_t;

/* Create a new EEPROM with (nwords * 2) bytes. */
eeprom_t *eeprom9346_new(uint16_t nwords);

/* Destroy an existing EEPROM. */
void eeprom9346_free(eeprom_t *eeprom);

/* Read from the EEPROM. */
uint16_t eeprom9346_read(eeprom_t *eeprom);

/* Write to the EEPROM. */
void eeprom9346_write(eeprom_t *eeprom, int eecs, int eesk, int eedi);

/* Reset the EEPROM. */
void eeprom9346_reset(eeprom_t *eeprom);

/* Get EEPROM data array. */
uint16_t *eeprom9346_data(eeprom_t *eeprom);

#endif /* EEPROM9346_H */
