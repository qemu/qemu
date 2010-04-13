/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009, 2010 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This code implements a TCG which does not generate machine code for some
 * real target machine but which generates virtual machine code for an
 * interpreter. Interpreted pseudo code is slow, but it works on any host.
 *
 * Some remarks might help in understanding the code:
 *
 * "target" or "TCG target" is the machine which runs the generated code.
 * This is different to the usual meaning in QEMU where "target" is the
 * emulated machine. So normally QEMU host is identical to TCG target.
 * Here the TCG target is a virtual machine, but this virtual machine must
 * use the same word size like the real machine.
 * Therefore, we need both 32 and 64 bit virtual machines (interpreter).
 */

#if !defined(TCG_TARGET_H)
#define TCG_TARGET_H

#include "config-host.h"

#define TCG_TARGET_INTERPRETER 1

#ifdef CONFIG_DEBUG_TCG
/* Enable debug output. */
#define CONFIG_DEBUG_TCG_INTERPRETER
#endif

#if 0 /* TCI tries to emulate a little endian host. */
#if defined(HOST_WORDS_BIGENDIAN)
# define TCG_TARGET_WORDS_BIGENDIAN
#endif
#endif

/* Target word size (must be identical to pointer size). */
#if !defined(HOST_LONG_BITS)
# error HOST_LONG_BITS is undefined
#elif HOST_LONG_BITS == 32
# define TCG_TARGET_REG_BITS 32
#elif HOST_LONG_BITS == 64
# define TCG_TARGET_REG_BITS 64
#else
# error Only 32 or 64 bit long support for host
#endif

/* Optional instructions. */

#define TCG_TARGET_HAS_bswap16_i32
#define TCG_TARGET_HAS_bswap32_i32
/* Define not more than one of the next two defines. */
#define TCG_TARGET_HAS_div_i32
#undef TCG_TARGET_HAS_div2_i32
#define TCG_TARGET_HAS_ext8s_i32
#define TCG_TARGET_HAS_ext16s_i32
#define TCG_TARGET_HAS_ext8u_i32
#define TCG_TARGET_HAS_ext16u_i32
#define TCG_TARGET_HAS_neg_i32
#define TCG_TARGET_HAS_not_i32
#define TCG_TARGET_HAS_rot_i32

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_bswap16_i64
#define TCG_TARGET_HAS_bswap32_i64
#define TCG_TARGET_HAS_bswap64_i64
/* Define not more than one of the next two defines. */
#undef TCG_TARGET_HAS_div_i64
#undef TCG_TARGET_HAS_div2_i64
#define TCG_TARGET_HAS_ext8s_i64
#define TCG_TARGET_HAS_ext16s_i64
#define TCG_TARGET_HAS_ext32s_i64
#define TCG_TARGET_HAS_ext8u_i64
#define TCG_TARGET_HAS_ext16u_i64
#define TCG_TARGET_HAS_ext32u_i64
#define TCG_TARGET_HAS_neg_i64
#define TCG_TARGET_HAS_not_i64
#define TCG_TARGET_HAS_rot_i64
#endif /* TCG_TARGET_REG_BITS == 64 */

#if defined(TCG_TARGET_HAS_div_i32) && defined(TCG_TARGET_HAS_div2_i32)
# error both TCG_TARGET_HAS_div_i32 and TCG_TARGET_HAS_div2_i32 defined
#endif

#if defined(TCG_TARGET_HAS_div_i64) && defined(TCG_TARGET_HAS_div2_i64)
# error both TCG_TARGET_HAS_div_i64 and TCG_TARGET_HAS_div2_i64 defined
#endif

/* Offset to user memory in user mode. */
#define TCG_TARGET_HAS_GUEST_BASE

/* Number of registers available. */
#define TCG_TARGET_NB_REGS 8
//~ #define TCG_TARGET_NB_REGS 16
//~ #define TCG_TARGET_NB_REGS 32

/* List of registers which are used by TCG. */
enum {
    TCG_REG_R0 = 0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_AREG0 = TCG_REG_R7,
#if TCG_TARGET_NB_REGS == 16 || TCG_TARGET_NB_REGS == 32
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
#endif
#if TCG_TARGET_NB_REGS == 32
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_R27,
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31
#endif
};

void tci_disas(uint8_t opc);

#endif /* TCG_TARGET_H */
