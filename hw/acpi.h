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
struct ACPIPMTimer;
typedef struct ACPIPMTimer ACPIPMTimer;

typedef void (*acpi_update_sci_fn)(ACPIPMTimer *tmr);

struct ACPIPMTimer {
    QEMUTimer *timer;
    int64_t overflow_time;

    acpi_update_sci_fn update_sci;
};

void acpi_pm_tmr_update(ACPIPMTimer *tmr, bool enable);
void acpi_pm_tmr_calc_overflow_time(ACPIPMTimer *tmr);
uint32_t acpi_pm_tmr_get(ACPIPMTimer *tmr);
void acpi_pm_tmr_init(ACPIPMTimer *tmr, acpi_update_sci_fn update_sci);
void acpi_pm_tmr_reset(ACPIPMTimer *tmr);

#include "qemu-timer.h"
static inline int64_t acpi_pm_tmr_get_clock(void)
{
    return muldiv64(qemu_get_clock_ns(vm_clock), PM_TIMER_FREQUENCY,
                    get_ticks_per_sec());
}

/* PM1a_EVT: piix and ich9 don't implement PM1b. */
struct ACPIPM1EVT
{
    uint16_t sts;
    uint16_t en;
};
typedef struct ACPIPM1EVT ACPIPM1EVT;

uint16_t acpi_pm1_evt_get_sts(ACPIPM1EVT *pm1, int64_t overflow_time);
void acpi_pm1_evt_write_sts(ACPIPM1EVT *pm1, ACPIPMTimer *tmr, uint16_t val);
void acpi_pm1_evt_power_down(ACPIPM1EVT *pm1, ACPIPMTimer *tmr);
void acpi_pm1_evt_reset(ACPIPM1EVT *pm1);

/* PM1a_CNT: piix and ich9 don't implement PM1b CNT. */
struct ACPIPM1CNT {
    uint16_t cnt;

    qemu_irq cmos_s3;
};
typedef struct ACPIPM1CNT ACPIPM1CNT;

void acpi_pm1_cnt_init(ACPIPM1CNT *pm1_cnt, qemu_irq cmos_s3);
void acpi_pm1_cnt_write(ACPIPM1EVT *pm1a, ACPIPM1CNT *pm1_cnt, uint16_t val);
void acpi_pm1_cnt_update(ACPIPM1CNT *pm1_cnt,
                         bool sci_enable, bool sci_disable);
void acpi_pm1_cnt_reset(ACPIPM1CNT *pm1_cnt);

/* GPE0 */
struct ACPIGPE {
    uint32_t blk;
    uint8_t len;

    uint8_t *sts;
    uint8_t *en;
};
typedef struct ACPIGPE ACPIGPE;

void acpi_gpe_init(ACPIGPE *gpe, uint8_t len);
void acpi_gpe_blk(ACPIGPE *gpe, uint32_t blk);
void acpi_gpe_reset(ACPIGPE *gpe);

void acpi_gpe_ioport_writeb(ACPIGPE *gpe, uint32_t addr, uint32_t val);
uint32_t acpi_gpe_ioport_readb(ACPIGPE *gpe, uint32_t addr);

#endif /* !QEMU_HW_ACPI_H */
