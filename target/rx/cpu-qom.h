/*
 * RX CPU
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RX_CPU_QOM_H
#define RX_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#define TYPE_RX_CPU "rx-cpu"

#define TYPE_RX62N_CPU RX_CPU_TYPE_NAME("rx62n")

OBJECT_DECLARE_TYPE(RXCPU, RXCPUClass,
                    RX_CPU)

/*
 * RXCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A RX CPU model.
 */
struct RXCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceReset parent_reset;
};

#define CPUArchState struct CPURXState

#endif
