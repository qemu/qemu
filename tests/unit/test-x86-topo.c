/*
 *  Test code for x86 APIC ID and Topology functions
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
    X86CPUTopoInfo topo_info = {0};

    /* simple tests for 1 thread per core, 1 core per die, 1 die per package */
    topo_info = (X86CPUTopoInfo) {1, 1, 1};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 0);
    g_assert_cmpuint(apicid_core_width(&topo_info), ==, 0);
    g_assert_cmpuint(apicid_die_width(&topo_info), ==, 0);

    topo_info = (X86CPUTopoInfo) {1, 1, 1};
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 0), ==, 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 1), ==, 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 2), ==, 2);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 3), ==, 3);


    /* Test field width calculation for multiple values
     */
    topo_info = (X86CPUTopoInfo) {1, 1, 2};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 1);
    topo_info = (X86CPUTopoInfo) {1, 1, 3};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 2);
    topo_info = (X86CPUTopoInfo) {1, 1, 4};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 2);

    topo_info = (X86CPUTopoInfo) {1, 1, 14};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 4);
    topo_info = (X86CPUTopoInfo) {1, 1, 15};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 4);
    topo_info = (X86CPUTopoInfo) {1, 1, 16};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 4);
    topo_info = (X86CPUTopoInfo) {1, 1, 17};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 5);


    topo_info = (X86CPUTopoInfo) {1, 30, 2};
    g_assert_cmpuint(apicid_core_width(&topo_info), ==, 5);
    topo_info = (X86CPUTopoInfo) {1, 31, 2};
    g_assert_cmpuint(apicid_core_width(&topo_info), ==, 5);
    topo_info = (X86CPUTopoInfo) {1, 32, 2};
    g_assert_cmpuint(apicid_core_width(&topo_info), ==, 5);
    topo_info = (X86CPUTopoInfo) {1, 33, 2};
    g_assert_cmpuint(apicid_core_width(&topo_info), ==, 6);

    topo_info = (X86CPUTopoInfo) {1, 30, 2};
    g_assert_cmpuint(apicid_die_width(&topo_info), ==, 0);
    topo_info = (X86CPUTopoInfo) {2, 30, 2};
    g_assert_cmpuint(apicid_die_width(&topo_info), ==, 1);
    topo_info = (X86CPUTopoInfo) {3, 30, 2};
    g_assert_cmpuint(apicid_die_width(&topo_info), ==, 2);
    topo_info = (X86CPUTopoInfo) {4, 30, 2};
    g_assert_cmpuint(apicid_die_width(&topo_info), ==, 2);

    /* build a weird topology and see if IDs are calculated correctly
     */

    /* This will use 2 bits for thread ID and 3 bits for core ID
     */
    topo_info = (X86CPUTopoInfo) {1, 6, 3};
    g_assert_cmpuint(apicid_smt_width(&topo_info), ==, 2);
    g_assert_cmpuint(apicid_core_offset(&topo_info), ==, 2);
    g_assert_cmpuint(apicid_die_offset(&topo_info), ==, 5);
    g_assert_cmpuint(apicid_pkg_offset(&topo_info), ==, 5);

    topo_info = (X86CPUTopoInfo) {1, 6, 3};
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 0), ==, 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 1), ==, 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 2), ==, 2);

    topo_info = (X86CPUTopoInfo) {1, 6, 3};
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 1 * 3 + 0), ==,
                     (1 << 2) | 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 1 * 3 + 1), ==,
                     (1 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 1 * 3 + 2), ==,
                     (1 << 2) | 2);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 2 * 3 + 0), ==,
                     (2 << 2) | 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 2 * 3 + 1), ==,
                     (2 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 2 * 3 + 2), ==,
                     (2 << 2) | 2);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 5 * 3 + 0), ==,
                     (5 << 2) | 0);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 5 * 3 + 1), ==,
                     (5 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info, 5 * 3 + 2), ==,
                     (5 << 2) | 2);

    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info,
                     1 * 6 * 3 + 0 * 3 + 0), ==, (1 << 5));
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info,
                     1 * 6 * 3 + 1 * 3 + 1), ==, (1 << 5) | (1 << 2) | 1);
    g_assert_cmpuint(x86_apicid_from_cpu_idx(&topo_info,
                     3 * 6 * 3 + 5 * 3 + 2), ==, (3 << 5) | (5 << 2) | 2);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/cpuid/topology/basic", test_topo_bits);

    g_test_run();

    return 0;
}
