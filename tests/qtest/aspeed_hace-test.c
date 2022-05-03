/*
 * QTest testcase for the ASPEED Hash and Crypto Engine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 IBM Corp.
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "qemu/bitops.h"

#define HACE_CMD                 0x10
#define  HACE_SHA_BE_EN          BIT(3)
#define  HACE_MD5_LE_EN          BIT(2)
#define  HACE_ALGO_MD5           0
#define  HACE_ALGO_SHA1          BIT(5)
#define  HACE_ALGO_SHA224        BIT(6)
#define  HACE_ALGO_SHA256        (BIT(4) | BIT(6))
#define  HACE_ALGO_SHA512        (BIT(5) | BIT(6))
#define  HACE_ALGO_SHA384        (BIT(5) | BIT(6) | BIT(10))
#define  HACE_SG_EN              BIT(18)
#define  HACE_ACCUM_EN           BIT(8)

#define HACE_STS                 0x1c
#define  HACE_RSA_ISR            BIT(13)
#define  HACE_CRYPTO_ISR         BIT(12)
#define  HACE_HASH_ISR           BIT(9)
#define  HACE_RSA_BUSY           BIT(2)
#define  HACE_CRYPTO_BUSY        BIT(1)
#define  HACE_HASH_BUSY          BIT(0)
#define HACE_HASH_SRC            0x20
#define HACE_HASH_DIGEST         0x24
#define HACE_HASH_KEY_BUFF       0x28
#define HACE_HASH_DATA_LEN       0x2c
#define HACE_HASH_CMD            0x30
/* Scatter-Gather Hash */
#define SG_LIST_LEN_LAST         BIT(31)
struct AspeedSgList {
        uint32_t len;
        uint32_t addr;
} __attribute__ ((__packed__));

/*
 * Test vector is the ascii "abc"
 *
 * Expected results were generated using command line utitiles:
 *
 *  echo -n -e 'abc' | dd of=/tmp/test
 *  for hash in sha512sum sha256sum md5sum; do $hash /tmp/test; done
 *
 */
static const uint8_t test_vector[] = {0x61, 0x62, 0x63};

static const uint8_t test_result_sha512[] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49,
    0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21, 0x92, 0x99, 0x2a,
    0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f,
    0xa5, 0x4c, 0xa4, 0x9f};

static const uint8_t test_result_sha256[] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

static const uint8_t test_result_md5[] = {
    0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d,
    0x28, 0xe1, 0x7f, 0x72};

/*
 * The Scatter-Gather Test vector is the ascii "abc" "def" "ghi", broken
 * into blocks of 3 characters as shown
 *
 * Expected results were generated using command line utitiles:
 *
 *  echo -n -e 'abcdefghijkl' | dd of=/tmp/test
 *  for hash in sha512sum sha256sum; do $hash /tmp/test; done
 *
 */
static const uint8_t test_vector_sg1[] = {0x61, 0x62, 0x63, 0x64, 0x65, 0x66};
static const uint8_t test_vector_sg2[] = {0x67, 0x68, 0x69};
static const uint8_t test_vector_sg3[] = {0x6a, 0x6b, 0x6c};

static const uint8_t test_result_sg_sha512[] = {
    0x17, 0x80, 0x7c, 0x72, 0x8e, 0xe3, 0xba, 0x35, 0xe7, 0xcf, 0x7a, 0xf8,
    0x23, 0x11, 0x6d, 0x26, 0xe4, 0x1e, 0x5d, 0x4d, 0x6c, 0x2f, 0xf1, 0xf3,
    0x72, 0x0d, 0x3d, 0x96, 0xaa, 0xcb, 0x6f, 0x69, 0xde, 0x64, 0x2e, 0x63,
    0xd5, 0xb7, 0x3f, 0xc3, 0x96, 0xc1, 0x2b, 0xe3, 0x8b, 0x2b, 0xd5, 0xd8,
    0x84, 0x25, 0x7c, 0x32, 0xc8, 0xf6, 0xd0, 0x85, 0x4a, 0xe6, 0xb5, 0x40,
    0xf8, 0x6d, 0xda, 0x2e};

static const uint8_t test_result_sg_sha256[] = {
    0xd6, 0x82, 0xed, 0x4c, 0xa4, 0xd9, 0x89, 0xc1, 0x34, 0xec, 0x94, 0xf1,
    0x55, 0x1e, 0x1e, 0xc5, 0x80, 0xdd, 0x6d, 0x5a, 0x6e, 0xcd, 0xe9, 0xf3,
    0xd3, 0x5e, 0x6e, 0x4a, 0x71, 0x7f, 0xbd, 0xe4};

/*
 * The accumulative mode requires firmware to provide internal initial state
 * and message padding (including length L at the end of padding).
 *
 * This test vector is a ascii text "abc" with padding message.
 *
 * Expected results were generated using command line utitiles:
 *
 *  echo -n -e 'abc' | dd of=/tmp/test
 *  for hash in sha512sum sha256sum; do $hash /tmp/test; done
 */
static const uint8_t test_vector_accum_512[] = {
    0x61, 0x62, 0x63, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18};

