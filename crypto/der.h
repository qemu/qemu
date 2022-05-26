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
 * returned.
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
 * returned.
 */
int qcrypto_der_decode_seq(const uint8_t **data,
                           size_t *dlen,
                           QCryptoDERDecodeCb cb,
                           void *opaque,
                           Error **errp);

#endif  /* QCRYPTO_ASN1_DECODER_H */
