/*
 * QEMU Crypto PBKDF support (Password-Based Key Derivation Function)
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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
#include "crypto/pbkdf.h"

bool qcrypto_pbkdf2_supports(QCryptoHashAlgorithm hash G_GNUC_UNUSED)
{
    return false;
}

int qcrypto_pbkdf2(QCryptoHashAlgorithm hash G_GNUC_UNUSED,
                   const uint8_t *key G_GNUC_UNUSED,
                   size_t nkey G_GNUC_UNUSED,
                   const uint8_t *salt G_GNUC_UNUSED,
                   size_t nsalt G_GNUC_UNUSED,
                   uint64_t iterations G_GNUC_UNUSED,
                   uint8_t *out G_GNUC_UNUSED,
                   size_t nout G_GNUC_UNUSED,
                   Error **errp)
{
    error_setg_errno(errp, ENOSYS,
                     "No crypto library supporting PBKDF in this build");
    return -1;
}
