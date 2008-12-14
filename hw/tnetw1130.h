/*
 * QEMU emulation for Texas Instruments TNETW1130 (ACX111) wireless.
 *
 * Copyright (C) 2007 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(HW_TNETW1130_H)
#define HW_TNETW1130_H

#include "qemu-common.h"

void vlynq_tnetw1130_init(void);

// pci_tnetw1130_init is in pci.h
//~ void pci_tnetw1130_init(PCIBus * bus, NICInfo * nd, int devfn);

#endif /* HW_TNETW1130_H */
