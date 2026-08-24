/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Simple test for the CPACF KDSA instruction
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpacf.h"

#define QUERY_BLOCK_SIZE 16

/* expected kdsa query block */
static uint8_t exp_query_block[QUERY_BLOCK_SIZE] = {
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static int test_kdsa_query(void)
{
    uint8_t query_block[QUERY_BLOCK_SIZE] = {0};
    unsigned long cc = 0;
    int i, rc = 0;

    cpacf_kdsa(CPACF_KDSA_QUERY, query_block, NULL, 0, &cc);

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

int main(void)
{
    int rc;

    /* Test query function */
    rc = test_kdsa_query();

    /* As of now only KDSA query is implemented */

    if (rc) {
        printf("cpacf-kdsa: %d failures\n", rc);
    }

    return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
