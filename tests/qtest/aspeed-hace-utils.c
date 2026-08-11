/*
 * QTest testcase for the ASPEED Hash and Crypto Engine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 IBM Corp.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "crypto/cipher.h"
#include "aspeed-hace-utils.h"

/*
 * Test vector is the ascii "abc"
 *
 * Expected results were generated using command line utitiles:
 *
 *  echo -n -e 'abc' | dd of=/tmp/test
 *  for hash in sha512sum sha384sum sha256sum md5sum; do $hash /tmp/test; done
 *
 */
static const uint8_t test_vector[3] = {0x61, 0x62, 0x63};

static const uint8_t test_result_sha512[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49,
    0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21, 0x92, 0x99, 0x2a,
    0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f,
    0xa5, 0x4c, 0xa4, 0x9f};

static const uint8_t test_result_sha384[48] = {
    0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b, 0xb5, 0xa0, 0x3d, 0x69,
    0x9a, 0xc6, 0x50, 0x07, 0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
    0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed, 0x80, 0x86, 0x07, 0x2b,
    0xa1, 0xe7, 0xcc, 0x23, 0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7};

static const uint8_t test_result_sha256[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

static const uint8_t test_result_md5[16] = {
    0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d,
    0x28, 0xe1, 0x7f, 0x72};

/*
 * The Scatter-Gather Test vector is the ascii "abc" "def" "ghi", broken
 * into blocks of 3 characters as shown
 *
 * Expected results were generated using command line utitiles:
 *
 *  echo -n -e 'abcdefghijkl' | dd of=/tmp/test
 *  for hash in sha512sum sha384sum sha256sum; do $hash /tmp/test; done
 *
 */
static const uint8_t test_vector_sg1[6] = {0x61, 0x62, 0x63, 0x64, 0x65, 0x66};
static const uint8_t test_vector_sg2[3] = {0x67, 0x68, 0x69};
static const uint8_t test_vector_sg3[3] = {0x6a, 0x6b, 0x6c};

static const uint8_t test_result_sg_sha512[64] = {
    0x17, 0x80, 0x7c, 0x72, 0x8e, 0xe3, 0xba, 0x35, 0xe7, 0xcf, 0x7a, 0xf8,
    0x23, 0x11, 0x6d, 0x26, 0xe4, 0x1e, 0x5d, 0x4d, 0x6c, 0x2f, 0xf1, 0xf3,
    0x72, 0x0d, 0x3d, 0x96, 0xaa, 0xcb, 0x6f, 0x69, 0xde, 0x64, 0x2e, 0x63,
    0xd5, 0xb7, 0x3f, 0xc3, 0x96, 0xc1, 0x2b, 0xe3, 0x8b, 0x2b, 0xd5, 0xd8,
    0x84, 0x25, 0x7c, 0x32, 0xc8, 0xf6, 0xd0, 0x85, 0x4a, 0xe6, 0xb5, 0x40,
    0xf8, 0x6d, 0xda, 0x2e};

static const uint8_t test_result_sg_sha384[48] = {
    0x10, 0x3c, 0xa9, 0x6c, 0x06, 0xa1, 0xce, 0x79, 0x8f, 0x08, 0xf8, 0xef,
    0xf0, 0xdf, 0xb0, 0xcc, 0xdb, 0x56, 0x7d, 0x48, 0xb2, 0x85, 0xb2, 0x3d,
    0x0c, 0xd7, 0x73, 0x45, 0x46, 0x67, 0xa3, 0xc2, 0xfa, 0x5f, 0x1b, 0x58,
    0xd9, 0xcd, 0xf2, 0x32, 0x9b, 0xd9, 0x97, 0x97, 0x30, 0xbf, 0xaa, 0xff};

static const uint8_t test_result_sg_sha256[32] = {
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
 *  for hash in sha512sum sha384sum sha256sum; do $hash /tmp/test; done
 */
static const uint8_t test_vector_accum_512[128] = {
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

static const uint8_t test_vector_accum_384[128] = {
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

static const uint8_t test_vector_accum_256[64] = {
    0x61, 0x62, 0x63, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18};

static const uint8_t test_result_accum_sha512[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49,
    0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21, 0x92, 0x99, 0x2a,
    0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f,
    0xa5, 0x4c, 0xa4, 0x9f};

static const uint8_t test_result_accum_sha384[48] = {
    0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b, 0xb5, 0xa0, 0x3d, 0x69,
    0x9a, 0xc6, 0x50, 0x07, 0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
    0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed, 0x80, 0x86, 0x07, 0x2b,
    0xa1, 0xe7, 0xcc, 0x23, 0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7};

static const uint8_t test_result_accum_sha256[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

static void write_regs(QTestState *s, uint32_t base, uint64_t src,
                       uint32_t length, uint64_t out, uint32_t method)
{
        qtest_writel(s, base + HACE_HASH_SRC, extract64(src, 0, 32));
        qtest_writel(s, base + HACE_HASH_SRC_HI, extract64(src, 32, 32));
        qtest_writel(s, base + HACE_HASH_DIGEST, extract64(out, 0, 32));
        qtest_writel(s, base + HACE_HASH_DIGEST_HI, extract64(out, 32, 32));
        qtest_writel(s, base + HACE_HASH_DATA_LEN, length);
        qtest_writel(s, base + HACE_HASH_CMD, HACE_SHA_BE_EN | method);
}

void aspeed_test_md5(const char *machine, const uint32_t base,
                     const uint64_t src_addr)

{
    QTestState *s = qtest_init(machine);

    uint64_t digest_addr = src_addr + 0x010000;
    uint8_t digest[16] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, base, src_addr, sizeof(test_vector),
               digest_addr, HACE_ALGO_MD5);

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

void aspeed_test_sha256(const char *machine, const uint32_t base,
                        const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t digest_addr = src_addr + 0x10000;
    uint8_t digest[32] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, base, src_addr, sizeof(test_vector), digest_addr,
               HACE_ALGO_SHA256);

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

void aspeed_test_sha384(const char *machine, const uint32_t base,
                        const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t digest_addr = src_addr + 0x10000;
    uint8_t digest[48] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, base, src_addr, sizeof(test_vector), digest_addr,
               HACE_ALGO_SHA384);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sha384, sizeof(digest));

    qtest_quit(s);
}

void aspeed_test_sha512(const char *machine, const uint32_t base,
                        const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t digest_addr = src_addr + 0x10000;
    uint8_t digest[64] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, base, src_addr, sizeof(test_vector), digest_addr,
               HACE_ALGO_SHA512);

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

void aspeed_test_sha256_sg(const char *machine, const uint32_t base,
                           const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t src_addr_1 = src_addr + 0x10000;
    const uint64_t src_addr_2 = src_addr + 0x20000;
    const uint64_t src_addr_3 = src_addr + 0x30000;
    const uint64_t digest_addr = src_addr + 0x40000;
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

void aspeed_test_sha384_sg(const char *machine, const uint32_t base,
                           const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t src_addr_1 = src_addr + 0x10000;
    const uint64_t src_addr_2 = src_addr + 0x20000;
    const uint64_t src_addr_3 = src_addr + 0x30000;
    const uint64_t digest_addr = src_addr + 0x40000;
    uint8_t digest[48] = {0};
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
               digest_addr, HACE_ALGO_SHA384 | HACE_SG_EN);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sg_sha384, sizeof(digest));

    qtest_quit(s);
}

void aspeed_test_sha512_sg(const char *machine, const uint32_t base,
                           const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t src_addr_1 = src_addr + 0x10000;
    const uint64_t src_addr_2 = src_addr + 0x20000;
    const uint64_t src_addr_3 = src_addr + 0x30000;
    const uint64_t digest_addr = src_addr + 0x40000;
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

void aspeed_test_sha256_accum(const char *machine, const uint32_t base,
                              const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t buffer_addr = src_addr + 0x10000;
    const uint64_t digest_addr = src_addr + 0x40000;
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

void aspeed_test_sha384_accum(const char *machine, const uint32_t base,
                              const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t buffer_addr = src_addr + 0x10000;
    const uint64_t digest_addr = src_addr + 0x40000;
    uint8_t digest[48] = {0};
    struct AspeedSgList array[] = {
        {  cpu_to_le32(sizeof(test_vector_accum_384) | SG_LIST_LEN_LAST),
           cpu_to_le32(buffer_addr) },
    };

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, buffer_addr, test_vector_accum_384,
                   sizeof(test_vector_accum_384));
    qtest_memwrite(s, src_addr, array, sizeof(array));

    write_regs(s, base, src_addr, sizeof(test_vector_accum_384),
               digest_addr, HACE_ALGO_SHA384 | HACE_SG_EN | HACE_ACCUM_EN);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, base + HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, base + HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_accum_sha384, sizeof(digest));

    qtest_quit(s);
}

void aspeed_test_sha512_accum(const char *machine, const uint32_t base,
                              const uint64_t src_addr)
{
    QTestState *s = qtest_init(machine);

    const uint64_t buffer_addr = src_addr + 0x10000;
    const uint64_t digest_addr = src_addr + 0x40000;
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

void aspeed_test_addresses(const char *machine, const uint32_t base,
                           const struct AspeedMasks *expected)
{
    QTestState *s = qtest_init(machine);

    /*
     * Check command mode is zero, meaning engine is in direct access mode,
     * as this affects the masking behavior of the HASH_SRC register.
     */
    g_assert_cmphex(qtest_readl(s, base + HACE_CMD), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC_HI), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST_HI), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_KEY_BUFF), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_KEY_BUFF_HI), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DATA_LEN), ==, 0);

    /* Check that the address masking is correct */
    qtest_writel(s, base + HACE_HASH_SRC, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC), ==, expected->src);

    qtest_writel(s, base + HACE_HASH_SRC_HI, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC_HI),
                    ==, expected->src_hi);

    qtest_writel(s, base + HACE_HASH_DIGEST, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST), ==,
                    expected->dest);

    qtest_writel(s, base + HACE_HASH_DIGEST_HI, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST_HI), ==,
                    expected->dest_hi);

    qtest_writel(s, base + HACE_HASH_KEY_BUFF, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_KEY_BUFF), ==,
                    expected->key);

    qtest_writel(s, base + HACE_HASH_KEY_BUFF_HI, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_KEY_BUFF_HI), ==,
                    expected->key_hi);

    qtest_writel(s, base + HACE_HASH_DATA_LEN, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DATA_LEN), ==,
                    expected->len);

    /* Reset to zero */
    qtest_writel(s, base + HACE_HASH_SRC, 0);
    qtest_writel(s, base + HACE_HASH_SRC_HI, 0);
    qtest_writel(s, base + HACE_HASH_DIGEST, 0);
    qtest_writel(s, base + HACE_HASH_DIGEST_HI, 0);
    qtest_writel(s, base + HACE_HASH_KEY_BUFF, 0);
    qtest_writel(s, base + HACE_HASH_KEY_BUFF_HI, 0);
    qtest_writel(s, base + HACE_HASH_DATA_LEN, 0);

    /* Check that all bits are now zero */
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_SRC_HI), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DIGEST_HI), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_KEY_BUFF), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_KEY_BUFF_HI), ==, 0);
    g_assert_cmphex(qtest_readl(s, base + HACE_HASH_DATA_LEN), ==, 0);

    qtest_quit(s);
}

