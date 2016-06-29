/*
 * QEMU Crypto block device encryption QCow/QCow2 AES-CBC format
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

#ifndef QCRYPTO_BLOCK_QCOW_H
#define QCRYPTO_BLOCK_QCOW_H

#include "crypto/blockpriv.h"

extern const QCryptoBlockDriver qcrypto_block_driver_qcow;

#endif /* QCRYPTO_BLOCK_QCOW_H */
