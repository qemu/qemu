/*
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

#ifndef QCRYPTO_ASN1_DECODER_H
#define QCRYPTO_ASN1_DECODER_H

#include "qapi/error.h"

typedef struct QCryptoEncodeContext QCryptoEncodeContext;

/* rsaEncryption: 1.2.840.113549.1.1.1 */
#define QCRYPTO_OID_rsaEncryption "\x2A\x86\x48\x86\xF7\x0D\x01\x01\x01"

/* Simple decoder used to parse DER encoded rsa keys. */

/**
 *  @opaque: user context.
 *  @value: the starting address of |value| part of 'Tag-Length-Value' pattern.
 *  @vlen: length of the |value|.
 *  Returns: 0 for success, any other value is considered an error.
 */
typedef int (*QCryptoDERDecodeCb) (void *opaque, const uint8_t *value,
                                   size_t vlen, Error **errp);

/**
 * qcrypto_der_decode_int:
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Decode integer from DER-encoded data.
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded INTEGER will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_int(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);
/**
 * qcrypto_der_decode_seq:
 *
 * Decode sequence from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded SEQUENCE will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_seq(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);

/**
 * qcrypto_der_decode_oid:
 *
 * Decode OID from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded OID will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_oid(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);

/**
 * qcrypto_der_decode_octet_str:
 *
 * Decode OCTET STRING from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded OCTET STRING will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_octet_str(const uint8_t **data,
                                 size_t *dlen,
                                 QCryptoDERDecodeCb cb,
                                 void *opaque,
                                 Error **errp);

/**
 * qcrypto_der_decode_bit_str:
 *
 * Decode BIT STRING from DER-encoded data, similar with der_decode_int.
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded BIT STRING will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_bit_str(const uint8_t **data,
                               size_t *dlen,
                               QCryptoDERDecodeCb cb,
                               void *opaque,
                               Error **errp);


/**
 * qcrypto_der_decode_ctx_tag:
 *
 * Decode context specific tag
 *
 * @data: pointer to address of input data
 * @dlen: pointer to length of input data
 * @tag: expected value of context specific tag
 * @cb: callback invoked when decode succeed, if cb equals NULL, no
 * callback will be invoked
 * @opaque: parameter passed to cb
 *
 * Returns: On success, *data points to rest data, and *dlen
 * will be set to the rest length of data, if cb is not NULL, must
 * return 0 to make decode success, at last, the length of the data
 * part of the decoded BIT STRING will be returned. Otherwise, -1 is
 * returned and the valued of *data and *dlen keep unchanged.
 */
int qcrypto_der_decode_ctx_tag(const uint8_t **data,
                               size_t *dlen, int tag_id,
                               QCryptoDERDecodeCb cb,
                               void *opaque,
                               Error **errp);

/**
 * qcrypto_der_encode_ctx_new:
 *
 * Allocate a context used for der encoding.
 */
QCryptoEncodeContext *qcrypto_der_encode_ctx_new(void);

/**
 * qcrypto_der_encode_seq_begin:
 * @ctx: the encode context.
 *
 * Start encoding a SEQUENCE for ctx.
 *
 */
void qcrypto_der_encode_seq_begin(QCryptoEncodeContext *ctx);

/**
 * qcrypto_der_encode_seq_begin:
 * @ctx: the encode context.
 *
 * Finish uencoding a SEQUENCE for ctx.
 *
 */
void qcrypto_der_encode_seq_end(QCryptoEncodeContext *ctx);


/**
 * qcrypto_der_encode_oid:
 * @ctx: the encode context.
 * @src: the source data of oid, note it should be already encoded, this
 * function only add tag and length part for it.
 *
 * Encode an oid into ctx.
 */
void qcrypto_der_encode_oid(QCryptoEncodeContext *ctx,
                            const uint8_t *src, size_t src_len);

/**
 * qcrypto_der_encode_int:
 * @ctx: the encode context.
 * @src: the source data of integer, note it should be already encoded, this
 * function only add tag and length part for it.
 *
 * Encode an integer into ctx.
 */
void qcrypto_der_encode_int(QCryptoEncodeContext *ctx,
                            const uint8_t *src, size_t src_len);

/**
 * qcrypto_der_encode_null:
 * @ctx: the encode context.
 *
 * Encode a null into ctx.
 */
void qcrypto_der_encode_null(QCryptoEncodeContext *ctx);

/**
 * qcrypto_der_encode_octet_str:
 * @ctx: the encode context.
 * @src: the source data of the octet string.
 *
 * Encode a octet string into ctx.
 */
void qcrypto_der_encode_octet_str(QCryptoEncodeContext *ctx,
                                  const uint8_t *src, size_t src_len);

/**
 * qcrypto_der_encode_octet_str_begin:
 * @ctx: the encode context.
 *
 * Start encoding a octet string, All fields between
 * qcrypto_der_encode_octet_str_begin and qcrypto_der_encode_octet_str_end
 * are encoded as an octet string. This is useful when we need to encode a
 * encoded SEQUENCE as OCTET STRING.
 */
void qcrypto_der_encode_octet_str_begin(QCryptoEncodeContext *ctx);

/**
 * qcrypto_der_encode_octet_str_end:
 * @ctx: the encode context.
 *
 * Finish encoding a octet string, All fields between
 * qcrypto_der_encode_octet_str_begin and qcrypto_der_encode_octet_str_end
 * are encoded as an octet string. This is useful when we need to encode a
 * encoded SEQUENCE as OCTET STRING.
 */
void qcrypto_der_encode_octet_str_end(QCryptoEncodeContext *ctx);

/**
 * qcrypto_der_encode_ctx_buffer_len:
 * @ctx: the encode context.
 *
 * Compute the expected buffer size to save all encoded things.
 */
size_t qcrypto_der_encode_ctx_buffer_len(QCryptoEncodeContext *ctx);

/**
 * qcrypto_der_encode_ctx_flush_and_free:
 * @ctx: the encode context.
 * @dst: the destination to save the encoded data, the length of dst should
 * not less than qcrypto_der_encode_cxt_buffer_len
 *
 * Flush all encoded data into dst, then free ctx.
 */
void qcrypto_der_encode_ctx_flush_and_free(QCryptoEncodeContext *ctx,
                                           uint8_t *dst);

#endif  /* QCRYPTO_ASN1_DECODER_H */
