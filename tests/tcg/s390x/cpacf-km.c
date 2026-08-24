/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Simple test for CPACF KM instruction
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpacf.h"

#define QUERY_BLOCK_SIZE 16

/* expected km query block */
static uint8_t exp_query_block[QUERY_BLOCK_SIZE] = {
    0x80, 0x00, 0x38, 0x38, 0x00, 0x00, 0x28, 0x28,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* KM AES-128 test data */
static const uint8_t kmaes128key[] = {
    0xed, 0xfd, 0xb2, 0x57, 0xcb, 0x37, 0xcd, 0xf1,
    0x82, 0xc5, 0x45, 0x5b, 0x0c, 0x0e, 0xfe, 0xbb
};
static const uint8_t kmaes128plain[] = {
    0x16, 0x95, 0xfe, 0x47, 0x54, 0x21, 0xca, 0xce,
    0x35, 0x57, 0xda, 0xca, 0x01, 0xf4, 0x45, 0xff
};
static const uint8_t kmaes128cipher[] = {
    0x78, 0x88, 0xbe, 0xae, 0x6e, 0x7a, 0x42, 0x63,
    0x32, 0xa7, 0xea, 0xa2, 0xf8, 0x08, 0xe6, 0x37
};

/* KM AES-192 test data */
static const uint8_t kmaes192key[] = {
    0x61, 0x39, 0x6c, 0x53, 0x0c, 0xc1, 0x74, 0x9a,
    0x5b, 0xab, 0x6f, 0xbc, 0xf9, 0x06, 0xfe, 0x67,
    0x2d, 0x0c, 0x4a, 0xb2, 0x01, 0xaf, 0x45, 0x54
};
static const uint8_t kmaes192plain[] = {
    0x60, 0xbc, 0xdb, 0x94, 0x16, 0xba, 0xc0, 0x8d,
    0x7f, 0xd0, 0xd7, 0x80, 0x35, 0x37, 0x40, 0xa5
};
static const uint8_t kmaes192cipher[] = {
    0x24, 0xf4, 0x0c, 0x4e, 0xec, 0xd9, 0xc4, 0x98,
    0x25, 0x00, 0x0f, 0xcb, 0x49, 0x72, 0x64, 0x7a
};

/* KM AES-256 test data */
static const uint8_t kmaes256key[] = {
    0xcc, 0x22, 0xda, 0x78, 0x7f, 0x37, 0x57, 0x11,
    0xc7, 0x63, 0x02, 0xbe, 0xf0, 0x97, 0x9d, 0x8e,
    0xdd, 0xf8, 0x42, 0x82, 0x9c, 0x2b, 0x99, 0xef,
    0x3d, 0xd0, 0x4e, 0x23, 0xe5, 0x4c, 0xc2, 0x4b
};
static const uint8_t kmaes256plain[] = {
    0xcc, 0xc6, 0x2c, 0x6b, 0x0a, 0x09, 0xa6, 0x71,
    0xd6, 0x44, 0x56, 0x81, 0x8d, 0xb2, 0x9a, 0x4d
};
static const uint8_t kmaes256cipher[] = {
    0xdf, 0x86, 0x34, 0xca, 0x02, 0xb1, 0x3a, 0x12,
    0x5b, 0x78, 0x6e, 0x1d, 0xce, 0x90, 0x65, 0x8b
};

/* KM AES XTS-128 test data */
static const uint8_t kmaesxts128key1[] = {
    0xa1, 0xb9, 0x0c, 0xba, 0x3f, 0x06, 0xac, 0x35,
    0x3b, 0x2c, 0x34, 0x38, 0x76, 0x08, 0x17, 0x62
};
static const uint8_t kmaesxts128key2[] = {
    0x09, 0x09, 0x23, 0x02, 0x6e, 0x91, 0x77, 0x18,
    0x15, 0xf2, 0x9d, 0xab, 0x01, 0x93, 0x2f, 0x2f
};
static const uint8_t kmaesxts128sect[] = {
    0x4f, 0xae, 0xf7, 0x11, 0x7c, 0xda, 0x59, 0xc6,
    0x6e, 0x4b, 0x92, 0x01, 0x3e, 0x76, 0x8a, 0xd5
};
static const uint8_t kmaesxts128plain[] = {
    0xeb, 0xab, 0xce, 0x95, 0xb1, 0x4d, 0x3c, 0x8d,
    0x6f, 0xb3, 0x50, 0x39, 0x07, 0x90, 0x31, 0x1c
};
static const uint8_t kmaesxts128cipher[] = {
    0x77, 0x8a, 0xe8, 0xb4, 0x3c, 0xb9, 0x8d, 0x5a,
    0x82, 0x50, 0x81, 0xd5, 0xbe, 0x47, 0x1c, 0x63
};

/* KM AES XTS-256 test data */
static const uint8_t kmaesxts256key1[] = {
    0x1e, 0xa6, 0x61, 0xc5, 0x8d, 0x94, 0x3a, 0x0e,
    0x48, 0x01, 0xe4, 0x2f, 0x4b, 0x09, 0x47, 0x14,
    0x9e, 0x7f, 0x9f, 0x8e, 0x3e, 0x68, 0xd0, 0xc7,
    0x50, 0x52, 0x10, 0xbd, 0x31, 0x1a, 0x0e, 0x7c
};
static const uint8_t kmaesxts256key2[] = {
    0xd6, 0xe1, 0x3f, 0xfd, 0xf2, 0x41, 0x8d, 0x8d,
    0x19, 0x11, 0xc0, 0x04, 0xcd, 0xa5, 0x8d, 0xa3,
    0xd6, 0x19, 0xb7, 0xe2, 0xb9, 0x14, 0x1e, 0x58,
    0x31, 0x8e, 0xea, 0x39, 0x2c, 0xf4, 0x1b, 0x08
};
static const uint8_t kmaesxts256sect[] = {
    0xad, 0xf8, 0xd9, 0x26, 0x27, 0x46, 0x4a, 0xd2,
    0xf0, 0x42, 0x8e, 0x84, 0xa9, 0xf8, 0x75, 0x64
};
static const uint8_t kmaesxts256plain[] = {
    0x2e, 0xed, 0xea, 0x52, 0xcd, 0x82, 0x15, 0xe1,
    0xac, 0xc6, 0x47, 0xe8, 0x10, 0xbb, 0xc3, 0x64,
    0x2e, 0x87, 0x28, 0x7f, 0x8d, 0x2e, 0x57, 0xe3,
    0x6c, 0x0a, 0x24, 0xfb, 0xc1, 0x2a, 0x20, 0x2e
};
static const uint8_t kmaesxts256cipher[] = {
    0xcb, 0xaa, 0xd0, 0xe2, 0xf6, 0xce, 0xa3, 0xf5,
    0x0b, 0x37, 0xf9, 0x34, 0xd4, 0x6a, 0x9b, 0x13,
    0x0b, 0x9d, 0x54, 0xf0, 0x7e, 0x34, 0xf3, 0x6a,
    0xf7, 0x93, 0xe8, 0x6f, 0x73, 0xc6, 0xd7, 0xdb
};

/* static byte array containing the WKVP */
static const uint8_t protkey_wkvp[32] = PROTKEY_WKVP;

/*
 * Query test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_query(void)
{
    uint8_t query_block[QUERY_BLOCK_SIZE] = {0};
    unsigned long cc = 0;
    int i, rc = 0;

    cpacf_km(CPACF_KM_QUERY, query_block, NULL, NULL, 0, &cc);

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
 * Subfunction CPACF_KM_AES_128 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_aes_128(void)
{
    uint8_t param[16]; /* key only, no IV for ECB mode */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key only */
    memcpy(param, kmaes128key, sizeof(kmaes128key));

    /* Encrypt */
    cpacf_km(CPACF_KM_AES_128, param, output, kmaes128plain,
             sizeof(kmaes128plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaes128cipher, sizeof(kmaes128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_AES_192 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_aes_192(void)
{
    uint8_t param[24]; /* key only, no IV for ECB mode */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key only */
    memcpy(param, kmaes192key, sizeof(kmaes192key));

    /* Encrypt */
    cpacf_km(CPACF_KM_AES_192, param, output, kmaes192plain,
             sizeof(kmaes192plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaes192cipher, sizeof(kmaes192cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_AES_256 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_aes_256(void)
{
    uint8_t param[32]; /* key only, no IV for ECB mode */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key only */
    memcpy(param, kmaes256key, sizeof(kmaes256key));

    /* Encrypt */
    cpacf_km(CPACF_KM_AES_256, param, output, kmaes256plain,
             sizeof(kmaes256plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaes256cipher, sizeof(kmaes256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_PAES_128 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_paes_128(void)
{
    uint8_t param[16 + 32]; /* protected key + wkvp */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp */
    memcpy(param, kmaes128key, sizeof(kmaes128key));
    encrypt_clrkey(param, sizeof(kmaes128key));
    memcpy(param + sizeof(kmaes128key), protkey_wkvp, sizeof(protkey_wkvp));

    /* Encrypt */
    cpacf_km(CPACF_KM_PAES_128, param, output, kmaes128plain,
             sizeof(kmaes128plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaes128cipher, sizeof(kmaes128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_PAES_192 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_paes_192(void)
{
    uint8_t param[24 + 32]; /* protected key + wkvp */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp */
    memcpy(param, kmaes192key, sizeof(kmaes192key));
    encrypt_clrkey(param, sizeof(kmaes192key));
    memcpy(param + sizeof(kmaes192key), protkey_wkvp, sizeof(protkey_wkvp));

    /* Encrypt */
    cpacf_km(CPACF_KM_PAES_192, param, output, kmaes192plain,
             sizeof(kmaes192plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaes192cipher, sizeof(kmaes192cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_PAES_256 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_paes_256(void)
{
    uint8_t param[32 + 32]; /* protected key + wkvp */
    uint8_t output[16];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp */
    memcpy(param, kmaes256key, sizeof(kmaes256key));
    encrypt_clrkey(param, sizeof(kmaes256key));
    memcpy(param + sizeof(kmaes256key), protkey_wkvp, sizeof(protkey_wkvp));

    /* Encrypt */
    cpacf_km(CPACF_KM_PAES_256, param, output, kmaes256plain,
             sizeof(kmaes256plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaes256cipher, sizeof(kmaes256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_XTS_128 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_xts_128(void)
{
    uint8_t param[16 + 16]; /* key + initial XTS value */
    uint8_t output[16];
    uint8_t init_xts[16];
    unsigned long cc = 0;
    int rc = 0;

    /* First compute initial XTS value using key2 and sector */
    memcpy(param, kmaesxts128key2, sizeof(kmaesxts128key2));
    cpacf_km(CPACF_KM_AES_128, param, init_xts, kmaesxts128sect,
             sizeof(kmaesxts128sect), &cc);

    if (cc != 0) {
        printf("%s failed: initial XTS computation cc=%lu\n", __func__, cc);
        return 1;
    }

    /* Setup parameter block: key1 + initial XTS value */
    memcpy(param, kmaesxts128key1, sizeof(kmaesxts128key1));
    memcpy(param + 16, init_xts, sizeof(init_xts));

    /* Encrypt */
    cpacf_km(CPACF_KM_XTS_128, param, output, kmaesxts128plain,
             sizeof(kmaesxts128plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaesxts128cipher, sizeof(kmaesxts128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_XTS_256 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_xts_256(void)
{
    uint8_t param[32 + 16]; /* key + initial XTS value */
    uint8_t output[32];
    uint8_t init_xts[16];
    unsigned long cc = 0;
    int rc = 0;

    /* First compute initial XTS value using key2 and sector */
    memcpy(param, kmaesxts256key2, sizeof(kmaesxts256key2));
    cpacf_km(CPACF_KM_AES_256, param, init_xts, kmaesxts256sect,
             sizeof(kmaesxts256sect), &cc);

    if (cc != 0) {
        printf("%s failed: initial XTS computation cc=%lu\n", __func__, cc);
        return 1;
    }

    /* Setup parameter block: key1 + initial XTS value */
    memcpy(param, kmaesxts256key1, sizeof(kmaesxts256key1));
    memcpy(param + 32, init_xts, sizeof(init_xts));

    /* Encrypt */
    cpacf_km(CPACF_KM_XTS_256, param, output, kmaesxts256plain,
             sizeof(kmaesxts256plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaesxts256cipher, sizeof(kmaesxts256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_PXTS_128 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_pxts_128(void)
{
    uint8_t param[16 + 32 + 16]; /* protected key + wkvp + initial XTS value */
    uint8_t output[16];
    uint8_t init_xts[16];
    uint8_t key2_param[16 + 32];
    unsigned long cc = 0;
    int rc = 0;

    /* First compute initial XTS value using protected key2 and sector */
    memcpy(key2_param, kmaesxts128key2, sizeof(kmaesxts128key2));
    encrypt_clrkey(key2_param, sizeof(kmaesxts128key2));
    memcpy(key2_param + sizeof(kmaesxts128key2),
           protkey_wkvp, sizeof(protkey_wkvp));

    cpacf_km(CPACF_KM_PAES_128, key2_param, init_xts, kmaesxts128sect,
             sizeof(kmaesxts128sect), &cc);

    if (cc != 0) {
        printf("%s failed: initial XTS computation cc=%lu\n", __func__, cc);
        return 1;
    }

    /* Setup parameter block: protected key1 + wkvp + initial XTS value */
    memcpy(param, kmaesxts128key1, sizeof(kmaesxts128key1));
    encrypt_clrkey(param, sizeof(kmaesxts128key1));
    memcpy(param + sizeof(kmaesxts128key1), protkey_wkvp, sizeof(protkey_wkvp));
    memcpy(param + sizeof(kmaesxts128key1) + sizeof(protkey_wkvp),
           init_xts, sizeof(init_xts));

    /* Encrypt */
    cpacf_km(CPACF_KM_PXTS_128, param, output, kmaesxts128plain,
             sizeof(kmaesxts128plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaesxts128cipher, sizeof(kmaesxts128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KM_PXTS_256 test for km
 * returns > 0 on failure, otherwise 0
 */
static int test_km_pxts_256(void)
{
    uint8_t param[32 + 32 + 16]; /* protected key + wkvp + initial XTS value */
    uint8_t output[32];
    uint8_t init_xts[16];
    uint8_t key2_param[32 + 32];
    unsigned long cc = 0;
    int rc = 0;

    /* First compute initial XTS value using protected key2 and sector */
    memcpy(key2_param, kmaesxts256key2, sizeof(kmaesxts256key2));
    encrypt_clrkey(key2_param, sizeof(kmaesxts256key2));
    memcpy(key2_param + sizeof(kmaesxts256key2),
           protkey_wkvp, sizeof(protkey_wkvp));

    cpacf_km(CPACF_KM_PAES_256, key2_param, init_xts, kmaesxts256sect,
             sizeof(kmaesxts256sect), &cc);

    if (cc != 0) {
        printf("%s failed: initial XTS computation cc=%lu\n", __func__, cc);
        return 1;
    }

    /* Setup parameter block: protected key1 + wkvp + initial XTS value */
    memcpy(param, kmaesxts256key1, sizeof(kmaesxts256key1));
    encrypt_clrkey(param, sizeof(kmaesxts256key1));
    memcpy(param + sizeof(kmaesxts256key1), protkey_wkvp, sizeof(protkey_wkvp));
    memcpy(param + sizeof(kmaesxts256key1) + sizeof(protkey_wkvp),
           init_xts, sizeof(init_xts));

    /* Encrypt */
    cpacf_km(CPACF_KM_PXTS_256, param, output, kmaesxts256plain,
             sizeof(kmaesxts256plain), &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmaesxts256cipher, sizeof(kmaesxts256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

int main(void)
{
    int rc = 0;

    /* Test query function */
    rc += test_km_query();

    /* Test AES-128 */
    rc += test_km_aes_128();

    /* Test AES-192 */
    rc += test_km_aes_192();

    /* Test AES-256 */
    rc += test_km_aes_256();

    /* Test PAES-128 */
    rc += test_km_paes_128();

    /* Test PAES-192 */
    rc += test_km_paes_192();

    /* Test PAES-256 */
    rc += test_km_paes_256();

    /* Test XTS-128 */
    rc += test_km_xts_128();

    /* Test XTS-256 */
    rc += test_km_xts_256();

    /* Test PXTS-128 */
    rc += test_km_pxts_128();

    /* Test PXTS-256 */
    rc += test_km_pxts_256();

    if (rc) {
        printf("cpacf-km: %d failures\n", rc);
    }

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
