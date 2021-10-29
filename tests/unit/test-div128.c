/*
 * Test 128-bit division functions
 *
 * Copyright (c) 2021 Instituto de Pesquisas Eldorado (eldorado.org.br)
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"

typedef struct {
    uint64_t high;
    uint64_t low;
    uint64_t rhigh;
    uint64_t rlow;
    uint64_t divisor;
    uint64_t remainder;
} test_data_unsigned;

typedef struct {
    int64_t high;
    uint64_t low;
    int64_t rhigh;
    uint64_t rlow;
    int64_t divisor;
    int64_t remainder;
} test_data_signed;

static const test_data_unsigned test_table_unsigned[] = {
    /* Dividend fits in 64 bits */
    { 0x0000000000000000ULL, 0x0000000000000000ULL,
      0x0000000000000000ULL, 0x0000000000000000ULL,
      0x0000000000000001ULL, 0x0000000000000000ULL},
    { 0x0000000000000000ULL, 0x0000000000000001ULL,
      0x0000000000000000ULL, 0x0000000000000001ULL,
      0x0000000000000001ULL, 0x0000000000000000ULL},
    { 0x0000000000000000ULL, 0x0000000000000003ULL,
      0x0000000000000000ULL, 0x0000000000000001ULL,
      0x0000000000000002ULL, 0x0000000000000001ULL},
    { 0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000001ULL, 0x0000000000000000ULL},
    { 0x0000000000000000ULL, 0xa000000000000000ULL,
      0x0000000000000000ULL, 0x0000000000000002ULL,
      0x4000000000000000ULL, 0x2000000000000000ULL},
    { 0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000000ULL, 0x0000000000000001ULL,
      0x8000000000000000ULL, 0x0000000000000000ULL},

    /* Dividend > 64 bits, with MSB 0 */
    { 0x123456789abcdefeULL, 0xefedcba987654321ULL,
      0x123456789abcdefeULL, 0xefedcba987654321ULL,
      0x0000000000000001ULL, 0x0000000000000000ULL},
    { 0x123456789abcdefeULL, 0xefedcba987654321ULL,
      0x0000000000000001ULL, 0x000000000000000dULL,
      0x123456789abcdefeULL, 0x03456789abcdf03bULL},
    { 0x123456789abcdefeULL, 0xefedcba987654321ULL,
      0x0123456789abcdefULL, 0xeefedcba98765432ULL,
      0x0000000000000010ULL, 0x0000000000000001ULL},

    /* Dividend > 64 bits, with MSB 1 */
    { 0xfeeddccbbaa99887ULL, 0x766554433221100fULL,
      0xfeeddccbbaa99887ULL, 0x766554433221100fULL,
      0x0000000000000001ULL, 0x0000000000000000ULL},
    { 0xfeeddccbbaa99887ULL, 0x766554433221100fULL,
      0x0000000000000001ULL, 0x0000000000000000ULL,
      0xfeeddccbbaa99887ULL, 0x766554433221100fULL},
    { 0xfeeddccbbaa99887ULL, 0x766554433221100fULL,
      0x0feeddccbbaa9988ULL, 0x7766554433221100ULL,
      0x0000000000000010ULL, 0x000000000000000fULL},
    { 0xfeeddccbbaa99887ULL, 0x766554433221100fULL,
      0x000000000000000eULL, 0x00f0f0f0f0f0f35aULL,
      0x123456789abcdefeULL, 0x0f8922bc55ef90c3ULL},

    /**
     * Divisor == 64 bits, with MSB 1
     * and high 64 bits of dividend >= divisor
     * (for testing normalization)
     */
    { 0xfeeddccbbaa99887ULL, 0x766554433221100fULL,
      0x0000000000000001ULL, 0x0000000000000000ULL,
      0xfeeddccbbaa99887ULL, 0x766554433221100fULL},
    { 0xfeeddccbbaa99887ULL, 0x766554433221100fULL,
      0x0000000000000001ULL, 0xfddbb9977553310aULL,
      0x8000000000000001ULL, 0x78899aabbccddf05ULL},

    /* Dividend > 64 bits, divisor almost as big */
    { 0x0000000000000001ULL, 0x23456789abcdef01ULL,
      0x0000000000000000ULL, 0x000000000000000fULL,
      0x123456789abcdefeULL, 0x123456789abcde1fULL},
};

