/*
 * QTest TPM TIS: Common test functions used for both the
 * ISA and SYSBUS devices
 *
 * Copyright (c) 2018 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TESTS_TPM_TIS_UTIL_H
#define TESTS_TPM_TIS_UTIL_H

void tpm_tis_test_check_localities(const void *data);
void tpm_tis_test_check_access_reg(const void *data);
void tpm_tis_test_check_access_reg_seize(const void *data);
void tpm_tis_test_check_access_reg_release(const void *data);
void tpm_tis_test_check_transmit(const void *data);

void tpm_tis_transfer(QTestState *s,
                      const unsigned char *req, size_t req_size,
                      unsigned char *rsp, size_t rsp_size);

#endif /* TESTS_TPM_TIS_UTIL_H */
