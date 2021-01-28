/*
 * QEMU Crypto Device Common Vhost User Implement
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
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

#ifndef CRYPTODEV_VHOST_USER_H
#define CRYPTODEV_VHOST_USER_H

#include "sysemu/cryptodev-vhost.h"

#define VHOST_USER_MAX_AUTH_KEY_LEN    512
#define VHOST_USER_MAX_CIPHER_KEY_LEN  64


/**
 * cryptodev_vhost_user_get_vhost:
 * @cc: the client object for each queue
 * @b: the cryptodev backend common vhost object
 * @queue: the queue index
 *
 * Gets a new cryptodev backend common vhost object based on
 * @b and @queue
 *
 * Returns: the cryptodev backend common vhost object
 */
CryptoDevBackendVhost *
cryptodev_vhost_user_get_vhost(
                         CryptoDevBackendClient *cc,
                         CryptoDevBackend *b,
                         uint16_t queue);

#endif /* CRYPTODEV_VHOST_USER_H */
