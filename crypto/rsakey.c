/*
 * QEMU Crypto RSA key parser
 *
 * Copyright (c) 2022 Bytedance
 * Author: lei he <helei.sig11@bytedance.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "der.h"
#include "rsakey.h"

void qcrypto_akcipher_rsakey_free(QCryptoAkCipherRSAKey *rsa_key)
{
    if (!rsa_key) {
        return;
    }
    g_free(rsa_key->n.data);
    g_free(rsa_key->e.data);
    g_free(rsa_key->d.data);
    g_free(rsa_key->p.data);
    g_free(rsa_key->q.data);
    g_free(rsa_key->dp.data);
    g_free(rsa_key->dq.data);
    g_free(rsa_key->u.data);
    g_free(rsa_key);
}

/**
 * PKCS#8 private key info for RSA
 *
 * PrivateKeyInfo ::= SEQUENCE {
 * version         INTEGER,
 * privateKeyAlgorithm PrivateKeyAlgorithmIdentifier,
 * privateKey      OCTET STRING,
 * attributes      [0] IMPLICIT Attributes OPTIONAL
 * }
 */
void qcrypto_akcipher_rsakey_export_p8info(const uint8_t *key,
                                           size_t keylen,
                                           uint8_t **dst,
                                           size_t *dlen)
{
    QCryptoEncodeContext *ctx = qcrypto_der_encode_ctx_new();
    uint8_t version = 0;

    qcrypto_der_encode_seq_begin(ctx);

    /* version */
    qcrypto_der_encode_int(ctx, &version, sizeof(version));

    /* algorithm identifier */
    qcrypto_der_encode_seq_begin(ctx);
    qcrypto_der_encode_oid(ctx, (uint8_t *)QCRYPTO_OID_rsaEncryption,
                           sizeof(QCRYPTO_OID_rsaEncryption) - 1);
    qcrypto_der_encode_null(ctx);
    qcrypto_der_encode_seq_end(ctx);

    /* RSA private key */
    qcrypto_der_encode_octet_str(ctx, key, keylen);

    qcrypto_der_encode_seq_end(ctx);

    *dlen = qcrypto_der_encode_ctx_buffer_len(ctx);
    *dst = g_malloc(*dlen);
    qcrypto_der_encode_ctx_flush_and_free(ctx, *dst);
}

#if defined(CONFIG_NETTLE) && defined(CONFIG_HOGWEED)
#include "rsakey-nettle.c.inc"
#else
#include "rsakey-builtin.c.inc"
#endif
