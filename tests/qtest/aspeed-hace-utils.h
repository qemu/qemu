/*
 * QTest testcase for the ASPEED Hash and Crypto Engine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 IBM Corp.
 */

#ifndef TESTS_ASPEED_HACE_UTILS_H
#define TESTS_ASPEED_HACE_UTILS_H

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
#define HACE_HASH_SRC_HI         0x90
#define HACE_HASH_DIGEST_HI      0x94
#define HACE_HASH_KEY_BUFF_HI    0x98

/* Scatter-Gather Hash */
#define SG_LIST_LEN_LAST         BIT(31)
struct AspeedSgList {
        uint32_t len;
        uint32_t addr;
} __attribute__ ((__packed__));

struct AspeedMasks {
    uint32_t src;
    uint32_t dest;
    uint32_t key;
    uint32_t len;
    uint32_t src_hi;
    uint32_t dest_hi;
    uint32_t key_hi;
};

void aspeed_test_md5(const char *machine, const uint32_t base,
                     const uint64_t src_addr);
void aspeed_test_sha256(const char *machine, const uint32_t base,
                        const uint64_t src_addr);
void aspeed_test_sha384(const char *machine, const uint32_t base,
                        const uint64_t src_addr);
void aspeed_test_sha512(const char *machine, const uint32_t base,
                        const uint64_t src_addr);
void aspeed_test_sha256_sg(const char *machine, const uint32_t base,
                           const uint64_t src_addr);
void aspeed_test_sha384_sg(const char *machine, const uint32_t base,
                           const uint64_t src_addr);
void aspeed_test_sha512_sg(const char *machine, const uint32_t base,
                           const uint64_t src_addr);
void aspeed_test_sha256_accum(const char *machine, const uint32_t base,
                              const uint64_t src_addr);
void aspeed_test_sha384_accum(const char *machine, const uint32_t base,
                              const uint64_t src_addr);
void aspeed_test_sha512_accum(const char *machine, const uint32_t base,
                              const uint64_t src_addr);
void aspeed_test_addresses(const char *machine, const uint32_t base,
                           const struct AspeedMasks *expected);

#endif /* TESTS_ASPEED_HACE_UTILS_H */

