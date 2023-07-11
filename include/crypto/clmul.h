/*
 * Carry-less multiply operations.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef CRYPTO_CLMUL_H
#define CRYPTO_CLMUL_H

/**
 * clmul_8x8_low:
 *
 * Perform eight 8x8->8 carry-less multiplies.
 */
uint64_t clmul_8x8_low(uint64_t, uint64_t);

/**
 * clmul_8x4_even:
 *
 * Perform four 8x8->16 carry-less multiplies.
 * The odd bytes of the inputs are ignored.
 */
uint64_t clmul_8x4_even(uint64_t, uint64_t);

/**
 * clmul_8x4_odd:
 *
 * Perform four 8x8->16 carry-less multiplies.
 * The even bytes of the inputs are ignored.
 */
uint64_t clmul_8x4_odd(uint64_t, uint64_t);

/**
 * clmul_8x4_packed:
 *
 * Perform four 8x8->16 carry-less multiplies.
 */
uint64_t clmul_8x4_packed(uint32_t, uint32_t);

/**
 * clmul_16x2_even:
 *
 * Perform two 16x16->32 carry-less multiplies.
 * The odd words of the inputs are ignored.
 */
uint64_t clmul_16x2_even(uint64_t, uint64_t);

/**
 * clmul_16x2_odd:
 *
 * Perform two 16x16->32 carry-less multiplies.
 * The even words of the inputs are ignored.
 */
uint64_t clmul_16x2_odd(uint64_t, uint64_t);

#endif /* CRYPTO_CLMUL_H */
