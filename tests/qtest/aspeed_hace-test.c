/*
 * QTest testcase for the ASPEED Hash and Crypto Engine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 IBM Corp.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "aspeed-hace-utils.h"

static const struct AspeedMasks ast2600_masks = {
    .src  = 0x7fffffff,
    .dest = 0x7ffffff8,
    .len  = 0x0fffffff,
};

static const struct AspeedMasks ast2500_masks = {
    .src  = 0x3fffffff,
    .dest = 0x3ffffff8,
    .len  = 0x0fffffff,
};

static const struct AspeedMasks ast2400_masks = {
    .src  = 0x0fffffff,
    .dest = 0x0ffffff8,
    .len  = 0x0fffffff,
};

/* ast2600 */
static void test_md5_ast2600(void)
{
    aspeed_test_md5("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha256_ast2600(void)
{
    aspeed_test_sha256("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha256_sg_ast2600(void)
{
    aspeed_test_sha256_sg("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha512_ast2600(void)
{
    aspeed_test_sha512("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha512_sg_ast2600(void)
{
    aspeed_test_sha512_sg("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha256_accum_ast2600(void)
{
    aspeed_test_sha256_accum("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha512_accum_ast2600(void)
{
    aspeed_test_sha512_accum("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_addresses_ast2600(void)
{
    aspeed_test_addresses("-machine ast2600-evb", 0x1e6d0000, &ast2600_masks);
}

/* ast2500 */
static void test_md5_ast2500(void)
{
    aspeed_test_md5("-machine ast2500-evb", 0x1e6e3000, 0x80000000);
}

static void test_sha256_ast2500(void)
{
    aspeed_test_sha256("-machine ast2500-evb", 0x1e6e3000, 0x80000000);
}

static void test_sha512_ast2500(void)
{
    aspeed_test_sha512("-machine ast2500-evb", 0x1e6e3000, 0x80000000);
}

static void test_addresses_ast2500(void)
{
    aspeed_test_addresses("-machine ast2500-evb", 0x1e6e3000, &ast2500_masks);
}

/* ast2400 */
static void test_md5_ast2400(void)
{
    aspeed_test_md5("-machine palmetto-bmc", 0x1e6e3000, 0x40000000);
}

static void test_sha256_ast2400(void)
{
    aspeed_test_sha256("-machine palmetto-bmc", 0x1e6e3000, 0x40000000);
}

static void test_sha512_ast2400(void)
{
    aspeed_test_sha512("-machine palmetto-bmc", 0x1e6e3000, 0x40000000);
}

static void test_addresses_ast2400(void)
{
    aspeed_test_addresses("-machine palmetto-bmc", 0x1e6e3000, &ast2400_masks);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("ast2600/hace/addresses", test_addresses_ast2600);
    qtest_add_func("ast2600/hace/sha512", test_sha512_ast2600);
    qtest_add_func("ast2600/hace/sha256", test_sha256_ast2600);
    qtest_add_func("ast2600/hace/md5", test_md5_ast2600);

    qtest_add_func("ast2600/hace/sha512_sg", test_sha512_sg_ast2600);
    qtest_add_func("ast2600/hace/sha256_sg", test_sha256_sg_ast2600);

    qtest_add_func("ast2600/hace/sha512_accum", test_sha512_accum_ast2600);
    qtest_add_func("ast2600/hace/sha256_accum", test_sha256_accum_ast2600);

    qtest_add_func("ast2500/hace/addresses", test_addresses_ast2500);
    qtest_add_func("ast2500/hace/sha512", test_sha512_ast2500);
    qtest_add_func("ast2500/hace/sha256", test_sha256_ast2500);
    qtest_add_func("ast2500/hace/md5", test_md5_ast2500);

    qtest_add_func("ast2400/hace/addresses", test_addresses_ast2400);
    qtest_add_func("ast2400/hace/sha512", test_sha512_ast2400);
    qtest_add_func("ast2400/hace/sha256", test_sha256_ast2400);
    qtest_add_func("ast2400/hace/md5", test_md5_ast2400);

    return g_test_run();
}
