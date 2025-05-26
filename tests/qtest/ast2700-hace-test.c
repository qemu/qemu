/*
 * QTest testcase for the ASPEED Hash and Crypto Engine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025 ASPEED Technology Inc.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "aspeed-hace-utils.h"

static const struct AspeedMasks as2700_masks = {
    .src  = 0x7fffffff,
    .dest = 0x7ffffff8,
    .key = 0x7ffffff8,
    .len  = 0x0fffffff,
    .src_hi  = 0x00000003,
    .dest_hi = 0x00000003,
    .key_hi = 0x00000003,
};

/* ast2700 */
static void test_md5_ast2700(void)
{
    aspeed_test_md5("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha256_ast2700(void)
{
    aspeed_test_sha256("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha256_sg_ast2700(void)
{
    aspeed_test_sha256_sg("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha384_ast2700(void)
{
    aspeed_test_sha384("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha384_sg_ast2700(void)
{
    aspeed_test_sha384_sg("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha512_ast2700(void)
{
    aspeed_test_sha512("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha512_sg_ast2700(void)
{
    aspeed_test_sha512_sg("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha256_accum_ast2700(void)
{
    aspeed_test_sha256_accum("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha384_accum_ast2700(void)
{
    aspeed_test_sha384_accum("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_sha512_accum_ast2700(void)
{
    aspeed_test_sha512_accum("-machine ast2700a1-evb", 0x12070000, 0x400000000);
}

static void test_addresses_ast2700(void)
{
    aspeed_test_addresses("-machine ast2700a1-evb", 0x12070000, &as2700_masks);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("ast2700/hace/addresses", test_addresses_ast2700);
    qtest_add_func("ast2700/hace/sha512", test_sha512_ast2700);
    qtest_add_func("ast2700/hace/sha384", test_sha384_ast2700);
    qtest_add_func("ast2700/hace/sha256", test_sha256_ast2700);
    qtest_add_func("ast2700/hace/md5", test_md5_ast2700);

    qtest_add_func("ast2700/hace/sha512_sg", test_sha512_sg_ast2700);
    qtest_add_func("ast2700/hace/sha384_sg", test_sha384_sg_ast2700);
    qtest_add_func("ast2700/hace/sha256_sg", test_sha256_sg_ast2700);

    qtest_add_func("ast2700/hace/sha512_accum", test_sha512_accum_ast2700);
    qtest_add_func("ast2700/hace/sha384_accum", test_sha384_accum_ast2700);
    qtest_add_func("ast2700/hace/sha256_accum", test_sha256_accum_ast2700);

    return g_test_run();
}