static const uint8_t test_vector_accum_256[] = {
    0x61, 0x62, 0x63, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18};

static const uint8_t test_result_accum_sha512[] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49,
    0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21, 0x92, 0x99, 0x2a,
    0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f,
    0xa5, 0x4c, 0xa4, 0x9f};

static const uint8_t test_result_accum_sha256[] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

static void write_regs(QTestState *s, uint32_t base, uint32_t src,
                       uint32_t length, uint32_t out, uint32_t method)
{
        qtest_writel(s, base + HACE_HASH_SRC, src);
        qtest_writel(s, base + HACE_HASH_DIGEST, out);
        qtest_writel(s, base + HACE_HASH_DATA_LEN, length);
        qtest_writel(s, base + HACE_HASH_CMD, HACE_SHA_BE_EN | method);
}

static void test_md5(const char *machine, const uint32_t base,
                     const uint32_t src_addr)

{
    QTestState *s = qtest_init(machine);

    uint32_t digest_addr = src_addr + 0x01000000;
    uint8_t digest[16] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, base, src_addr, sizeof(test_vector), digest_addr, HACE_ALGO_MD5);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_md5, sizeof(digest));

    qtest_quit(s);
}

static void test_sha256(const char *machine, const uint32_t base,
                        const uint32_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint32_t digest_addr = src_addr + 0x1000000;
    uint8_t digest[32] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, base, src_addr, sizeof(test_vector), digest_addr, HACE_ALGO_SHA256);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sha256, sizeof(digest));

    qtest_quit(s);
}

static void test_sha512(const char *machine, const uint32_t base,
                        const uint32_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint32_t digest_addr = src_addr + 0x1000000;
    uint8_t digest[64] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, base, src_addr, sizeof(test_vector), digest_addr, HACE_ALGO_SHA512);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sha512, sizeof(digest));

    qtest_quit(s);
}

static void test_sha256_sg(const char *machine, const uint32_t base,
                        const uint32_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint32_t src_addr_1 = src_addr + 0x1000000;
    const uint32_t src_addr_2 = src_addr + 0x2000000;
    const uint32_t src_addr_3 = src_addr + 0x3000000;
    const uint32_t digest_addr = src_addr + 0x4000000;
    uint8_t digest[32] = {0};
    struct AspeedSgList array[] = {
        {  cpu_to_le32(sizeof(test_vector_sg1)),
           cpu_to_le32(src_addr_1) },
        {  cpu_to_le32(sizeof(test_vector_sg2)),
           cpu_to_le32(src_addr_2) },
        {  cpu_to_le32(sizeof(test_vector_sg3) | SG_LIST_LEN_LAST),
           cpu_to_le32(src_addr_3) },
    };

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr_1, test_vector_sg1, sizeof(test_vector_sg1));
    qtest_memwrite(s, src_addr_2, test_vector_sg2, sizeof(test_vector_sg2));
    qtest_memwrite(s, src_addr_3, test_vector_sg3, sizeof(test_vector_sg3));
    qtest_memwrite(s, src_addr, array, sizeof(array));

    write_regs(s, base, src_addr,
               (sizeof(test_vector_sg1)
                + sizeof(test_vector_sg2)
                + sizeof(test_vector_sg3)),
               digest_addr, HACE_ALGO_SHA256 | HACE_SG_EN);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sg_sha256, sizeof(digest));

    qtest_quit(s);
}

static void test_sha512_sg(const char *machine, const uint32_t base,
                        const uint32_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint32_t src_addr_1 = src_addr + 0x1000000;
    const uint32_t src_addr_2 = src_addr + 0x2000000;
    const uint32_t src_addr_3 = src_addr + 0x3000000;
    const uint32_t digest_addr = src_addr + 0x4000000;
    uint8_t digest[64] = {0};
    struct AspeedSgList array[] = {
        {  cpu_to_le32(sizeof(test_vector_sg1)),
           cpu_to_le32(src_addr_1) },
        {  cpu_to_le32(sizeof(test_vector_sg2)),
           cpu_to_le32(src_addr_2) },
        {  cpu_to_le32(sizeof(test_vector_sg3) | SG_LIST_LEN_LAST),
           cpu_to_le32(src_addr_3) },
    };

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr_1, test_vector_sg1, sizeof(test_vector_sg1));
    qtest_memwrite(s, src_addr_2, test_vector_sg2, sizeof(test_vector_sg2));
    qtest_memwrite(s, src_addr_3, test_vector_sg3, sizeof(test_vector_sg3));
    qtest_memwrite(s, src_addr, array, sizeof(array));

    write_regs(s, base, src_addr,
               (sizeof(test_vector_sg1)
                + sizeof(test_vector_sg2)
                + sizeof(test_vector_sg3)),
               digest_addr, HACE_ALGO_SHA512 | HACE_SG_EN);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sg_sha512, sizeof(digest));

    qtest_quit(s);
}

