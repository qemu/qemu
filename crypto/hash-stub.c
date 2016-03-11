/*
 * QEMU Crypto hash algorithms
 *
 * Copyright (c) 2016 Red Hat, Inc.
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "crypto/hash.h"

gboolean qcrypto_hash_supports(QCryptoHashAlgorithm alg G_GNUC_UNUSED)
{
    return false;
}

int qcrypto_hash_bytesv(QCryptoHashAlgorithm alg,
                        const struct iovec *iov G_GNUC_UNUSED,
                        size_t niov G_GNUC_UNUSED,
                        uint8_t **result G_GNUC_UNUSED,
                        size_t *resultlen G_GNUC_UNUSED,
                        Error **errp)
{
    error_setg(errp,
               "Hash algorithm %d not supported without GNUTLS",
               alg);
    return -1;
}
