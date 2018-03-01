/*
 * QEMU Cryptodev backend for QEMU cipher APIs
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *    Jay Zhou <jianjay.zhou@huawei.com>
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

#include "qemu/osdep.h"
#include "sysemu/cryptodev-vhost.h"

#ifdef CONFIG_VHOST_CRYPTO
uint64_t
cryptodev_vhost_get_max_queues(
                        CryptoDevBackendVhost *crypto)
{
    return crypto->dev.max_queues;
}

void cryptodev_vhost_cleanup(CryptoDevBackendVhost *crypto)
{
    vhost_dev_cleanup(&crypto->dev);
    g_free(crypto);
}

struct CryptoDevBackendVhost *
cryptodev_vhost_init(
             CryptoDevBackendVhostOptions *options)
{
    int r;
    CryptoDevBackendVhost *crypto;

    crypto = g_new(CryptoDevBackendVhost, 1);
    crypto->dev.max_queues = 1;
    crypto->dev.nvqs = 1;
    crypto->dev.vqs = crypto->vqs;

    crypto->cc = options->cc;

    crypto->dev.protocol_features = 0;
    crypto->backend = -1;

    /* vhost-user needs vq_index to initiate a specific queue pair */
    crypto->dev.vq_index = crypto->cc->queue_index * crypto->dev.nvqs;

    r = vhost_dev_init(&crypto->dev, options->opaque, options->backend_type, 0);
    if (r < 0) {
        goto fail;
    }

    return crypto;
fail:
    g_free(crypto);
    return NULL;
}

#else
uint64_t
cryptodev_vhost_get_max_queues(CryptoDevBackendVhost *crypto)
{
    return 0;
}

void cryptodev_vhost_cleanup(CryptoDevBackendVhost *crypto)
{
}

struct CryptoDevBackendVhost *
cryptodev_vhost_init(CryptoDevBackendVhostOptions *options)
{
    return NULL;
}
#endif
