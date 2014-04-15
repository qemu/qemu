/*
 * QEMU ARM CPU -- internal functions and types
 *
 * Copyright (c) 2014 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * This header defines functions, types, etc which need to be shared
 * between different source files within target-arm/ but which are
 * private to it and not required by the rest of QEMU.
 */

#ifndef TARGET_ARM_INTERNALS_H
#define TARGET_ARM_INTERNALS_H

/* Scale factor for generic timers, ie number of ns per tick.
 * This gives a 62.5MHz timer.
 */
#define GTIMER_SCALE 16

int bank_number(int mode);
void switch_mode(CPUARMState *, int);
void arm_cpu_register_gdb_regs_for_features(ARMCPU *cpu);
void arm_translate_init(void);

enum arm_fprounding {
    FPROUNDING_TIEEVEN,
    FPROUNDING_POSINF,
    FPROUNDING_NEGINF,
    FPROUNDING_ZERO,
    FPROUNDING_TIEAWAY,
    FPROUNDING_ODD
};

int arm_rmode_to_sf(int rmode);

#endif
