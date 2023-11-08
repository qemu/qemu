/*
 * QEMU RX CPU QOM header (target agnostic)
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

#define TYPE_RX_CPU "rx-cpu"

#define TYPE_RX62N_CPU RX_CPU_TYPE_NAME("rx62n")

OBJECT_DECLARE_CPU_TYPE(RXCPU, RXCPUClass, RX_CPU)

#define RX_CPU_TYPE_SUFFIX "-" TYPE_RX_CPU
#define RX_CPU_TYPE_NAME(model) model RX_CPU_TYPE_SUFFIX

#endif
