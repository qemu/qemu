/*
 * QEMU Crypto random number provider
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

#ifndef QCRYPTO_RANDOM_H
#define QCRYPTO_RANDOM_H


/**
 * qcrypto_random_bytes:
 * @buf: the buffer to fill
 * @buflen: length of @buf in bytes
 * @errp: pointer to a NULL-initialized error object
 *
 * Fill @buf with @buflen bytes of cryptographically strong
 * random data
 *
 * Returns 0 on success, -1 on error
 */
int qcrypto_random_bytes(void *buf,
                         size_t buflen,
                         Error **errp);

/**
 * qcrypto_random_init:
 * @errp: pointer to a NULL-initialized error object
 *
 * Initializes the handles used by qcrypto_random_bytes
 *
 * Returns 0 on success, -1 on error
 */
int qcrypto_random_init(Error **errp);

#endif /* QCRYPTO_RANDOM_H */
