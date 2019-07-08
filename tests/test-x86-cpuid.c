/*
 *  Test code for x86 CPUID and Topology functions
 *
 *  Copyright (c) 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "hw/i386/topology.h"

static void test_topo_bits(void)
{
    /* simple tests for 1 thread per core, 1 core per die, 1 die per package */
    g_assert_cmpuint(apicid_smt_width(1, 1, 1), ==, 0);
    g_assert_cmpuint(apicid_core_width(1, 1, 1), ==, 0);
    g_assert_cmpuint(apicid_die_width(1, 1, 1), ==, 0);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 1, 1, 0), ==, 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 1, 1, 1), ==, 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 1, 1, 2), ==, 2);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 1, 1, 3), ==, 3);


    /* Test field width calculation for multiple values
     */
    g_assert_cmpuint(apicid_smt_width(1, 1, 2), ==, 1);
    g_assert_cmpuint(apicid_smt_width(1, 1, 3), ==, 2);
    g_assert_cmpuint(apicid_smt_width(1, 1, 4), ==, 2);

    g_assert_cmpuint(apicid_smt_width(1, 1, 14), ==, 4);
    g_assert_cmpuint(apicid_smt_width(1, 1, 15), ==, 4);
    g_assert_cmpuint(apicid_smt_width(1, 1, 16), ==, 4);
    g_assert_cmpuint(apicid_smt_width(1, 1, 17), ==, 5);


    g_assert_cmpuint(apicid_core_width(1, 30, 2), ==, 5);
    g_assert_cmpuint(apicid_core_width(1, 31, 2), ==, 5);
    g_assert_cmpuint(apicid_core_width(1, 32, 2), ==, 5);
    g_assert_cmpuint(apicid_core_width(1, 33, 2), ==, 6);

    g_assert_cmpuint(apicid_die_width(1, 30, 2), ==, 0);
    g_assert_cmpuint(apicid_die_width(2, 30, 2), ==, 1);
    g_assert_cmpuint(apicid_die_width(3, 30, 2), ==, 2);
    g_assert_cmpuint(apicid_die_width(4, 30, 2), ==, 2);

    /* build a weird topology and see if IDs are calculated correctly
     */

    /* This will use 2 bits for thread ID and 3 bits for core ID
     */
    g_assert_cmpuint(apicid_smt_width(1, 6, 3), ==, 2);
    g_assert_cmpuint(apicid_core_offset(1, 6, 3), ==, 2);
    g_assert_cmpuint(apicid_die_offset(1, 6, 3), ==, 5);
    g_assert_cmpuint(apicid_pkg_offset(1, 6, 3), ==, 5);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 0), ==, 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 1), ==, 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 2), ==, 2);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 1 * 3 + 0), ==,
                     (1 << 2) | 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 1 * 3 + 1), ==,
                     (1 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 1 * 3 + 2), ==,
                     (1 << 2) | 2);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 2 * 3 + 0), ==,
                     (2 << 2) | 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 2 * 3 + 1), ==,
                     (2 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 2 * 3 + 2), ==,
                     (2 << 2) | 2);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 5 * 3 + 0), ==,
                     (5 << 2) | 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 5 * 3 + 1), ==,
                     (5 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3, 5 * 3 + 2), ==,
                     (5 << 2) | 2);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3,
                     1 * 6 * 3 + 0 * 3 + 0), ==, (1 << 5));
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3,
                     1 * 6 * 3 + 1 * 3 + 1), ==, (1 << 5) | (1 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(1, 6, 3,
                     3 * 6 * 3 + 5 * 3 + 2), ==, (3 << 5) | (5 << 2) | 2);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/cpuid/topology/basic", test_topo_bits);

    g_test_run();

    return 0;
}
