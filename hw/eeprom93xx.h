/*
 * QEMU EEPROM 93xx emulation
 *
 * Copyright (c) 2006-2007 Stefan Weil
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

#ifndef EEPROM93XX_H
#define EEPROM93XX_H

typedef struct _eeprom_t eeprom_t;

/* Create a new EEPROM with (nwords * 2) bytes. */
eeprom_t *eeprom93xx_new(uint16_t nwords);

/* Destroy an existing EEPROM. */
void eeprom93xx_free(eeprom_t *eeprom);

/* Read from the EEPROM. */
uint16_t eeprom93xx_read(eeprom_t *eeprom);

/* Write to the EEPROM. */
void eeprom93xx_write(eeprom_t *eeprom, int eecs, int eesk, int eedi);

/* Get EEPROM data array. */
uint16_t *eeprom93xx_data(eeprom_t *eeprom);

#endif /* EEPROM93XX_H */