static const test_data_signed test_table_signed[] = {
    /* Positive dividend, positive/negative divisors */
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0x0000000000000000LL, 0x0000000000bc614eULL,
      0x0000000000000001LL, 0x0000000000000000LL},
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0xffffffffffffffffLL, 0x0000000000000000LL},
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0x0000000000000000LL, 0x00000000005e30a7ULL,
      0x0000000000000002LL, 0x0000000000000000LL},
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0xffffffffffffffffLL, 0xffffffffffa1cf59ULL,
      0xfffffffffffffffeLL, 0x0000000000000000LL},
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0x0000000000000000LL, 0x0000000000178c29ULL,
      0x0000000000000008LL, 0x0000000000000006LL},
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0xffffffffffffffffLL, 0xffffffffffe873d7ULL,
      0xfffffffffffffff8LL, 0x0000000000000006LL},
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0x0000000000000000LL, 0x000000000000550dULL,
      0x0000000000000237LL, 0x0000000000000183LL},
    { 0x0000000000000000LL, 0x0000000000bc614eULL,
      0xffffffffffffffffLL, 0xffffffffffffaaf3ULL,
      0xfffffffffffffdc9LL, 0x0000000000000183LL},

    /* Negative dividend, positive/negative divisors */
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0x0000000000000001LL, 0x0000000000000000LL},
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0x0000000000000000LL, 0x0000000000bc614eULL,
      0xffffffffffffffffLL, 0x0000000000000000LL},
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0xffffffffffffffffLL, 0xffffffffffa1cf59ULL,
      0x0000000000000002LL, 0x0000000000000000LL},
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0x0000000000000000LL, 0x00000000005e30a7ULL,
      0xfffffffffffffffeLL, 0x0000000000000000LL},
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0xffffffffffffffffLL, 0xffffffffffe873d7ULL,
      0x0000000000000008LL, 0xfffffffffffffffaLL},
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0x0000000000000000LL, 0x0000000000178c29ULL,
      0xfffffffffffffff8LL, 0xfffffffffffffffaLL},
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0xffffffffffffffffLL, 0xffffffffffffaaf3ULL,
      0x0000000000000237LL, 0xfffffffffffffe7dLL},
    { 0xffffffffffffffffLL, 0xffffffffff439eb2ULL,
      0x0000000000000000LL, 0x000000000000550dULL,
      0xfffffffffffffdc9LL, 0xfffffffffffffe7dLL},
};

static void test_divu128(void)
{
    int i;
    uint64_t rem;
    test_data_unsigned tmp;

    for (i = 0; i < ARRAY_SIZE(test_table_unsigned); ++i) {
        tmp = test_table_unsigned[i];

        rem = divu128(&tmp.low, &tmp.high, tmp.divisor);
        g_assert_cmpuint(tmp.low, ==, tmp.rlow);
        g_assert_cmpuint(tmp.high, ==, tmp.rhigh);
        g_assert_cmpuint(rem, ==, tmp.remainder);
    }
}

static void test_divs128(void)
{
    int i;
    int64_t rem;
    test_data_signed tmp;

    for (i = 0; i < ARRAY_SIZE(test_table_signed); ++i) {
        tmp = test_table_signed[i];

        rem = divs128(&tmp.low, &tmp.high, tmp.divisor);
        g_assert_cmpuint(tmp.low, ==, tmp.rlow);
        g_assert_cmpuint(tmp.high, ==, tmp.rhigh);
        g_assert_cmpuint(rem, ==, tmp.remainder);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/host-utils/test_divu128", test_divu128);
    g_test_add_func("/host-utils/test_divs128", test_divs128);
    return g_test_run();
}
