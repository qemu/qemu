/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Simple test for CPACF KMC instruction
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpacf.h"

#define QUERY_BLOCK_SIZE 16

/* expected kmc query block */
static uint8_t exp_query_block[QUERY_BLOCK_SIZE] = {
    0x80, 0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* KMC AES-128 test data */
static const uint8_t kmcaes128key[] = {
    0x1f, 0x8e, 0x49, 0x73, 0x95, 0x3f, 0x3f, 0xb0,
    0xbd, 0x6b, 0x16, 0x66, 0x2e, 0x9a, 0x3c, 0x17
};
static const uint8_t kmcaes128iv[] = {
    0x2f, 0xe2, 0xb3, 0x33, 0xce, 0xda, 0x8f, 0x98,
    0xf4, 0xa9, 0x9b, 0x40, 0xd2, 0xcd, 0x34, 0xa8
};
static const uint8_t kmcaes128plain[] = {
    0x45, 0xcf, 0x12, 0x96, 0x4f, 0xc8, 0x24, 0xab,
    0x76, 0x61, 0x6a, 0xe2, 0xf4, 0xbf, 0x08, 0x22
};
static const uint8_t kmcaes128cipher[] = {
    0x0f, 0x61, 0xc4, 0xd4, 0x4c, 0x51, 0x47, 0xc0,
    0x3c, 0x19, 0x5a, 0xd7, 0xe2, 0xcc, 0x12, 0xb2
};

/* KMC AES-192 test data */
static const uint8_t kmcaes192key[] = {
    0xba, 0x75, 0xf4, 0xd1, 0xd9, 0xd7, 0xcf, 0x7f,
    0x55, 0x14, 0x45, 0xd5, 0x6c, 0xc1, 0xa8, 0xab,
    0x2a, 0x07, 0x8e, 0x15, 0xe0, 0x49, 0xdc, 0x2c
};
static const uint8_t kmcaes192iv[] = {
    0x53, 0x1c, 0xe7, 0x81, 0x76, 0x40, 0x16, 0x66,
    0xaa, 0x30, 0xdb, 0x94, 0xec, 0x4a, 0x30, 0xeb
};
static const uint8_t kmcaes192plain[] = {
    0xc5, 0x1f, 0xc2, 0x76, 0x77, 0x4d, 0xad, 0x94,
    0xbc, 0xdc, 0x1d, 0x28, 0x91, 0xec, 0x86, 0x68
};
static const uint8_t kmcaes192cipher[] = {
    0x70, 0xdd, 0x95, 0xa1, 0x4e, 0xe9, 0x75, 0xe2,
    0x39, 0xdf, 0x36, 0xff, 0x4a, 0xee, 0x1d, 0x5d
};

/* KMC AES-256 test data */
static const uint8_t kmcaes256key[] = {
    0x6e, 0xd7, 0x6d, 0x2d, 0x97, 0xc6, 0x9f, 0xd1,
    0x33, 0x95, 0x89, 0x52, 0x39, 0x31, 0xf2, 0xa6,
    0xcf, 0xf5, 0x54, 0xb1, 0x5f, 0x73, 0x8f, 0x21,
    0xec, 0x72, 0xdd, 0x97, 0xa7, 0x33, 0x09, 0x07
};
static const uint8_t kmcaes256iv[] = {
    0x85, 0x1e, 0x87, 0x64, 0x77, 0x6e, 0x67, 0x96,
    0xaa, 0xb7, 0x22, 0xdb, 0xb6, 0x44, 0xac, 0xe8
};
static const uint8_t kmcaes256plain[] = {
    0x62, 0x82, 0xb8, 0xc0, 0x5c, 0x5c, 0x15, 0x30,
    0xb9, 0x7d, 0x48, 0x16, 0xca, 0x43, 0x47, 0x62
};
static const uint8_t kmcaes256cipher[] = {
    0x6a, 0xcc, 0x04, 0x14, 0x2e, 0x10, 0x0a, 0x65,
    0xf5, 0x1b, 0x97, 0xad, 0xf5, 0x17, 0x2c, 0x41
};

/* static byte array containing the WKVP */
static const uint8_t protkey_wkvp[32] = PROTKEY_WKVP;

/*
 * Query test for kmc
 * returns > 0 on failure, otherwise 0
 */
static int test_kmc_query(void)
{
    uint8_t query_block[QUERY_BLOCK_SIZE] = {0};
    unsigned long cc = 0;
    int i, rc = 0;

    cpacf_kmc(CPACF_KMC_QUERY, query_block, NULL, NULL, 0, &cc);

    /* compare with expected query block */
    for (i = 0; i < QUERY_BLOCK_SIZE; i++) {
        if ((query_block[i] & exp_query_block[i]) != exp_query_block[i]) {
            rc++;
            break;
        }
    }

    if (rc) {
        printf("%s failed\n", __func__);
    }

    return rc;
}

/*
 * Subfunction CPACF_KMC_AES_128 test for kmc
 * returns > 0 on failure, otherwise 0
 */
static int test_kmc_aes_128(void)
{
    uint8_t param[16 + 16]; /* IV + key */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: IV followed by key */
    memcpy(param, kmcaes128iv, sizeof(kmcaes128iv));
    memcpy(param + sizeof(kmcaes128iv), kmcaes128key, sizeof(kmcaes128key));

    /* Encrypt */
    cpacf_kmc(CPACF_KMC_AES_128, param, output, kmcaes128plain,
              sizeof(kmcaes128plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmcaes128cipher, sizeof(kmcaes128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMC_AES_192 test for kmc
 * returns > 0 on failure, otherwise 0
 */
static int test_kmc_aes_192(void)
{
    uint8_t param[16 + 24]; /* IV + key */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: IV followed by key */
    memcpy(param, kmcaes192iv, sizeof(kmcaes192iv));
    memcpy(param + sizeof(kmcaes192iv), kmcaes192key, sizeof(kmcaes192key));

    /* Encrypt */
    cpacf_kmc(CPACF_KMC_AES_192, param, output, kmcaes192plain,
              sizeof(kmcaes192plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmcaes192cipher, sizeof(kmcaes192cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMC_AES_256 test for kmc
 * returns > 0 on failure, otherwise 0
 */
static int test_kmc_aes_256(void)
{
    uint8_t param[16 + 32]; /* IV + key */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: IV followed by key */
    memcpy(param, kmcaes256iv, sizeof(kmcaes256iv));
    memcpy(param + sizeof(kmcaes256iv), kmcaes256key, sizeof(kmcaes256key));

    /* Encrypt */
    cpacf_kmc(CPACF_KMC_AES_256, param, output, kmcaes256plain,
              sizeof(kmcaes256plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmcaes256cipher, sizeof(kmcaes256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMC_PAES_128 test for kmc
 * returns > 0 on failure, otherwise 0
 */
static int test_kmc_paes_128(void)
{
    uint8_t param[16 + 16 + 32]; /* IV + protected key + wkvp */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: IV + protected key + wkvp */
    memcpy(param, kmcaes128iv, sizeof(kmcaes128iv));
    memcpy(param + sizeof(kmcaes128iv), kmcaes128key, sizeof(kmcaes128key));
    encrypt_clrkey(param + sizeof(kmcaes128iv), sizeof(kmcaes128key));
    memcpy(param + sizeof(kmcaes128iv) + sizeof(kmcaes128key),
           protkey_wkvp, sizeof(protkey_wkvp));

    /* Encrypt */
    cpacf_kmc(CPACF_KMC_PAES_128, param, output, kmcaes128plain,
              sizeof(kmcaes128plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmcaes128cipher, sizeof(kmcaes128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMC_PAES_192 test for kmc
 * returns > 0 on failure, otherwise 0
 */
static int test_kmc_paes_192(void)
{
    uint8_t param[16 + 24 + 32]; /* IV + protected key + wkvp */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: IV + protected key + wkvp */
    memcpy(param, kmcaes192iv, sizeof(kmcaes192iv));
    memcpy(param + sizeof(kmcaes192iv), kmcaes192key, sizeof(kmcaes192key));
    encrypt_clrkey(param + sizeof(kmcaes192iv), sizeof(kmcaes192key));
    memcpy(param + sizeof(kmcaes192iv) + sizeof(kmcaes192key),
           protkey_wkvp, sizeof(protkey_wkvp));

    /* Encrypt */
    cpacf_kmc(CPACF_KMC_PAES_192, param, output, kmcaes192plain,
              sizeof(kmcaes192plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmcaes192cipher, sizeof(kmcaes192cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMC_PAES_256 test for kmc
 * returns > 0 on failure, otherwise 0
 */
static int test_kmc_paes_256(void)
{
    uint8_t param[16 + 32 + 32]; /* IV + protected key + wkvp */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: IV + protected key + wkvp */
    memcpy(param, kmcaes256iv, sizeof(kmcaes256iv));
    memcpy(param + sizeof(kmcaes256iv), kmcaes256key, sizeof(kmcaes256key));
    encrypt_clrkey(param + sizeof(kmcaes256iv), sizeof(kmcaes256key));
    memcpy(param + sizeof(kmcaes256iv) + sizeof(kmcaes256key),
           protkey_wkvp, sizeof(protkey_wkvp));

    /* Encrypt */
    cpacf_kmc(CPACF_KMC_PAES_256, param, output, kmcaes256plain,
              sizeof(kmcaes256plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmcaes256cipher, sizeof(kmcaes256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

int main(void)
{
    int rc = 0;

    /* Test query function */
    rc += test_kmc_query();

    /* Test AES-128 */
    rc += test_kmc_aes_128();

    /* Test AES-192 */
    rc += test_kmc_aes_192();

    /* Test AES-256 */
    rc += test_kmc_aes_256();

    /* Test PAES-128 */
    rc += test_kmc_paes_128();

    /* Test PAES-192 */
    rc += test_kmc_paes_192();

    /* Test PAES-256 */
    rc += test_kmc_paes_256();

    if (rc) {
        printf("cpacf-kmc: %d failures\n", rc);
    }

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