/*
 * Crypto engine register layout (offsets from the HACE base).
 */
#define HACE_CRYPTO_SRC          0x00
#define HACE_CRYPTO_DEST         0x04
#define HACE_CRYPTO_CONTEXT      0x08
#define HACE_CRYPTO_DATA_LEN     0x0c
#define HACE_CRYPTO_CMD          0x10
#define HACE_CRYPTO_GCM_ADD_LEN  0x14
#define HACE_CRYPTO_GCM_TAG      0x18

/* Crypto command bits */
#define HACE_CMD_ENCRYPT         BIT(7)
#define HACE_CMD_ISR_EN          BIT(12)
#define HACE_CMD_DES_SELECT      BIT(16)
#define HACE_CMD_TRIPLE_DES      BIT(17)
#define HACE_CMD_SRC_SG_CTRL     BIT(18)
#define HACE_CMD_DST_SG_CTRL     BIT(19)
#define HACE_CMD_OP_MODE_MASK    (0x7 << 4)
#define HACE_CMD_ECB             (0x0 << 4)
#define HACE_CMD_CBC             (0x1 << 4)
#define HACE_CMD_CTR             (0x4 << 4)
#define HACE_CMD_GCM             (0x5 << 4)
#define HACE_CMD_AES128          (0x0 << 2)
#define HACE_CMD_AES256          (0x2 << 2)

