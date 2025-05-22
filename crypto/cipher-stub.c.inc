/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU Crypto cipher impl stub
 *
 * Copyright (c) 2025 Red Hat, Inc.
 *
 */

bool qcrypto_cipher_supports(QCryptoCipherAlgo alg,
                             QCryptoCipherMode mode)
{
    return false;
}

static QCryptoCipher *qcrypto_cipher_ctx_new(QCryptoCipherAlgo alg,
                                             QCryptoCipherMode mode,
                                             const uint8_t *key,
                                             size_t nkey,
                                             Error **errp)
{
    if (!qcrypto_cipher_validate_key_length(alg, mode, nkey, errp)) {
        return NULL;
    }

    error_setg(errp,
               "Unsupported cipher algorithm %s, no crypto library enabled in build",
               QCryptoCipherAlgo_str(alg));
    return NULL;
}
