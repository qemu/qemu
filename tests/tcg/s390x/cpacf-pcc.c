/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Simple test for CPACF PCC instruction
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpacf.h"

#define QUERY_BLOCK_SIZE 16

/* expected pcc query block */
static uint8_t exp_query_block[QUERY_BLOCK_SIZE] = {
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x28,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* PCC XTS AES-128 test data */
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

/* PCC XTS AES-256 test data */
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

/* static byte array containing the WKVP */
static const uint8_t protkey_wkvp[32] = PROTKEY_WKVP;

/*
 * Query test for pcc
 * returns > 0 on failure, otherwise 0
 */
static int test_pcc_query(void)
{
    uint8_t query_block[QUERY_BLOCK_SIZE] = {0};
    unsigned long cc = 0;
    int i, rc = 0;

    cpacf_pcc(CPACF_PCC_QUERY, query_block, &cc);

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
 * Subfunction CPACF_PCC_XTS_AES_128 test for pcc
 * returns > 0 on failure, otherwise 0
 */
static int test_pcc_xts_aes_128(void)
{
    uint8_t param[80];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key + plaintext + zeros */
    memcpy(param, kmaes128key, sizeof(kmaes128key));
    memcpy(param + 16, kmaes128plain, sizeof(kmaes128plain));
    /* Clear Block Sequential Nr, Intermediate Bit Index, and XTS Parameter */
    memset(param + 32, 0, 48);

    /* Execute PCC to compute XTS parameter */
    cpacf_pcc(CPACF_PCC_XTS_AES_128, param, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare computed XTS parameter (at offset 64) with expected cipher */
    if (memcmp(param + 64, kmaes128cipher, sizeof(kmaes128cipher))) {
        printf("%s failed: XTS parameter mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_PCC_XTS_AES_256 test for pcc
 * returns > 0 on failure, otherwise 0
 */
static int test_pcc_xts_aes_256(void)
{
    uint8_t param[96];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: key + plaintext + zeros */
    memcpy(param, kmaes256key, sizeof(kmaes256key));
    memcpy(param + 32, kmaes256plain, sizeof(kmaes256plain));
    /* Clear Block Sequential Nr, Intermediate Bit Index, and XTS Parameter */
    memset(param + 48, 0, 48);

    /* Execute PCC to compute XTS parameter */
    cpacf_pcc(CPACF_PCC_XTS_AES_256, param, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare computed XTS parameter (at offset 80) with expected cipher */
    if (memcmp(param + 80, kmaes256cipher, sizeof(kmaes256cipher))) {
        printf("%s failed: XTS parameter mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_PCC_XTS_PAES_128 test for pcc
 * returns > 0 on failure, otherwise 0
 */
static int test_pcc_xts_paes_128(void)
{
    uint8_t param[112];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp + plaintext + zeros */
    memcpy(param, kmaes128key, sizeof(kmaes128key));
    encrypt_clrkey(param, sizeof(kmaes128key));
    memcpy(param + 16, protkey_wkvp, sizeof(protkey_wkvp));
    memcpy(param + 48, kmaes128plain, sizeof(kmaes128plain));
    /* Clear Block Sequential Nr, Intermediate Bit Index, and XTS Parameter */
    memset(param + 64, 0, 48);

    /* Execute PCC to compute XTS parameter */
    cpacf_pcc(CPACF_PCC_XTS_PAES_128, param, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare computed XTS parameter (at offset 96) with expected cipher */
    if (memcmp(param + 96, kmaes128cipher, sizeof(kmaes128cipher))) {
        printf("%s failed: XTS parameter mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

/*
 * Subfunction CPACF_PCC_XTS_PAES_256 test for pcc
 * returns > 0 on failure, otherwise 0
 */
static int test_pcc_xts_paes_256(void)
{
    uint8_t param[128];
    unsigned long cc = 0;
    int rc = 0;

    /* Setup parameter block: protected key + wkvp + plaintext + zeros */
    memcpy(param, kmaes256key, sizeof(kmaes256key));
    encrypt_clrkey(param, sizeof(kmaes256key));
    memcpy(param + 32, protkey_wkvp, sizeof(protkey_wkvp));
    memcpy(param + 64, kmaes256plain, sizeof(kmaes256plain));
    /* Clear Block Sequential Nr, Intermediate Bit Index, and XTS Parameter */
    memset(param + 80, 0, 48);

    /* Execute PCC to compute XTS parameter */
    cpacf_pcc(CPACF_PCC_XTS_PAES_256, param, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu\n", __func__, cc);
        rc = 1;
    }

    /* Compare computed XTS parameter (at offset 112) with expected cipher */
    if (memcmp(param + 112, kmaes256cipher, sizeof(kmaes256cipher))) {
        printf("%s failed: XTS parameter mismatch\n", __func__);
        rc = 1;
    }

    return rc;
}

int main(void)
{
    int rc = 0;

    /* Test query function */
    rc += test_pcc_query();

    /* Test XTS-AES-128 */
    rc += test_pcc_xts_aes_128();

    /* Test XTS-AES-256 */
    rc += test_pcc_xts_aes_256();

    /* Test XTS-PAES-128 */
    rc += test_pcc_xts_paes_128();

    /* Test XTS-PAES-256 */
    rc += test_pcc_xts_paes_256();

    if (rc) {
        printf("cpacf-pcc: %d failures\n", rc);
    }

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
