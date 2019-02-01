/*
 *  QEMU model of the Milkymist texture mapping unit.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *  Copyright (c) 2010 Sebastien Bourdeauducq
 *                       <sebastien.bourdeauducq@lekernel.net>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://milkymist.walle.cc/socdoc/tmu2.pdf
 *
 */

#ifndef HW_DISPLAY_MILKYMIST_TMU2_H
#define HW_DISPLAY_MILKYMIST_TMU2_H

#include "hw/qdev.h"

#if defined(CONFIG_X11) && defined(CONFIG_OPENGL)
DeviceState *milkymist_tmu2_create(hwaddr base, qemu_irq irq);
#else
static inline DeviceState *milkymist_tmu2_create(hwaddr base, qemu_irq irq)
{
    return NULL;
}
#endif

#endif /* HW_DISPLAY_MILKYMIST_TMU2_H */
