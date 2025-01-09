/*
 * QTest testcase for RISC-V CSRs
 *
 * Copyright (c) 2024 Syntacore.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define CSR_MVENDORID       0xf11
#define CSR_MISELECT        0x350

static void run_test_csr(void)
{
    uint64_t res;
    uint64_t val = 0;

    QTestState *qts = qtest_init("-machine virt -cpu veyron-v1");

    res = qtest_csr_call(qts, "get_csr", 0, CSR_MVENDORID, &val);

    g_assert_cmpint(res, ==, 0);
    g_assert_cmpint(val, ==, 0x61f);

    val = 0xff;
    res = qtest_csr_call(qts, "set_csr", 0, CSR_MISELECT, &val);

    g_assert_cmpint(res, ==, 0);

    val = 0;
    res = qtest_csr_call(qts, "get_csr", 0, CSR_MISELECT, &val);

    g_assert_cmpint(res, ==, 0);
    g_assert_cmpint(val, ==, 0xff);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/cpu/csr", run_test_csr);

    return g_test_run();
}
