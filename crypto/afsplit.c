/*
 * QEMU Crypto anti forensic information splitter
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * Derived from cryptsetup package lib/luks1/af.c
 *
 * Copyright (C) 2004, Clemens Fruhwirth <clemens@endorphin.org>
 * Copyright (C) 2009-2012, Red Hat, Inc. All rights reserved.
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

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "crypto/afsplit.h"
#include "crypto/random.h"


static void qcrypto_afsplit_xor(size_t blocklen,
                                const uint8_t *in1,
                                const uint8_t *in2,
                                uint8_t *out)
{
    size_t i;
    for (i = 0; i < blocklen; i++) {
        out[i] = in1[i] ^ in2[i];
    }
}


static int qcrypto_afsplit_hash(QCryptoHashAlgorithm hash,
                                size_t blocklen,
                                uint8_t *block,
                                Error **errp)
{
    size_t digestlen = qcrypto_hash_digest_len(hash);

    size_t hashcount = blocklen / digestlen;
    size_t finallen = blocklen % digestlen;
    uint32_t i;

    if (finallen) {
        hashcount++;
    } else {
        finallen = digestlen;
    }

    for (i = 0; i < hashcount; i++) {
        uint8_t *out = NULL;
        size_t outlen = 0;
        uint32_t iv = cpu_to_be32(i);
        struct iovec in[] = {
            { .iov_base = &iv,
              .iov_len = sizeof(iv) },
            { .iov_base = block + (i * digestlen),
              .iov_len = (i == (hashcount - 1)) ? finallen : digestlen },
        };

        if (qcrypto_hash_bytesv(hash,
                                in,
                                G_N_ELEMENTS(in),
                                &out, &outlen,
                                errp) < 0) {
            return -1;
        }

        assert(outlen == digestlen);
        memcpy(block + (i * digestlen), out,
               (i == (hashcount - 1)) ? finallen : digestlen);
        g_free(out);
    }

    return 0;
}


int qcrypto_afsplit_encode(QCryptoHashAlgorithm hash,
                           size_t blocklen,
                           uint32_t stripes,
                           const uint8_t *in,
                           uint8_t *out,
                           Error **errp)
{
    uint8_t *block = g_new0(uint8_t, blocklen);
    size_t i;
    int ret = -1;

    for (i = 0; i < (stripes - 1); i++) {
        if (qcrypto_random_bytes(out + (i * blocklen), blocklen, errp) < 0) {
            goto cleanup;
        }

        qcrypto_afsplit_xor(blocklen,
                            out + (i * blocklen),
                            block,
                            block);
        if (qcrypto_afsplit_hash(hash, blocklen, block,
                                 errp) < 0) {
            goto cleanup;
        }
    }
    qcrypto_afsplit_xor(blocklen,
                        in,
                        block,
                        out + (i * blocklen));
    ret = 0;

 cleanup:
    g_free(block);
    return ret;
}


int qcrypto_afsplit_decode(QCryptoHashAlgorithm hash,
                           size_t blocklen,
                           uint32_t stripes,
                           const uint8_t *in,
                           uint8_t *out,
                           Error **errp)
{
    uint8_t *block = g_new0(uint8_t, blocklen);
    size_t i;
    int ret = -1;

    for (i = 0; i < (stripes - 1); i++) {
        qcrypto_afsplit_xor(blocklen,
                            in + (i * blocklen),
                            block,
                            block);
        if (qcrypto_afsplit_hash(hash, blocklen, block,
                                 errp) < 0) {
            goto cleanup;
        }
    }

    qcrypto_afsplit_xor(blocklen,
                        in + (i * blocklen),
                        block,
                        out);

    ret = 0;

 cleanup:
    g_free(block);
    return ret;
}
