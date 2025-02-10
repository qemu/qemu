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

typedef struct QCryptoDerEncodeNode {
    uint8_t tag;
    struct QCryptoDerEncodeNode *parent;
    struct QCryptoDerEncodeNode *next;
    /* for constructed type, data is null */
    const uint8_t *data;
    size_t dlen;
} QCryptoDerEncodeNode;

typedef struct QCryptoEncodeContext {
    QCryptoDerEncodeNode root;
    QCryptoDerEncodeNode *current_parent;
    QCryptoDerEncodeNode *tail;
} QCryptoEncodeContext;

enum QCryptoDERTypeTag {
    QCRYPTO_DER_TYPE_TAG_BOOL = 0x1,
    QCRYPTO_DER_TYPE_TAG_INT = 0x2,
    QCRYPTO_DER_TYPE_TAG_BIT_STR = 0x3,
    QCRYPTO_DER_TYPE_TAG_OCT_STR = 0x4,
    QCRYPTO_DER_TYPE_TAG_NULL = 0x5,
    QCRYPTO_DER_TYPE_TAG_OID = 0x6,
    QCRYPTO_DER_TYPE_TAG_SEQ = 0x10,
    QCRYPTO_DER_TYPE_TAG_SET = 0x11,
};

enum QCryptoDERTagClass {
    QCRYPTO_DER_TAG_CLASS_UNIV = 0x0,
    QCRYPTO_DER_TAG_CLASS_APPL = 0x1,
    QCRYPTO_DER_TAG_CLASS_CONT = 0x2,
    QCRYPTO_DER_TAG_CLASS_PRIV = 0x3,
};

enum QCryptoDERTagEnc {
    QCRYPTO_DER_TAG_ENC_PRIM = 0x0,
    QCRYPTO_DER_TAG_ENC_CONS = 0x1,
};

#define QCRYPTO_DER_TAG_ENC_MASK 0x20
#define QCRYPTO_DER_TAG_ENC_SHIFT 5

#define QCRYPTO_DER_TAG_CLASS_MASK 0xc0
#define QCRYPTO_DER_TAG_CLASS_SHIFT 6

#define QCRYPTO_DER_TAG_VAL_MASK 0x1f
#define QCRYPTO_DER_SHORT_LEN_MASK 0x80

#define QCRYPTO_DER_TAG(class, enc, val)           \
    (((class) << QCRYPTO_DER_TAG_CLASS_SHIFT) |    \
     ((enc) << QCRYPTO_DER_TAG_ENC_SHIFT) | (val))

/**
 * qcrypto_der_encode_length:
 * @src_len: the length of source data
 * @dst: destination to save the encoded 'length', if dst is NULL, only compute
 * the expected buffer size in bytes.
 * @dst_len: output parameter, indicates how many bytes wrote.
 *
 * Encode the 'length' part of TLV tuple.
 */
static void qcrypto_der_encode_length(size_t src_len,
                                      uint8_t *dst, size_t *dst_len)
{
    size_t max_length = 0xFF;
    uint8_t length_bytes = 0, header_byte;

    if (src_len < QCRYPTO_DER_SHORT_LEN_MASK) {
        header_byte = src_len;
        *dst_len = 1;
    } else {
        for (length_bytes = 1; max_length < src_len; length_bytes++) {
            max_length = (max_length << 8) + max_length;
        }
        header_byte = length_bytes;
        header_byte |= QCRYPTO_DER_SHORT_LEN_MASK;
        *dst_len = length_bytes + 1;
    }
    if (!dst) {
        return;
    }
    *dst++ = header_byte;
    /* Bigendian length bytes */
    for (; length_bytes > 0; length_bytes--) {
        *dst++ = ((src_len >> (length_bytes - 1) * 8) & 0xFF);
    }
}

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

static int qcrypto_der_decode_tlv(const uint8_t expected_tag,
                                  const uint8_t **data, size_t *dlen,
                                  QCryptoDERDecodeCb cb,
                                  void *ctx, Error **errp)
{
    const uint8_t *saved_data = *data;
    size_t saved_dlen = *dlen;
    uint8_t tag;
    int data_length;

    if (*dlen < 1) {
        error_setg(errp, "Need more data");
        return -1;
    }
    tag = qcrypto_der_cut_byte(data, dlen);
    if (tag != expected_tag) {
        error_setg(errp, "Unexpected tag: expected: %u, actual: %u",
                   expected_tag, tag);
        goto error;
    }

    data_length = qcrypto_der_extract_data(data, dlen, cb, ctx, errp);
    if (data_length < 0) {
        goto error;
    }
    return data_length;

error:
    *data = saved_data;
    *dlen = saved_dlen;
    return -1;
}

int qcrypto_der_decode_int(const uint8_t **data, size_t *dlen,
                           QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    const uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                        QCRYPTO_DER_TAG_ENC_PRIM,
                                        QCRYPTO_DER_TYPE_TAG_INT);
    return qcrypto_der_decode_tlv(tag, data, dlen, cb, ctx, errp);
}

int qcrypto_der_decode_seq(const uint8_t **data, size_t *dlen,
                           QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_CONS,
                                  QCRYPTO_DER_TYPE_TAG_SEQ);
    return qcrypto_der_decode_tlv(tag, data, dlen, cb, ctx, errp);
}

int qcrypto_der_decode_octet_str(const uint8_t **data, size_t *dlen,
                                 QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_PRIM,
                                  QCRYPTO_DER_TYPE_TAG_OCT_STR);
    return qcrypto_der_decode_tlv(tag, data, dlen, cb, ctx, errp);
}

