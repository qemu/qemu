/*
 * QEMU emulation for TNETW1130 (ACX111).
 * Copyright (c) 2007 Stefan Weil
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

#if !defined(HW_TNETW1130_H)
#define HW_TNETW1130_H

#include "vl.h"

uint32_t acx111_read(unsigned index, unsigned region, target_phys_addr_t addr);
void acx111_write(unsigned index, unsigned region, target_phys_addr_t addr, uint32_t value);

uint16_t acx111_read_mem0(unsigned index, target_phys_addr_t addr);
uint16_t acx111_read_mem1(unsigned index, target_phys_addr_t addr);
void acx111_write_mem0(unsigned index, target_phys_addr_t addr, uint16_t value);
void acx111_write_mem1(unsigned index, target_phys_addr_t addr, uint16_t value);

void pci_tnetw1130_init(PCIBus * bus, NICInfo * nd, int devfn);

#endif /* HW_TNETW1130_H */
