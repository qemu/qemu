/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Simple test for CPACF KMCTR instruction
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpacf.h"

#define QUERY_BLOCK_SIZE 16

/* expected kmctr query block */
static uint8_t exp_query_block[QUERY_BLOCK_SIZE] = {
    0x80, 0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* KMCTR AES-128 test data */
static const uint8_t kmctraes128key[] = {
    0xed, 0xfd, 0xb2, 0x57, 0xcb, 0x37, 0xcd, 0xf1,
    0x82, 0xc5, 0x45, 0x5b, 0x0c, 0x0e, 0xfe, 0xbb
};
static const uint8_t kmctraes128plain[] = {
    0x16, 0x95, 0xfe, 0x47, 0x54, 0x21, 0xca, 0xce,
    0x35, 0x57, 0xda, 0xca, 0x01, 0xf4, 0x45, 0xff
};
static const uint8_t kmctraes128cipher[] = {
    0x78, 0x88, 0xbe, 0xae, 0x6e, 0x7a, 0x42, 0x63,
    0x32, 0xa7, 0xea, 0xa2, 0xf8, 0x08, 0xe6, 0x37
};

/* KMCTR AES-192 test data */
static const uint8_t kmctraes192key[] = {
    0x61, 0x39, 0x6c, 0x53, 0x0c, 0xc1, 0x74, 0x9a,
    0x5b, 0xab, 0x6f, 0xbc, 0xf9, 0x06, 0xfe, 0x67,
    0x2d, 0x0c, 0x4a, 0xb2, 0x01, 0xaf, 0x45, 0x54
};
static const uint8_t kmctraes192plain[] = {
    0x60, 0xbc, 0xdb, 0x94, 0x16, 0xba, 0xc0, 0x8d,
    0x7f, 0xd0, 0xd7, 0x80, 0x35, 0x37, 0x40, 0xa5
};
static const uint8_t kmctraes192cipher[] = {
    0x24, 0xf4, 0x0c, 0x4e, 0xec, 0xd9, 0xc4, 0x98,
    0x25, 0x00, 0x0f, 0xcb, 0x49, 0x72, 0x64, 0x7a
};

/* KMCTR AES-256 test data */
static const uint8_t kmctraes256key[] = {
    0xcc, 0x22, 0xda, 0x78, 0x7f, 0x37, 0x57, 0x11,
    0xc7, 0x63, 0x02, 0xbe, 0xf0, 0x97, 0x9d, 0x8e,
    0xdd, 0xf8, 0x42, 0x82, 0x9c, 0x2b, 0x99, 0xef,
    0x3d, 0xd0, 0x4e, 0x23, 0xe5, 0x4c, 0xc2, 0x4b
};
static const uint8_t kmctraes256plain[] = {
    0xcc, 0xc6, 0x2c, 0x6b, 0x0a, 0x09, 0xa6, 0x71,
    0xd6, 0x44, 0x56, 0x81, 0x8d, 0xb2, 0x9a, 0x4d
};
static const uint8_t kmctraes256cipher[] = {
    0xdf, 0x86, 0x34, 0xca, 0x02, 0xb1, 0x3a, 0x12,
    0x5b, 0x78, 0x6e, 0x1d, 0xce, 0x90, 0x65, 0x8b
};

/* static byte array containing the WKVP */
static const uint8_t protkey_wkvp[32] = PROTKEY_WKVP;

/*
 * Query test for kmctr
 * returns > 0 on failure, otherwise 0
 */
static int test_kmctr_query(void)
{
    uint8_t query_block[QUERY_BLOCK_SIZE] = {0};
    unsigned long cc = 0;
    int i, rc = 0;

    cpacf_kmctr(CPACF_KMCTR_QUERY, query_block, NULL, NULL, 0, NULL, &cc);

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
 * Subfunction CPACF_KMCTR_AES_128 test for kmctr
 * returns > 0 on failure, otherwise 0
 */
static int test_kmctr_aes_128(void)
{
    uint8_t param[16];      /* Parameter block: AES-128 key */
    uint8_t src[16] = {0};  /* Source data (zeros for this test) */
    uint8_t counter[16];    /* Counter value */
    uint8_t output[16];     /* Output buffer */
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key only */
    memcpy(param, kmctraes128key, sizeof(kmctraes128key));

    /* Setup counter buffer */
    memcpy(counter, kmctraes128plain, sizeof(kmctraes128plain));

    /* En/Decrypt src with given counter, note that src is all zero */
    cpacf_kmctr(CPACF_KMCTR_AES_128, param, output, src,
                sizeof(src), counter, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmctraes128cipher, sizeof(kmctraes128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMCTR_AES_192 test for kmctr
 * returns > 0 on failure, otherwise 0
 */
static int test_kmctr_aes_192(void)
{
    uint8_t param[24];      /* Parameter block: AES-192 key */
    uint8_t src[16] = {0};  /* Source data (zeros for this test) */
    uint8_t counter[16];    /* Counter value */
    uint8_t output[16];     /* Output buffer */
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key only */
    memcpy(param, kmctraes192key, sizeof(kmctraes192key));

    /* Setup counter buffer */
    memcpy(counter, kmctraes192plain, sizeof(kmctraes192plain));

    /* En/Decrypt src with given counter, note that src is all zero */
    cpacf_kmctr(CPACF_KMCTR_AES_192, param, output, src,
                sizeof(src), counter, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmctraes192cipher, sizeof(kmctraes192cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMCTR_AES_256 test for kmctr
 * returns > 0 on failure, otherwise 0
 */
static int test_kmctr_aes_256(void)
{
    uint8_t param[32];      /* Parameter block: AES-256 key */
    uint8_t src[16] = {0};  /* Source data (zeros for this test) */
    uint8_t counter[16];    /* Counter value */
    uint8_t output[16];     /* Output buffer */
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key only */
    memcpy(param, kmctraes256key, sizeof(kmctraes256key));

    /* Setup counter buffer */
    memcpy(counter, kmctraes256plain, sizeof(kmctraes256plain));

    /* En/Decrypt src with given counter, note that src is all zero */
    cpacf_kmctr(CPACF_KMCTR_AES_256, param, output, src,
                sizeof(src), counter, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmctraes256cipher, sizeof(kmctraes256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMCTR_PAES_128 test for kmctr
 * returns > 0 on failure, otherwise 0
 */
static int test_kmctr_paes_128(void)
{
    uint8_t param[16 + 32];  /* Parameter block: protected key + wkvp */
    uint8_t src[16] = {0};   /* Source data (zeros for this test) */
    uint8_t counter[16];     /* Counter value */
    uint8_t output[16];      /* Output buffer */
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp */
    memcpy(param, kmctraes128key, sizeof(kmctraes128key));
    encrypt_clrkey(param, sizeof(kmctraes128key));
    memcpy(param + sizeof(kmctraes128key), protkey_wkvp, sizeof(protkey_wkvp));

    /* Setup counter buffer */
    memcpy(counter, kmctraes128plain, sizeof(kmctraes128plain));

    /* En/Decrypt src with given counter, note that src is all zero */
    cpacf_kmctr(CPACF_KMCTR_PAES_128, param, output, src,
                sizeof(src), counter, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmctraes128cipher, sizeof(kmctraes128cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMCTR_PAES_192 test for kmctr
 * returns > 0 on failure, otherwise 0
 */
static int test_kmctr_paes_192(void)
{
    uint8_t param[24 + 32];  /* Parameter block: protected key + wkvp */
    uint8_t src[16] = {0};   /* Source data (zeros for this test) */
    uint8_t counter[16];     /* Counter value */
    uint8_t output[16];      /* Output buffer */
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp */
    memcpy(param, kmctraes192key, sizeof(kmctraes192key));
    encrypt_clrkey(param, sizeof(kmctraes192key));
    memcpy(param + sizeof(kmctraes192key), protkey_wkvp, sizeof(protkey_wkvp));

    /* Setup counter buffer */
    memcpy(counter, kmctraes192plain, sizeof(kmctraes192plain));

    /* En/Decrypt src with given counter, note that src is all zero */
    cpacf_kmctr(CPACF_KMCTR_PAES_192, param, output, src,
                sizeof(src), counter, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmctraes192cipher, sizeof(kmctraes192cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_KMCTR_PAES_256 test for kmctr
 * returns > 0 on failure, otherwise 0
 */
static int test_kmctr_paes_256(void)
{
    uint8_t param[32 + 32];  /* Parameter block: protected key + wkvp */
    uint8_t src[16] = {0};   /* Source data (zeros for this test) */
    uint8_t counter[16];     /* Counter value */
    uint8_t output[16];      /* Output buffer */
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp */
    memcpy(param, kmctraes256key, sizeof(kmctraes256key));
    encrypt_clrkey(param, sizeof(kmctraes256key));
    memcpy(param + sizeof(kmctraes256key), protkey_wkvp, sizeof(protkey_wkvp));

    /* Setup counter buffer */
    memcpy(counter, kmctraes256plain, sizeof(kmctraes256plain));

    /* En/Decrypt src with given counter, note that src is all zero */
    cpacf_kmctr(CPACF_KMCTR_PAES_256, param, output, src,
                sizeof(src), counter, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare result with expected ciphertext */
    if (memcmp(output, kmctraes256cipher, sizeof(kmctraes256cipher))) {
        printf("%s failed: output mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

int main(void)
{
    int rc = 0;

    /* Test query function */
    rc += test_kmctr_query();

    /* Test AES-128 */
    rc += test_kmctr_aes_128();

    /* Test AES-192 */
    rc += test_kmctr_aes_192();

    /* Test AES-256 */
    rc += test_kmctr_aes_256();

    /* Test PAES-128 */
    rc += test_kmctr_paes_128();

    /* Test PAES-192 */
    rc += test_kmctr_paes_192();

    /* Test PAES-256 */
    rc += test_kmctr_paes_256();

    if (rc) {
        printf("cpacf-kmctr: %d failures\n", rc);
    }

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
