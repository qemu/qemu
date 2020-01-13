/*
 * QTest TPM utilities
 *
 * Copyright (c) 2018 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TESTS_TPM_UTIL_H
#define TESTS_TPM_UTIL_H

#include "io/channel-socket.h"

typedef void (tx_func)(QTestState *s,
                       const unsigned char *req, size_t req_size,
                       unsigned char *rsp, size_t rsp_size);

void tpm_util_crb_transfer(QTestState *s,
                           const unsigned char *req, size_t req_size,
                           unsigned char *rsp, size_t rsp_size);
void tpm_util_tis_transfer(QTestState *s,
                           const unsigned char *req, size_t req_size,
                           unsigned char *rsp, size_t rsp_size);

void tpm_util_startup(QTestState *s, tx_func *tx);
void tpm_util_pcrextend(QTestState *s, tx_func *tx);
void tpm_util_pcrread(QTestState *s, tx_func *tx,
                      const unsigned char *exp_resp, size_t exp_resp_size);

bool tpm_util_swtpm_has_tpm2(void);

gboolean tpm_util_swtpm_start(const char *path, GPid *pid,
                              SocketAddress **addr, GError **error);
void tpm_util_swtpm_kill(GPid pid);

void tpm_util_migrate(QTestState *who, const char *uri);

void tpm_util_migration_start_qemu(QTestState **src_qemu,
                                   QTestState **dst_qemu,
                                   SocketAddress *src_tpm_addr,
                                   SocketAddress *dst_tpm_addr,
                                   const char *miguri,
                                   const char *ifmodel);

void tpm_util_wait_for_migration_complete(QTestState *who);

#endif /* TESTS_TPM_UTIL_H */