/* Context buffer layout: IV (DES at +8), key at +0x10 */
#define HACE_CTX_KEY_OFFSET      0x10
#define HACE_CTX_SIZE            0x30

/*
 * Crypto known-answer test vectors, taken verbatim from the Linux kernel
 * crypto self-test templates in crypto/testmgr.h:
 *
 *   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/crypto/testmgr.h?h=v6.18
 *
 * The originating template is noted above each block. CTR and the longer CBC
 * vectors are truncated to a single block (still a valid known-answer test as
 * the first block only depends on the IV).
 */

/* aes_tv_template[0] (FIPS-197) */
static const uint8_t aes128_ecb_key[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
static const uint8_t aes128_ecb_ptext[16] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
static const uint8_t aes128_ecb_ctext[16] = {
    0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
    0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a };

/* aes_cbc_tv_template[0] (RFC 3602) */
static const uint8_t aes128_cbc_key[16] = {
    0x06, 0xa9, 0x21, 0x40, 0x36, 0xb8, 0xa1, 0x5b,
    0x51, 0x2e, 0x03, 0xd5, 0x34, 0x12, 0x00, 0x06 };
static const uint8_t aes128_cbc_iv[16] = {
    0x3d, 0xaf, 0xba, 0x42, 0x9d, 0x9e, 0xb4, 0x30,
    0xb4, 0x22, 0xda, 0x80, 0x2c, 0x9f, 0xac, 0x41 };
static const uint8_t aes128_cbc_ptext[16] = {
    0x53, 0x69, 0x6e, 0x67, 0x6c, 0x65, 0x20, 0x62,
    0x6c, 0x6f, 0x63, 0x6b, 0x20, 0x6d, 0x73, 0x67 };
static const uint8_t aes128_cbc_ctext[16] = {
    0xe3, 0x53, 0x77, 0x9c, 0x10, 0x79, 0xae, 0xb8,
    0x27, 0x08, 0x94, 0x2d, 0xbe, 0x77, 0x18, 0x1a };
static const uint8_t aes128_cbc_ivout[16] = {
    0xe3, 0x53, 0x77, 0x9c, 0x10, 0x79, 0xae, 0xb8,
    0x27, 0x08, 0x94, 0x2d, 0xbe, 0x77, 0x18, 0x1a };

/* des_tv_template[0] (Applied Cryptography) */
static const uint8_t des_ecb_key[8] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
static const uint8_t des_ecb_ptext[8] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xe7 };
static const uint8_t des_ecb_ctext[8] = {
    0xc9, 0x57, 0x44, 0x25, 0x6a, 0x5e, 0xd3, 0x1d };

/* des_cbc_tv_template[0] (OpenSSL), first block */
static const uint8_t des_cbc_key[8] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
static const uint8_t des_cbc_iv[8] = {
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 };
static const uint8_t des_cbc_ptext[8] = {
    0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x20 };
static const uint8_t des_cbc_ctext[8] = {
    0xcc, 0xd1, 0x73, 0xff, 0xab, 0x20, 0x39, 0xf4 };

/* des3_ede_tv_template[0] (OpenSSL) */
static const uint8_t tdes_ecb_key[24] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 };
static const uint8_t tdes_ecb_ptext[8] = {
    0x73, 0x6f, 0x6d, 0x65, 0x64, 0x61, 0x74, 0x61 };
static const uint8_t tdes_ecb_ctext[8] = {
    0x18, 0xd7, 0x48, 0xe5, 0x63, 0x62, 0x05, 0x72 };

