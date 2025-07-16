/*
 * QEMU target info API (returning native types)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_H
#define QEMU_TARGET_INFO_H

/**
 * target_name:
 *
 * Returns: Canonical target name (i.e. "i386").
 */
const char *target_name(void);

/**
 * target_long_bits:
 *
 * Returns: number of bits in a long type for this target (i.e. 64).
 */
unsigned target_long_bits(void);

/**
 * target_machine_typename:
 *
 * Returns: Name of the QOM interface implemented by machines
 *          usable on this target binary.
 */
const char *target_machine_typename(void);

/**
 * target_cpu_type:
 *
 * Returns: target CPU base QOM type name (i.e. TYPE_X86_CPU).
 */
const char *target_cpu_type(void);

/**
 * target_big_endian:
 *
 * Returns: %true if the (default) endianness of the target is big endian,
 *          %false otherwise.
 *
 * Common code should normally never need to know about the endianness of
 * the target, so please do *not* use this function unless you  know very
 * well what you are doing!
 */
bool target_big_endian(void);

#endif
