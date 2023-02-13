/*
 *  ioapic.c IOAPIC emulation logic
 *
 *  Copyright (c) 2011 Jan Kiszka, Siemens AG
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_INTC_IOAPIC_H
#define HW_INTC_IOAPIC_H

#define IOAPIC_NUM_PINS 24
#define IO_APIC_DEFAULT_ADDRESS 0xfec00000
#define IO_APIC_SECONDARY_ADDRESS (IO_APIC_DEFAULT_ADDRESS + 0x10000)
#define IO_APIC_SECONDARY_IRQBASE 24 /* primary 0 -> 23, secondary 24 -> 47 */

#define TYPE_KVM_IOAPIC "kvm-ioapic"
#define TYPE_IOAPIC "ioapic"

void ioapic_eoi_broadcast(int vector);

#endif /* HW_INTC_IOAPIC_H */