/* des3_ede_cbc_tv_template[0] (OpenSSL), first block */
static const uint8_t tdes_cbc_key[24] = {
    0xe9, 0xc0, 0xff, 0x2e, 0x76, 0x0b, 0x64, 0x24,
    0x44, 0x4d, 0x99, 0x5a, 0x12, 0xd6, 0x40, 0xc0,
    0xea, 0xc2, 0x84, 0xe8, 0x14, 0x95, 0xdb, 0xe8 };
static const uint8_t tdes_cbc_iv[8] = {
    0x7d, 0x33, 0x88, 0x93, 0x0f, 0x93, 0xb2, 0x42 };
static const uint8_t tdes_cbc_ptext[8] = {
    0x6f, 0x54, 0x20, 0x6f, 0x61, 0x4d, 0x79, 0x6e };
static const uint8_t tdes_cbc_ctext[8] = {
    0x0e, 0x2d, 0xb6, 0x97, 0x3c, 0x56, 0x33, 0xf4 };

/* aes_ctr_tv_template[0] (NIST SP800-38A F.5.1), first block */
static const uint8_t aes128_ctr_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
static const uint8_t aes128_ctr_iv[16] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff };
static const uint8_t aes128_ctr_ptext[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a };
static const uint8_t aes128_ctr_ctext[16] = {
    0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
    0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce };
static const uint8_t aes128_ctr_ivout[16] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xff, 0x00 };

/* des_ctr_tv_template[0] (Crypto++), first block */
static const uint8_t des_ctr_key[8] = {
    0xc9, 0x83, 0xa6, 0xc9, 0xec, 0x0f, 0x32, 0x55 };
static const uint8_t des_ctr_iv[8] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd };
static const uint8_t des_ctr_ptext[8] = {
    0x50, 0xb9, 0x22, 0xae, 0x17, 0x80, 0x0c, 0x75 };
static const uint8_t des_ctr_ctext[8] = {
    0x2f, 0x96, 0x06, 0x0f, 0x50, 0xc9, 0x68, 0x03 };
static const uint8_t des_ctr_ivout[8] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe };

/* des3_ede_ctr_tv_template[0] (Crypto++), first block */
static const uint8_t tdes_ctr_key[24] = {
    0x9c, 0xd6, 0xf3, 0x9c, 0xb9, 0x5a, 0x67, 0x00,
    0x5a, 0x67, 0x00, 0x2d, 0xce, 0xeb, 0x2d, 0xce,
    0xeb, 0xb4, 0x51, 0x72, 0xb4, 0x51, 0x72, 0x1f };
static const uint8_t tdes_ctr_iv[8] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const uint8_t tdes_ctr_ptext[8] = {
    0x05, 0xec, 0x77, 0xfb, 0x42, 0xd5, 0x59, 0x20 };
static const uint8_t tdes_ctr_ctext[8] = {
    0x07, 0xc2, 0x08, 0x20, 0x72, 0x1f, 0x49, 0xef };
