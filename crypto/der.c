/*
 * QEMU Crypto ASN.1 DER decoder
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
#include "crypto/der.h"

enum QCryptoDERTypeTag {
    QCRYPTO_DER_TYPE_TAG_BOOL = 0x1,
    QCRYPTO_DER_TYPE_TAG_INT = 0x2,
    QCRYPTO_DER_TYPE_TAG_BIT_STR = 0x3,
    QCRYPTO_DER_TYPE_TAG_OCT_STR = 0x4,
    QCRYPTO_DER_TYPE_TAG_OCT_NULL = 0x5,
    QCRYPTO_DER_TYPE_TAG_OCT_OID = 0x6,
    QCRYPTO_DER_TYPE_TAG_SEQ = 0x10,
    QCRYPTO_DER_TYPE_TAG_SET = 0x11,
};

#define QCRYPTO_DER_CONSTRUCTED_MASK 0x20
#define QCRYPTO_DER_SHORT_LEN_MASK 0x80

static uint8_t qcrypto_der_peek_byte(const uint8_t **data, size_t *dlen)
{
    return **data;
}

static void qcrypto_der_cut_nbytes(const uint8_t **data,
                                   size_t *dlen,
                                   size_t nbytes)
{
    *data += nbytes;
    *dlen -= nbytes;
}

static uint8_t qcrypto_der_cut_byte(const uint8_t **data, size_t *dlen)
{
    uint8_t val = qcrypto_der_peek_byte(data, dlen);

    qcrypto_der_cut_nbytes(data, dlen, 1);

    return val;
}

static int qcrypto_der_invoke_callback(QCryptoDERDecodeCb cb, void *ctx,
                                       const uint8_t *value, size_t vlen,
                                       Error **errp)
{
    if (!cb) {
        return 0;
    }

    return cb(ctx, value, vlen, errp);
}

static int qcrypto_der_extract_definite_data(const uint8_t **data, size_t *dlen,
                                             QCryptoDERDecodeCb cb, void *ctx,
                                             Error **errp)
{
    const uint8_t *value;
    size_t vlen = 0;
    uint8_t byte_count = qcrypto_der_cut_byte(data, dlen);

    /* short format of definite-length */
    if (!(byte_count & QCRYPTO_DER_SHORT_LEN_MASK)) {
        if (byte_count > *dlen) {
            error_setg(errp, "Invalid content length: %u", byte_count);
            return -1;
        }

        value = *data;
        vlen = byte_count;
        qcrypto_der_cut_nbytes(data, dlen, vlen);

        if (qcrypto_der_invoke_callback(cb, ctx, value, vlen, errp) != 0) {
            return -1;
        }
        return vlen;
    }

    /* Ignore highest bit */
    byte_count &= ~QCRYPTO_DER_SHORT_LEN_MASK;

    /*
     * size_t is enough to store the value of length, although the DER
     * encoding standard supports larger length.
     */
    if (byte_count > sizeof(size_t)) {
        error_setg(errp, "Invalid byte count of content length: %u",
                   byte_count);
        return -1;
    }

    if (byte_count > *dlen) {
        error_setg(errp, "Invalid content length: %u", byte_count);
        return -1;
    }
    while (byte_count--) {
        vlen <<= 8;
        vlen += qcrypto_der_cut_byte(data, dlen);
    }

    if (vlen > *dlen) {
        error_setg(errp, "Invalid content length: %zu", vlen);
        return -1;
    }

    value = *data;
    qcrypto_der_cut_nbytes(data, dlen, vlen);

    if (qcrypto_der_invoke_callback(cb, ctx, value, vlen, errp) != 0) {
        return -1;
    }
    return vlen;
}

static int qcrypto_der_extract_data(const uint8_t **data, size_t *dlen,
                                    QCryptoDERDecodeCb cb, void *ctx,
                                    Error **errp)
{
    uint8_t val;
    if (*dlen < 1) {
        error_setg(errp, "Need more data");
        return -1;
    }
    val = qcrypto_der_peek_byte(data, dlen);

    /* must use definite length format */
    if (val == QCRYPTO_DER_SHORT_LEN_MASK) {
        error_setg(errp, "Only definite length format is allowed");
        return -1;
    }

    return qcrypto_der_extract_definite_data(data, dlen, cb, ctx, errp);
}

int qcrypto_der_decode_int(const uint8_t **data, size_t *dlen,
                           QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    uint8_t tag;
    if (*dlen < 1) {
        error_setg(errp, "Need more data");
        return -1;
    }
    tag = qcrypto_der_cut_byte(data, dlen);

    /* INTEGER must encoded in primitive-form */
    if (tag != QCRYPTO_DER_TYPE_TAG_INT) {
        error_setg(errp, "Invalid integer type tag: %u", tag);
        return -1;
    }

    return qcrypto_der_extract_data(data, dlen, cb, ctx, errp);
}

int qcrypto_der_decode_seq(const uint8_t **data, size_t *dlen,
                           QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    uint8_t tag;
    if (*dlen < 1) {
        error_setg(errp, "Need more data");
        return -1;
    }
    tag = qcrypto_der_cut_byte(data, dlen);

    /* SEQUENCE must use constructed form */
    if (tag != (QCRYPTO_DER_TYPE_TAG_SEQ | QCRYPTO_DER_CONSTRUCTED_MASK)) {
        error_setg(errp, "Invalid type sequence tag: %u", tag);
        return -1;
    }

    return qcrypto_der_extract_data(data, dlen, cb, ctx, errp);
}
