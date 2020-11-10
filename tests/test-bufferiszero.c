/*
 * QEMU buffer_is_zero test
 *
 * Copyright (c) 2016 Red Hat, Inc.
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
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"

static char buffer[8 * 1024 * 1024];

static void test_1(void)
{
    size_t s, a, o;

    /* Basic positive test.  */
    g_assert(buffer_is_zero(buffer, sizeof(buffer)));

    /* Basic negative test.  */
    buffer[sizeof(buffer) - 1] = 1;
    g_assert(!buffer_is_zero(buffer, sizeof(buffer)));
    buffer[sizeof(buffer) - 1] = 0;

    /* Positive tests for size and alignment.  */
    for (a = 1; a <= 64; a++) {
        for (s = 1; s < 1024; s++) {
            buffer[a - 1] = 1;
            buffer[a + s] = 1;
            g_assert(buffer_is_zero(buffer + a, s));
            buffer[a - 1] = 0;
            buffer[a + s] = 0;
        }
    }

    /* Negative tests for size, alignment, and the offset of the marker.  */
    for (a = 1; a <= 64; a++) {
        for (s = 1; s < 1024; s++) {
            for (o = 0; o < s; ++o) {
                buffer[a + o] = 1;
                g_assert(!buffer_is_zero(buffer + a, s));
                buffer[a + o] = 0;
            }
        }
    }
}

static void test_2(void)
{
    if (g_test_perf()) {
        test_1();
    } else {
        do {
            test_1();
        } while (test_buffer_is_zero_next_accel());
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/cutils/bufferiszero", test_2);

    return g_test_run();
}
