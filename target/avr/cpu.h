/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016-2020 Michael Rolnik
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef QEMU_AVR_CPU_H
#define QEMU_AVR_CPU_H

#include "exec/cpu-defs.h"

#define TCG_GUEST_DEFAULT_MO 0

/*
 * AVR has two memory spaces, data & code.
 * e.g. both have 0 address
 * ST/LD instructions access data space
 * LPM/SPM and instruction fetching access code memory space
 */
#define MMU_CODE_IDX 0
#define MMU_DATA_IDX 1

#define EXCP_RESET 1
#define EXCP_INT(n) (EXCP_RESET + (n) + 1)

/* Number of CPU registers */
#define NUMBER_OF_CPU_REGISTERS 32
/* Number of IO registers accessible by ld/st/in/out */
#define NUMBER_OF_IO_REGISTERS 64

/*
 * Offsets of AVR memory regions in host memory space.
 *
 * This is needed because the AVR has separate code and data address
 * spaces that both have start from zero but have to go somewhere in
 * host memory.
 *
 * It's also useful to know where some things are, like the IO registers.
 */
/* Flash program memory */
#define OFFSET_CODE 0x00000000
/* CPU registers, IO registers, and SRAM */
#define OFFSET_DATA 0x00800000
/* CPU registers specifically, these are mapped at the start of data */
#define OFFSET_CPU_REGISTERS OFFSET_DATA
/*
 * IO registers, including status register, stack pointer, and memory
 * mapped peripherals, mapped just after CPU registers
 */
#define OFFSET_IO_REGISTERS (OFFSET_DATA + NUMBER_OF_CPU_REGISTERS)

#endif /* !defined (QEMU_AVR_CPU_H) */
