/*
 *  Castagnoli CRC32C Checksum Algorithm
 *
 *  Polynomial: 0x11EDC6F41
 *
 *  Castagnoli93: Guy Castagnoli and Stefan Braeuer and Martin Herrman
 *               "Optimization of Cyclic Redundancy-Check Codes with 24
 *                 and 32 Parity Bits",IEEE Transactions on Communication,
 *                Volume 41, Number 6, June 1993
 *
 *  Copyright (c) 2013 Red Hat, Inc.,
 *
 *  Authors:
 *   Jeff Cody <jcody@redhat.com>
 *
 *  Based on the Linux kernel cryptographic crc32c module,
 *
 *  Copyright (c) 2004 Cisco Systems, Inc.
 *  Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef QEMU_CRC32C_H
#define QEMU_CRC32C_H


uint32_t crc32c(uint32_t crc, const uint8_t *data, unsigned int length);
uint32_t iov_crc32c(uint32_t crc, const struct iovec *iov, size_t iov_cnt);

#endif
