/*
 * QEMU base64 helpers
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#ifndef QEMU_BASE64_H
#define QEMU_BASE64_H

#include "qemu-common.h"


/**
 * qbase64_decode:
 * @input: the (possibly) base64 encoded text
 * @in_len: length of @input or -1 if NUL terminated
 * @out_len: filled with length of decoded data
 * @errp: pointer to a NULL-initialized error object
 *
 * Attempt to decode the (possibly) base64 encoded
 * text provided in @input. If the @input text may
 * contain embedded NUL characters, or may not be
 * NUL terminated, then @in_len must be set to the
 * known size of the @input buffer.
 *
 * Note that embedded NULs, or lack of a NUL terminator
 * are considered invalid base64 data and errors
 * will be reported to this effect.
 *
 * If decoding is successful, the decoded data will
 * be returned and @out_len set to indicate the
 * number of bytes in the decoded data. The caller
 * must use g_free() to free the returned data when
 * it is no longer required.
 *
 * Returns: the decoded data or NULL
 */
uint8_t *qbase64_decode(const char *input,
                        size_t in_len,
                        size_t *out_len,
                        Error **errp);


#endif /* QEMU_BASE64_H */
