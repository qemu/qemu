/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Simple test for CPACF PRNO instruction
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cpacf.h"

#define QUERY_BLOCK_SIZE 16
#define TRNG_OUTPUT_SIZE 32

/* expected prno query block */
static uint8_t exp_query_block[QUERY_BLOCK_SIZE] = {
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
};

/*
 * Query test for prno
 * returns > 0 on failure, otherwise 0
 */
static int test_prno_query(void)
{
    uint8_t query_block[QUERY_BLOCK_SIZE] = {0};
    unsigned long cc = 0;
    int i, rc = 0;

    cpacf_prno(CPACF_PRNO_QUERY, query_block, NULL, 0, NULL, 0, &cc);

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

/* check for buffer is all zero */
static bool is_all_zeros(const uint8_t *buf, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (buf[i] != 0) {
            return false;
        }
    }

    return true;
}

/*
 * Subfunction CPACF_PRNO_TRNG test for prno
 * returns > 0 on failure, otherwise 0
 */
static int test_prno_trng(void)
{
    uint8_t output1[TRNG_OUTPUT_SIZE];
    uint8_t output2[TRNG_OUTPUT_SIZE];
    unsigned long cc = 0;
    int rc = 0;

    /* Initialize outputs to detect if they get filled */
    memset(output1, 0, sizeof(output1));
    memset(output2, 0, sizeof(output2));

    /* First TRNG call */
    cpacf_prno(CPACF_PRNO_TRNG, NULL, output1, sizeof(output1), NULL, 0, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu on first call\n", __func__, cc);
        rc = 1;
    }
    /* Verify output is not all zeros */
    if (is_all_zeros(output1, TRNG_OUTPUT_SIZE)) {
        printf("%s failed: output1 is all zeros\n", __func__);
        rc = 1;
    }

    /* Second TRNG call */
    cpacf_prno(CPACF_PRNO_TRNG, NULL, output2, sizeof(output2), NULL, 0, &cc);

    /* Check for correct condition code */
    if (cc != 0) {
        printf("%s failed: unexpected cc=%lu on second call\n", __func__, cc);
        rc = 1;
    }
    /* Verify output is not all zeros */
    if (is_all_zeros(output2, TRNG_OUTPUT_SIZE)) {
        printf("%s failed: output2 is all zeros\n", __func__);
        rc = 1;
    }

    /* Verify the two outputs are different */
    if (memcmp(output1, output2, TRNG_OUTPUT_SIZE) == 0) {
        printf("%s failed: two TRNG calls produced same output\n", __func__);
        rc = 1;
    }

    return rc;
}

int main(void)
{
    int rc = 0;

    /* Test query function */
    rc += test_prno_query();

    /* Test TRNG */
    rc += test_prno_trng();

    if (rc) {
        printf("cpacf-prno: %d failures\n", rc);
    }

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