static void test_sha256_accum(const char *machine, const uint32_t base,
                        const uint32_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint32_t buffer_addr = src_addr + 0x1000000;
    const uint32_t digest_addr = src_addr + 0x4000000;
    uint8_t digest[32] = {0};
    struct AspeedSgList array[] = {
        {  cpu_to_le32(sizeof(test_vector_accum_256) | SG_LIST_LEN_LAST),
           cpu_to_le32(buffer_addr) },
    };

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, buffer_addr, test_vector_accum_256,
                   sizeof(test_vector_accum_256));
    qtest_memwrite(s, src_addr, array, sizeof(array));

    write_regs(s, base, src_addr, sizeof(test_vector_accum_256),
               digest_addr, HACE_ALGO_SHA256 | HACE_SG_EN | HACE_ACCUM_EN);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_accum_sha256, sizeof(digest));

    qtest_quit(s);
}

static void test_sha512_accum(const char *machine, const uint32_t base,
                        const uint32_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint32_t buffer_addr = src_addr + 0x1000000;
    const uint32_t digest_addr = src_addr + 0x4000000;
    uint8_t digest[64] = {0};
    struct AspeedSgList array[] = {
        {  cpu_to_le32(sizeof(test_vector_accum_512) | SG_LIST_LEN_LAST),
           cpu_to_le32(buffer_addr) },
    };

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, buffer_addr, test_vector_accum_512,
                   sizeof(test_vector_accum_512));
    qtest_memwrite(s, src_addr, array, sizeof(array));

    write_regs(s, base, src_addr, sizeof(test_vector_accum_512),
               digest_addr, HACE_ALGO_SHA512 | HACE_SG_EN | HACE_ACCUM_EN);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_accum_sha512, sizeof(digest));

    qtest_quit(s);
}

struct masks {
    uint32_t src;
    uint32_t dest;
    uint32_t len;
};

static const struct masks ast2600_masks = {
    .src  = 0x7fffffff,
    .dest = 0x7ffffff8,
    .len  = 0x0fffffff,
};

static const struct masks ast2500_masks = {
    .src  = 0x3fffffff,
    .dest = 0x3ffffff8,
    .len  = 0x0fffffff,
};

static const struct masks ast2400_masks = {
    .src  = 0x0fffffff,
    .dest = 0x0ffffff8,
    .len  = 0x0fffffff,
};

static void test_addresses(const char *machine, const uint32_t base,
                           const struct masks *expected)
{
    QTestState *s = qtest_init(machine);

    /*
     * Check command mode is zero, meaning engine is in direct access mode,
     * as this affects the masking behavior of the HASH_SRC register.
     */
    g_assert_cmphex(qtest_readl(s, base + HACE_CMD), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DATA_LEN), ==, 0);


    /* Check that the address masking is correct */
    qtest_writel(s, base + HACE_HASH_SRC, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC), ==, expected->src);

    qtest_writel(s, base + HACE_HASH_DIGEST, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST), ==, expected->dest);

    qtest_writel(s, base + HACE_HASH_DATA_LEN, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DATA_LEN), ==, expected->len);

    /* Reset to zero */
    qtest_writel(s, base + HACE_HASH_SRC, 0);
    qtest_writel(s, base + HACE_HASH_DIGEST, 0);
    qtest_writel(s, base + HACE_HASH_DATA_LEN, 0);

    /* Check that all bits are now zero */
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DATA_LEN), ==, 0);

    qtest_quit(s);
}

/* ast2600 */
static void test_md5_ast2600(void)
{
    test_md5("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha256_ast2600(void)
{
    test_sha256("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha256_sg_ast2600(void)
{
    test_sha256_sg("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha512_ast2600(void)
{
    test_sha512("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha512_sg_ast2600(void)
{
    test_sha512_sg("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha256_accum_ast2600(void)
{
    test_sha256_accum("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_sha512_accum_ast2600(void)
{
    test_sha512_accum("-machine ast2600-evb", 0x1e6d0000, 0x80000000);
}

static void test_addresses_ast2600(void)
{
    test_addresses("-machine ast2600-evb", 0x1e6d0000, &ast2600_masks);
}

/* ast2500 */
static void test_md5_ast2500(void)
{
    test_md5("-machine ast2500-evb", 0x1e6e3000, 0x80000000);
}

static void test_sha256_ast2500(void)
{
    test_sha256("-machine ast2500-evb", 0x1e6e3000, 0x80000000);
}

static void test_sha512_ast2500(void)
{
    test_sha512("-machine ast2500-evb", 0x1e6e3000, 0x80000000);
}

static void test_addresses_ast2500(void)
{
    test_addresses("-machine ast2500-evb", 0x1e6e3000, &ast2500_masks);
}

/* ast2400 */
static void test_md5_ast2400(void)
{
    test_md5("-machine palmetto-bmc", 0x1e6e3000, 0x40000000);
}

static void test_sha256_ast2400(void)
{
    test_sha256("-machine palmetto-bmc", 0x1e6e3000, 0x40000000);
}

static void test_sha512_ast2400(void)
{
    test_sha512("-machine palmetto-bmc", 0x1e6e3000, 0x40000000);
}

static void test_addresses_ast2400(void)
{
    test_addresses("-machine palmetto-bmc", 0x1e6e3000, &ast2400_masks);
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