static const uint8_t tdes_ctr_ivout[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/*
 * aes_gcm_tv_template[2] (AES-128) and [9] (AES-256), from the McGrew & Viega
 * GCM spec (also NIST SP 800-38D), no AAD. Both cases share this plaintext/IV.
 */
static const uint8_t aes_gcm_ptext[64] = {
    0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
    0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
    0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
    0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
    0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
    0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
    0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
    0xba, 0x63, 0x7b, 0x39, 0x1a, 0xaf, 0xd2, 0x55 };
static const uint8_t aes_gcm_iv[12] = {
    0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
    0xde, 0xca, 0xf8, 0x88 };

/* aes_gcm_tv_template[2] (AES-128) */
static const uint8_t aes128_gcm_key[16] = {
    0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
    0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08 };
static const uint8_t aes128_gcm_ctext[64] = {
    0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24,
    0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4, 0x9c,
    0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0,
    0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e,
    0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c,
    0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
    0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97,
    0x3d, 0x58, 0xe0, 0x91, 0x47, 0x3f, 0x59, 0x85 };
static const uint8_t aes128_gcm_tag[16] = {
    0x4d, 0x5c, 0x2a, 0xf3, 0x27, 0xcd, 0x64, 0xa6,
    0x2c, 0xf3, 0x5a, 0xbd, 0x2b, 0xa6, 0xfa, 0xb4 };

/* aes_gcm_tv_template[9] (AES-256) */
static const uint8_t aes256_gcm_key[32] = {
    0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
    0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
    0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
    0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08 };
static const uint8_t aes256_gcm_ctext[64] = {
    0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07,
    0xf4, 0x7f, 0x37, 0xa3, 0x2a, 0x84, 0x42, 0x7d,
    0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9,
    0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55, 0xd1, 0xaa,
    0x8c, 0xb0, 0x8e, 0x48, 0x59, 0x0d, 0xbb, 0x3d,
    0xa7, 0xb0, 0x8b, 0x10, 0x56, 0x82, 0x88, 0x38,
    0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a,
    0xbc, 0xc9, 0xf6, 0x62, 0x89, 0x80, 0x15, 0xad };
static const uint8_t aes256_gcm_tag[16] = {
    0xb0, 0x94, 0xda, 0xc5, 0xd9, 0x34, 0x71, 0xbd,
    0xec, 0x1a, 0x50, 0x22, 0x70, 0xe3, 0xcc, 0x6c };

typedef struct CryptTest {
    QCryptoCipherMode mode;
    QCryptoCipherAlgo alg;
    /* expected context IV after encrypt, or NULL */
    const uint8_t *iv_out;
    const uint8_t *ptext;
    const uint8_t *ctext;
    /* expected GCM authentication tag, or NULL for non-AEAD modes */
    const uint8_t *tag;
    const uint8_t *key;
    const uint8_t *iv;
    const char *name;
    size_t keylen;
    size_t taglen;
    /* algorithm | mode | key size selection */
    uint32_t cmd;
    size_t ivlen;
    size_t len;
} CryptTest;

static const CryptTest crypt_tests[] = {
    {
        .name = "aes128-ecb",
        .cmd = HACE_CMD_AES128 | HACE_CMD_ECB,
        .alg = QCRYPTO_CIPHER_ALGO_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_ECB,
        .key = aes128_ecb_key,
        .keylen = sizeof(aes128_ecb_key),
        .ptext = aes128_ecb_ptext,
        .ctext = aes128_ecb_ctext,
        .len = sizeof(aes128_ecb_ptext),
    },
    {
        .name = "aes128-cbc",
        .cmd = HACE_CMD_AES128 | HACE_CMD_CBC,
        .alg = QCRYPTO_CIPHER_ALGO_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_CBC,
        .key = aes128_cbc_key,
        .keylen = sizeof(aes128_cbc_key),
        .iv = aes128_cbc_iv,
        .ivlen = sizeof(aes128_cbc_iv),
        .ptext = aes128_cbc_ptext,
        .ctext = aes128_cbc_ctext,
        .iv_out = aes128_cbc_ivout,
        .len = sizeof(aes128_cbc_ptext),
    },
    {
        .name = "des-ecb",
        .cmd = HACE_CMD_DES_SELECT | HACE_CMD_ECB,
        .alg = QCRYPTO_CIPHER_ALGO_DES,
        .mode = QCRYPTO_CIPHER_MODE_ECB,
        .key = des_ecb_key,
        .keylen = sizeof(des_ecb_key),
        .ptext = des_ecb_ptext,
        .ctext = des_ecb_ctext,
        .len = sizeof(des_ecb_ptext),
    },
    {
        .name = "des-cbc",
        .cmd = HACE_CMD_DES_SELECT | HACE_CMD_CBC,
        .alg = QCRYPTO_CIPHER_ALGO_DES,
        .mode = QCRYPTO_CIPHER_MODE_CBC,
        .key = des_cbc_key,
        .keylen = sizeof(des_cbc_key),
        .iv = des_cbc_iv,
        .ivlen = sizeof(des_cbc_iv),
        .ptext = des_cbc_ptext,
        .ctext = des_cbc_ctext,
        .len = sizeof(des_cbc_ptext),
    },
    {
        .name = "des3_ede-ecb",
        .cmd = HACE_CMD_DES_SELECT | HACE_CMD_TRIPLE_DES | HACE_CMD_ECB,
        .alg = QCRYPTO_CIPHER_ALGO_3DES,
        .mode = QCRYPTO_CIPHER_MODE_ECB,
        .key = tdes_ecb_key,
        .keylen = sizeof(tdes_ecb_key),
        .ptext = tdes_ecb_ptext,
        .ctext = tdes_ecb_ctext,
        .len = sizeof(tdes_ecb_ptext),
    },
    {
        .name = "des3_ede-cbc",
        .cmd = HACE_CMD_DES_SELECT | HACE_CMD_TRIPLE_DES | HACE_CMD_CBC,
        .alg = QCRYPTO_CIPHER_ALGO_3DES,
        .mode = QCRYPTO_CIPHER_MODE_CBC,
        .key = tdes_cbc_key,
        .keylen = sizeof(tdes_cbc_key),
        .iv = tdes_cbc_iv,
        .ivlen = sizeof(tdes_cbc_iv),
        .ptext = tdes_cbc_ptext,
        .ctext = tdes_cbc_ctext,
        .len = sizeof(tdes_cbc_ptext),
    },
    {
        .name = "aes128-ctr",
        .cmd = HACE_CMD_AES128 | HACE_CMD_CTR,
        .alg = QCRYPTO_CIPHER_ALGO_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_CTR,
        .key = aes128_ctr_key,
        .keylen = sizeof(aes128_ctr_key),
        .iv = aes128_ctr_iv,
        .ivlen = sizeof(aes128_ctr_iv),
        .ptext = aes128_ctr_ptext,
        .ctext = aes128_ctr_ctext,
        .iv_out = aes128_ctr_ivout,
        .len = sizeof(aes128_ctr_ptext),
    },
    {
        .name = "des-ctr",
        .cmd = HACE_CMD_DES_SELECT | HACE_CMD_CTR,
        .alg = QCRYPTO_CIPHER_ALGO_DES,
        .mode = QCRYPTO_CIPHER_MODE_CTR,
        .key = des_ctr_key,
        .keylen = sizeof(des_ctr_key),
        .iv = des_ctr_iv,
        .ivlen = sizeof(des_ctr_iv),
        .ptext = des_ctr_ptext,
        .ctext = des_ctr_ctext,
        .iv_out = des_ctr_ivout,
        .len = sizeof(des_ctr_ptext),
    },
    {
        .name = "des3_ede-ctr",
        .cmd = HACE_CMD_DES_SELECT | HACE_CMD_TRIPLE_DES | HACE_CMD_CTR,
        .alg = QCRYPTO_CIPHER_ALGO_3DES,
        .mode = QCRYPTO_CIPHER_MODE_CTR,
        .key = tdes_ctr_key,
        .keylen = sizeof(tdes_ctr_key),
        .iv = tdes_ctr_iv,
        .ivlen = sizeof(tdes_ctr_iv),
        .ptext = tdes_ctr_ptext,
        .ctext = tdes_ctr_ctext,
        .iv_out = tdes_ctr_ivout,
        .len = sizeof(tdes_ctr_ptext),
    },
    {
        .name = "aes128-gcm",
        .cmd = HACE_CMD_AES128 | HACE_CMD_GCM,
        .alg = QCRYPTO_CIPHER_ALGO_AES_128,
        .mode = QCRYPTO_CIPHER_MODE_GCM,
        .key = aes128_gcm_key,
        .keylen = sizeof(aes128_gcm_key),
        .iv = aes_gcm_iv,
        .ivlen = sizeof(aes_gcm_iv),
        .ptext = aes_gcm_ptext,
        .ctext = aes128_gcm_ctext,
        .tag = aes128_gcm_tag,
        .taglen = sizeof(aes128_gcm_tag),
        .len = sizeof(aes_gcm_ptext),
    },
    {
        .name = "aes256-gcm",
        .cmd = HACE_CMD_AES256 | HACE_CMD_GCM,
        .alg = QCRYPTO_CIPHER_ALGO_AES_256,
        .mode = QCRYPTO_CIPHER_MODE_GCM,
        .key = aes256_gcm_key,
        .keylen = sizeof(aes256_gcm_key),
        .iv = aes_gcm_iv,
        .ivlen = sizeof(aes_gcm_iv),
        .ptext = aes_gcm_ptext,
        .ctext = aes256_gcm_ctext,
        .tag = aes256_gcm_tag,
        .taglen = sizeof(aes256_gcm_tag),
        .len = sizeof(aes_gcm_ptext),
    },
};

/* DRAM offsets for the crypto test source, destination and context buffers. */
#define CRYPT_OFF_SRC   0x10000
#define CRYPT_OFF_DST   0x20000
#define CRYPT_OFF_CTX   0x30000
/* Scatter-gather list offsets (each list has CRYPT_SG_FRAGS entries). */
#define CRYPT_OFF_SRC_SG  0x40000
#define CRYPT_OFF_DST_SG  0x50000
/*
 * The scatter-gather tests split each buffer into CRYPT_SG_FRAGS fragments,
 * each placed CRYPT_SG_FRAG_STRIDE apart so the fragments never abut. The gaps
 * make the test fail if the engine ignores the list and reads one contiguous
 * block.
 */
#define CRYPT_SG_FRAGS         3
#define CRYPT_SG_FRAG_STRIDE   0x1000
/* DRAM offset for the AES-GCM authentication tag write buffer. */
#define CRYPT_OFF_TAG          0x60000

/* Describes one registered crypto test (qtest_add_data_func() data pointer). */
typedef struct AspeedCryptoTest {
    const char *machine;
    uint64_t dram;
    uint32_t base;
    int index;
    bool sg;
} AspeedCryptoTest;

/* Map a command's operation mode (HACE10[6:4]) to a CRYPT_MODE_* flag. */
static uint32_t crypt_mode_flag(uint32_t cmd)
{
    switch (cmd & HACE_CMD_OP_MODE_MASK) {
    case HACE_CMD_ECB:
        return CRYPT_MODE_ECB;
    case HACE_CMD_CBC:
        return CRYPT_MODE_CBC;
    case HACE_CMD_CTR:
        return CRYPT_MODE_CTR;
    case HACE_CMD_GCM:
        return CRYPT_MODE_GCM;
    default:
        return 0;
    }
}

static void crypt_write_ctx(QTestState *s, uint64_t ctx_addr,
                            const CryptTest *t)
{
    size_t iv_off = (t->cmd & HACE_CMD_DES_SELECT) ? 8 : 0;
    uint8_t ctx[HACE_CTX_SIZE] = { 0 };

    if (t->iv) {
        memcpy(ctx + iv_off, t->iv, t->ivlen);
    }
    memcpy(ctx + HACE_CTX_KEY_OFFSET, t->key, t->keylen);
    qtest_memwrite(s, ctx_addr, ctx, sizeof(ctx));
}

/* Run one crypto operation in direct access mode and read back the result. */
static void crypt_run_direct(QTestState *s, uint32_t base, uint64_t dram,
                             const CryptTest *t, bool encrypt, uint8_t *out)
{
    const uint8_t *in = encrypt ? t->ptext : t->ctext;
    uint32_t cmd = t->cmd | HACE_CMD_ISR_EN;
    uint64_t src = dram + CRYPT_OFF_SRC;
    uint64_t dst = dram + CRYPT_OFF_DST;
    uint64_t ctx = dram + CRYPT_OFF_CTX;

    if (encrypt) {
        cmd |= HACE_CMD_ENCRYPT;
    }

    crypt_write_ctx(s, ctx, t);
    qtest_memwrite(s, src, in, t->len);

    qtest_writel(s, base + HACE_CRYPTO_SRC, (uint32_t)src);
    qtest_writel(s, base + HACE_CRYPTO_DEST, (uint32_t)dst);
    qtest_writel(s, base + HACE_CRYPTO_CONTEXT, (uint32_t)ctx);
    qtest_writel(s, base + HACE_CRYPTO_DATA_LEN, t->len);
    qtest_writel(s, base + HACE_CRYPTO_CMD, cmd);

    g_assert_cmphex(qtest_readl(s, base + HACE_STS) & HACE_CRYPTO_ISR, ==,
                    HACE_CRYPTO_ISR);
    qtest_writel(s, base + HACE_STS, HACE_CRYPTO_ISR);

    qtest_memread(s, dst, out, t->len);
}

/*
 * Byte range [*frag_off, *frag_off + *frag_len) of fragment @index when an
 * @len-byte buffer is split into CRYPT_SG_FRAGS pieces; the last piece takes
 * the remainder of an uneven split.
 */
static void crypt_frag_range(uint32_t len, int index,
                             uint32_t *frag_off, uint32_t *frag_len)
{
    uint32_t base = len / CRYPT_SG_FRAGS;

    *frag_off = base * index;
    *frag_len = (index == CRYPT_SG_FRAGS - 1) ? len - *frag_off : base;
}

/*
 * Scatter [in, len) across CRYPT_SG_FRAGS buffers based at @base_off and spaced
 * CRYPT_SG_FRAG_STRIDE apart, then build the SG list describing them at @list.
 * When @in is NULL only the list is built (used for the destination, which the
 * engine fills in).
 */
static void crypt_make_sg(QTestState *s, uint64_t dram, uint32_t base_off,
                          uint64_t list, const uint8_t *in, uint32_t len)
{
    struct AspeedSgList sg[CRYPT_SG_FRAGS];
    uint32_t frag_off;
    uint32_t frag_len;
    uint64_t buf;
    int i;

    for (i = 0; i < CRYPT_SG_FRAGS; i++) {
        crypt_frag_range(len, i, &frag_off, &frag_len);
        buf = dram + base_off + i * CRYPT_SG_FRAG_STRIDE;

        if (in) {
            qtest_memwrite(s, buf, in + frag_off, frag_len);
        }
        sg[i].len = cpu_to_le32(frag_len | (i == CRYPT_SG_FRAGS - 1 ?
                                            SG_LIST_LEN_LAST : 0));
        sg[i].addr = cpu_to_le32((uint32_t)buf);
    }

    qtest_memwrite(s, list, sg, sizeof(sg));
}

/* Gather a scatter-gathered result back from the CRYPT_SG_FRAGS buffers. */
static void crypt_gather_sg(QTestState *s, uint64_t dram, uint32_t base_off,
                            uint8_t *out, uint32_t len)
{
    uint32_t frag_off;
    uint32_t frag_len;
    int i;

    for (i = 0; i < CRYPT_SG_FRAGS; i++) {
        crypt_frag_range(len, i, &frag_off, &frag_len);
        qtest_memread(s, dram + base_off + i * CRYPT_SG_FRAG_STRIDE,
                      out + frag_off, frag_len);
    }
}

/*
 * Run one block-cipher (ECB/CBC/CTR) operation in scatter-gather mode and read
 * back the result. The source and destination are each split across
 * CRYPT_SG_FRAGS non-adjacent DRAM buffers described by an SG list; the gaps
 * ensure the test fails if the engine ignores the list and reads one
 * contiguous block.
 */
static void crypt_run_sg(QTestState *s, uint32_t base, uint64_t dram,
                         const CryptTest *t, bool encrypt, uint8_t *out)
{
    const uint8_t *in = encrypt ? t->ptext : t->ctext;
    uint64_t src_sg = dram + CRYPT_OFF_SRC_SG;
    uint64_t dst_sg = dram + CRYPT_OFF_DST_SG;
    uint64_t ctx = dram + CRYPT_OFF_CTX;
    uint32_t cmd = t->cmd | HACE_CMD_ISR_EN | HACE_CMD_SRC_SG_CTRL |
                   HACE_CMD_DST_SG_CTRL;

    if (encrypt) {
        cmd |= HACE_CMD_ENCRYPT;
    }

    crypt_write_ctx(s, ctx, t);
    crypt_make_sg(s, dram, CRYPT_OFF_SRC, src_sg, in, t->len);
    crypt_make_sg(s, dram, CRYPT_OFF_DST, dst_sg, NULL, t->len);

    qtest_writel(s, base + HACE_CRYPTO_SRC, (uint32_t)src_sg);
    qtest_writel(s, base + HACE_CRYPTO_DEST, (uint32_t)dst_sg);
    qtest_writel(s, base + HACE_CRYPTO_CONTEXT, (uint32_t)ctx);
    qtest_writel(s, base + HACE_CRYPTO_DATA_LEN, t->len);
    qtest_writel(s, base + HACE_CRYPTO_CMD, cmd);

    g_assert_cmphex(qtest_readl(s, base + HACE_STS) & HACE_CRYPTO_ISR, ==,
                    HACE_CRYPTO_ISR);
    qtest_writel(s, base + HACE_STS, HACE_CRYPTO_ISR);

    crypt_gather_sg(s, dram, CRYPT_OFF_DST, out, t->len);
}

/*
 * Run one AES-GCM operation in scatter-gather mode: like crypt_run_sg() but
 * also program the tag write buffer (HACE18) with no associated data, and read
 * the authentication tag back into @out_tag.
 */
static void crypt_run_gcm(QTestState *s, uint32_t base, uint64_t dram,
                          const CryptTest *t, bool encrypt, uint8_t *out,
                          uint8_t *out_tag)
{
    const uint8_t *in = encrypt ? t->ptext : t->ctext;
    uint64_t src_sg = dram + CRYPT_OFF_SRC_SG;
    uint64_t dst_sg = dram + CRYPT_OFF_DST_SG;
    uint64_t ctx = dram + CRYPT_OFF_CTX;
    uint32_t cmd = t->cmd | HACE_CMD_ISR_EN | HACE_CMD_SRC_SG_CTRL |
                   HACE_CMD_DST_SG_CTRL;

    if (encrypt) {
        cmd |= HACE_CMD_ENCRYPT;
    }

    crypt_write_ctx(s, ctx, t);
    crypt_make_sg(s, dram, CRYPT_OFF_SRC, src_sg, in, t->len);
    crypt_make_sg(s, dram, CRYPT_OFF_DST, dst_sg, NULL, t->len);

    qtest_writel(s, base + HACE_CRYPTO_SRC, (uint32_t)src_sg);
    qtest_writel(s, base + HACE_CRYPTO_DEST, (uint32_t)dst_sg);
    qtest_writel(s, base + HACE_CRYPTO_CONTEXT, (uint32_t)ctx);
    qtest_writel(s, base + HACE_CRYPTO_DATA_LEN, t->len);
    qtest_writel(s, base + HACE_CRYPTO_GCM_ADD_LEN, 0);
    qtest_writel(s, base + HACE_CRYPTO_GCM_TAG,
                 (uint32_t)(dram + CRYPT_OFF_TAG));
    qtest_writel(s, base + HACE_CRYPTO_CMD, cmd);

    g_assert_cmphex(qtest_readl(s, base + HACE_STS) & HACE_CRYPTO_ISR, ==,
                    HACE_CRYPTO_ISR);
    qtest_writel(s, base + HACE_STS, HACE_CRYPTO_ISR);

    crypt_gather_sg(s, dram, CRYPT_OFF_DST, out, t->len);
    qtest_memread(s, dram + CRYPT_OFF_TAG, out_tag, t->taglen);
}

static void aspeed_test_crypto(const void *data)
{
    const AspeedCryptoTest *c = data;
    const CryptTest *t = &crypt_tests[c->index];
    QTestState *s = qtest_init(c->machine);
    uint8_t out[64];
    uint8_t iv[16];
    size_t iv_off;

    g_assert_cmpuint(t->len, <=, sizeof(out));

    /* Encrypt: ptext -> ctext */
    if (c->sg) {
        crypt_run_sg(s, c->base, c->dram, t, true, out);
    } else {
        crypt_run_direct(s, c->base, c->dram, t, true, out);
    }
    g_assert_cmpmem(out, t->len, t->ctext, t->len);

    if (t->iv_out) {
        iv_off = (t->cmd & HACE_CMD_DES_SELECT) ? 8 : 0;
        qtest_memread(s, c->dram + CRYPT_OFF_CTX + iv_off, iv, t->ivlen);
        g_assert_cmpmem(iv, t->ivlen, t->iv_out, t->ivlen);
    }

    /* Decrypt: ctext -> ptext */
    if (c->sg) {
        crypt_run_sg(s, c->base, c->dram, t, false, out);
    } else {
        crypt_run_direct(s, c->base, c->dram, t, false, out);
    }
    g_assert_cmpmem(out, t->len, t->ptext, t->len);

    qtest_quit(s);
}

static void aspeed_test_crypto_gcm(const void *data)
{
    const AspeedCryptoTest *c = data;
    const CryptTest *t = &crypt_tests[c->index];
    QTestState *s = qtest_init(c->machine);
    uint8_t out[64];
    uint8_t tag[16];

    g_assert_cmpuint(t->len, <=, sizeof(out));

    /* Encrypt: ptext -> ctext, then check the authentication tag. */
    crypt_run_gcm(s, c->base, c->dram, t, true, out, tag);
    g_assert_cmpmem(out, t->len, t->ctext, t->len);
    g_assert_cmpmem(tag, t->taglen, t->tag, t->taglen);

    /* Decrypt: ctext -> ptext, the recomputed tag must match. */
    crypt_run_gcm(s, c->base, c->dram, t, false, out, tag);
    g_assert_cmpmem(out, t->len, t->ptext, t->len);
    g_assert_cmpmem(tag, t->taglen, t->tag, t->taglen);

    qtest_quit(s);
}

void aspeed_add_crypto_tests(const char *prefix, const char *machine,
                             uint32_t base, uint64_t dram, uint32_t modes,
                             bool sg)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(crypt_tests); i++) {
        bool is_gcm = crypt_tests[i].mode == QCRYPTO_CIPHER_MODE_GCM;
        g_autofree char *path = NULL;
        AspeedCryptoTest *t;

        if (!(modes & crypt_mode_flag(crypt_tests[i].cmd))) {
            continue;
        }

        if (!qcrypto_cipher_supports(crypt_tests[i].alg,
                                     crypt_tests[i].mode)) {
            g_printerr("# skip unsupported %s\n", crypt_tests[i].name);
            continue;
        }

        path = g_strdup_printf("%s/hace/crypto/%s", prefix,
                               crypt_tests[i].name);
        t = g_new0(AspeedCryptoTest, 1);
        t->machine = machine;
        t->base = base;
        t->dram = dram;
        t->index = i;
        t->sg = sg;
        qtest_add_data_func_full(path, t,
                                 is_gcm ? aspeed_test_crypto_gcm :
                                 aspeed_test_crypto, g_free);
    }
}

