/*
 * QEMU Crypto anti forensic information splitter
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QCRYPTO_AFSPLIT_H
#define QCRYPTO_AFSPLIT_H

#include "crypto/hash.h"

/**
 * This module implements the anti-forensic splitter that is specified
 * as part of the LUKS format:
 *
 *   http://clemens.endorphin.org/cryptography
 *   http://clemens.endorphin.org/TKS1-draft.pdf
 *
 * The core idea is to take a short piece of data (key material)
 * and process it to expand it to a much larger piece of data.
 * The expansion process is reversible, to obtain the original
 * short data. The key property of the expansion is that if any
 * byte in the larger data set is changed / missing, it should be
 * impossible to recreate the original short data.
 *
 * <example>
 *    <title>Creating a large split key for storage</title>
 *    <programlisting>
 * size_t nkey = 32;
 * uint32_t stripes = 32768; // To produce a 1 MB split key
 * uint8_t *masterkey = ....a 32-byte AES key...
 * uint8_t *splitkey;
 *
 * splitkey = g_new0(uint8_t, nkey * stripes);
 *
 * if (qcrypto_afsplit_encode(QCRYPTO_HASH_ALG_SHA256,
 *                            nkey, stripes,
 *                            masterkey, splitkey, errp) < 0) {
 *     g_free(splitkey);
 *     g_free(masterkey);
 *     return -1;
 * }
 *
 * ...store splitkey somewhere...
 *
 * g_free(splitkey);
 * g_free(masterkey);
 *    </programlisting>
 * </example>
 *
 * <example>
 *    <title>Retrieving a master key from storage</title>
 *    <programlisting>
 * size_t nkey = 32;
 * uint32_t stripes = 32768; // To produce a 1 MB split key
 * uint8_t *masterkey;
 * uint8_t *splitkey = .... read in 1 MB of data...
 *
 * masterkey = g_new0(uint8_t, nkey);
 *
 * if (qcrypto_afsplit_decode(QCRYPTO_HASH_ALG_SHA256,
 *                            nkey, stripes,
 *                            splitkey, masterkey, errp) < 0) {
 *     g_free(splitkey);
 *     g_free(masterkey);
 *     return -1;
 * }
 *
 * ..decrypt data with masterkey...
 *
 * g_free(splitkey);
 * g_free(masterkey);
 *    </programlisting>
 * </example>
 */

/**
 * qcrypto_afsplit_encode:
 * @hash: the hash algorithm to use for data expansion
 * @blocklen: the size of @in in bytes
 * @stripes: the number of times to expand @in in size
 * @in: the master key to be expanded in size
 * @out: preallocated buffer to hold the split key
 * @errp: pointer to a NULL-initialized error object
 *
 * Split the data in @in, which is @blocklen bytes in
 * size, to form a larger piece of data @out, which is
 * @blocklen * @stripes bytes in size.
 *
 * Returns: 0 on success, -1 on error;
 */
int qcrypto_afsplit_encode(QCryptoHashAlgorithm hash,
                           size_t blocklen,
                           uint32_t stripes,
                           const uint8_t *in,
                           uint8_t *out,
                           Error **errp);

/**
 * qcrypto_afsplit_decode:
 * @hash: the hash algorithm to use for data compression
 * @blocklen: the size of @out in bytes
 * @stripes: the number of times to decrease @in in size
 * @in: the split key to be recombined
 * @out: preallocated buffer to hold the master key
 * @errp: pointer to a NULL-initialized error object
 *
 * Join the data in @in, which is @blocklen * @stripes
 * bytes in size, to form the original small piece of
 * data @out, which is @blocklen bytes in size.
 *
 * Returns: 0 on success, -1 on error;
 */
int qcrypto_afsplit_decode(QCryptoHashAlgorithm hash,
                           size_t blocklen,
                           uint32_t stripes,
                           const uint8_t *in,
                           uint8_t *out,
                           Error **errp);

#endif /* QCRYPTO_AFSPLIT_H */
