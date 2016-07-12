/*
 * i.MX processors GPIO registers definition.
 *
 * Copyright (C) 2015 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IMX_GPIO_H
#define IMX_GPIO_H

#include "hw/sysbus.h"

#define TYPE_IMX_GPIO "imx.gpio"
#define IMX_GPIO(obj) OBJECT_CHECK(IMXGPIOState, (obj), TYPE_IMX_GPIO)

#define IMX_GPIO_MEM_SIZE 0x20

/* i.MX GPIO memory map */
#define DR_ADDR             0x00 /* DATA REGISTER */
#define GDIR_ADDR           0x04 /* DIRECTION REGISTER */
#define PSR_ADDR            0x08 /* PAD STATUS REGISTER */
#define ICR1_ADDR           0x0c /* INTERRUPT CONFIGURATION REGISTER 1 */
#define ICR2_ADDR           0x10 /* INTERRUPT CONFIGURATION REGISTER 2 */
#define IMR_ADDR            0x14 /* INTERRUPT MASK REGISTER */
#define ISR_ADDR            0x18 /* INTERRUPT STATUS REGISTER */
#define EDGE_SEL_ADDR       0x1c /* EDGE SEL REGISTER */

#define IMX_GPIO_PIN_COUNT 32

typedef struct IMXGPIOState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t dr;
    uint32_t gdir;
    uint32_t psr;
    uint64_t icr;
    uint32_t imr;
    uint32_t isr;
    bool has_edge_sel;
    uint32_t edge_sel;
    bool has_upper_pin_irq;

    qemu_irq irq[2];
    qemu_irq output[IMX_GPIO_PIN_COUNT];
} IMXGPIOState;

#endif /* IMX_GPIO_H */
