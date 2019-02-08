/*
 * Broadcom BCM283x ARM timer variant based on ARM SP804
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_TIMER_BCM2835_TIMER_H
#define HW_TIMER_BCM2835_TIMER_H

#include "hw/sysbus.h"

#define TYPE_BCM283xSP804 "bcm283xsp804"
#define BCM283xSP804(obj) OBJECT_CHECK(BCM283xSP804State, (obj), TYPE_BCM283xSP804)

typedef struct bcm283x_timer_state bcm283x_timer_state;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    bcm283x_timer_state *timer;
    uint32_t freq0;
    int level;
    qemu_irq irq;
} BCM283xSP804State;

#endif
