/*
 * QEMU crypto TLS credential support private helpers
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QCRYPTO_TLSCREDSPRIV_H
#define QCRYPTO_TLSCREDSPRIV_H

#include "crypto/tlscreds.h"

#ifdef CONFIG_GNUTLS

int qcrypto_tls_creds_get_path(QCryptoTLSCreds *creds,
                               const char *filename,
                               bool required,
                               char **cred,
                               Error **errp);

int qcrypto_tls_creds_get_dh_params_file(QCryptoTLSCreds *creds,
                                         const char *filename,
                                         gnutls_dh_params_t *dh_params,
                                         Error **errp);

#endif

#endif /* QCRYPTO_TLSCREDSPRIV_H */
