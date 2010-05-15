#ifndef QEMU_HW_ACPI_H
#define QEMU_HW_ACPI_H
/*
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

/* from linux include/acpi/actype.h */
/* Default ACPI register widths */

#define ACPI_GPE_REGISTER_WIDTH         8
#define ACPI_PM1_REGISTER_WIDTH         16
#define ACPI_PM2_REGISTER_WIDTH         8
#define ACPI_PM_TIMER_WIDTH             32

/* PM Timer ticks per second (HZ) */
#define PM_TIMER_FREQUENCY  3579545


/* ACPI fixed hardware registers */

/* from linux/drivers/acpi/acpica/aclocal.h */
/* Masks used to access the bit_registers */

/* PM1x_STS */
#define ACPI_BITMASK_TIMER_STATUS               0x0001
#define ACPI_BITMASK_BUS_MASTER_STATUS          0x0010
#define ACPI_BITMASK_GLOBAL_LOCK_STATUS         0x0020
#define ACPI_BITMASK_POWER_BUTTON_STATUS        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_STATUS        0x0200
#define ACPI_BITMASK_RT_CLOCK_STATUS            0x0400
#define ACPI_BITMASK_PCIEXP_WAKE_STATUS         0x4000	/* ACPI 3.0 */
#define ACPI_BITMASK_WAKE_STATUS                0x8000

#define ACPI_BITMASK_ALL_FIXED_STATUS           (\
	ACPI_BITMASK_TIMER_STATUS          | \
	ACPI_BITMASK_BUS_MASTER_STATUS     | \
	ACPI_BITMASK_GLOBAL_LOCK_STATUS    | \
	ACPI_BITMASK_POWER_BUTTON_STATUS   | \
	ACPI_BITMASK_SLEEP_BUTTON_STATUS   | \
	ACPI_BITMASK_RT_CLOCK_STATUS       | \
	ACPI_BITMASK_WAKE_STATUS)

/* PM1x_EN */
#define ACPI_BITMASK_TIMER_ENABLE               0x0001
#define ACPI_BITMASK_GLOBAL_LOCK_ENABLE         0x0020
#define ACPI_BITMASK_POWER_BUTTON_ENABLE        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_ENABLE        0x0200
#define ACPI_BITMASK_RT_CLOCK_ENABLE            0x0400
#define ACPI_BITMASK_PCIEXP_WAKE_DISABLE        0x4000	/* ACPI 3.0 */

/* PM1x_CNT */
#define ACPI_BITMASK_SCI_ENABLE                 0x0001
#define ACPI_BITMASK_BUS_MASTER_RLD             0x0002
#define ACPI_BITMASK_GLOBAL_LOCK_RELEASE        0x0004
#define ACPI_BITMASK_SLEEP_TYPE                 0x1C00
#define ACPI_BITMASK_SLEEP_ENABLE               0x2000

/* PM2_CNT */
#define ACPI_BITMASK_ARB_DISABLE                0x0001

/* PM_TMR */

#endif /* !QEMU_HW_ACPI_H */