int qcrypto_der_decode_bit_str(const uint8_t **data, size_t *dlen,
                               QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_PRIM,
                                  QCRYPTO_DER_TYPE_TAG_BIT_STR);
    return qcrypto_der_decode_tlv(tag, data, dlen, cb, ctx, errp);
}

int qcrypto_der_decode_oid(const uint8_t **data, size_t *dlen,
                           QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_PRIM,
                                  QCRYPTO_DER_TYPE_TAG_OID);
    return qcrypto_der_decode_tlv(tag, data, dlen, cb, ctx, errp);
}

int qcrypto_der_decode_ctx_tag(const uint8_t **data, size_t *dlen, int tag_id,
                               QCryptoDERDecodeCb cb, void *ctx, Error **errp)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_CONT,
                                  QCRYPTO_DER_TAG_ENC_CONS,
                                  tag_id);
    return qcrypto_der_decode_tlv(tag, data, dlen, cb, ctx, errp);
}

static void qcrypto_der_encode_prim(QCryptoEncodeContext *ctx, uint8_t tag,
                                    const uint8_t *data, size_t dlen)
{
    QCryptoDerEncodeNode *node = g_new0(QCryptoDerEncodeNode, 1);
    size_t nbytes_len;

    node->tag = tag;
    node->data = data;
    node->dlen = dlen;
    node->parent = ctx->current_parent;

    qcrypto_der_encode_length(dlen, NULL, &nbytes_len);
    /* 1 byte for Tag, nbyte_len for Length, and dlen for Value */
    node->parent->dlen += 1 + nbytes_len + dlen;

    ctx->tail->next = node;
    ctx->tail = node;
}

QCryptoEncodeContext *qcrypto_der_encode_ctx_new(void)
{
    QCryptoEncodeContext *ctx = g_new0(QCryptoEncodeContext, 1);
    ctx->current_parent = &ctx->root;
    ctx->tail = &ctx->root;
    return ctx;
}

static void qcrypto_der_encode_cons_begin(QCryptoEncodeContext *ctx,
                                          uint8_t tag)
{
    QCryptoDerEncodeNode *node = g_new0(QCryptoDerEncodeNode, 1);

    node->tag = tag;
    node->parent = ctx->current_parent;
    ctx->current_parent = node;
    ctx->tail->next = node;
    ctx->tail = node;
}

static void qcrypto_der_encode_cons_end(QCryptoEncodeContext *ctx)
{
    QCryptoDerEncodeNode *cons_node = ctx->current_parent;
    size_t nbytes_len;

    qcrypto_der_encode_length(cons_node->dlen, NULL, &nbytes_len);
    /* 1 byte for Tag, nbyte_len for Length, and dlen for Value */
    cons_node->parent->dlen += 1 + nbytes_len + cons_node->dlen;
    ctx->current_parent = cons_node->parent;
}

void qcrypto_der_encode_seq_begin(QCryptoEncodeContext *ctx)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_CONS,
                                  QCRYPTO_DER_TYPE_TAG_SEQ);
    qcrypto_der_encode_cons_begin(ctx, tag);
}

void qcrypto_der_encode_seq_end(QCryptoEncodeContext *ctx)
{
    qcrypto_der_encode_cons_end(ctx);
}

void qcrypto_der_encode_oid(QCryptoEncodeContext *ctx,
                            const uint8_t *src, size_t src_len)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_PRIM,
                                  QCRYPTO_DER_TYPE_TAG_OID);
    qcrypto_der_encode_prim(ctx, tag, src, src_len);
}

void qcrypto_der_encode_int(QCryptoEncodeContext *ctx,
                            const uint8_t *src, size_t src_len)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_PRIM,
                                  QCRYPTO_DER_TYPE_TAG_INT);
    qcrypto_der_encode_prim(ctx, tag, src, src_len);
}

void qcrypto_der_encode_null(QCryptoEncodeContext *ctx)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_PRIM,
                                  QCRYPTO_DER_TYPE_TAG_NULL);
    qcrypto_der_encode_prim(ctx, tag, NULL, 0);
}

void qcrypto_der_encode_octet_str(QCryptoEncodeContext *ctx,
                                  const uint8_t *src, size_t src_len)
{
    uint8_t tag = QCRYPTO_DER_TAG(QCRYPTO_DER_TAG_CLASS_UNIV,
                                  QCRYPTO_DER_TAG_ENC_PRIM,
                                  QCRYPTO_DER_TYPE_TAG_OCT_STR);
    qcrypto_der_encode_prim(ctx, tag, src, src_len);
}

size_t qcrypto_der_encode_ctx_buffer_len(QCryptoEncodeContext *ctx)
{
    return ctx->root.dlen;
}

void qcrypto_der_encode_ctx_flush_and_free(QCryptoEncodeContext *ctx,
                                           uint8_t *dst)
{
    QCryptoDerEncodeNode *node, *prev;
    size_t len;

    for (prev = &ctx->root;
         (node = prev->next) && (prev->next = node->next, 1);) {
        /* Tag */
        *dst++ = node->tag;

        /* Length */
        qcrypto_der_encode_length(node->dlen, dst, &len);
        dst += len;

        /* Value */
        if (node->data) {
            memcpy(dst, node->data, node->dlen);
            dst += node->dlen;
        }
        g_free(node);
    }
    g_free(ctx);
}
