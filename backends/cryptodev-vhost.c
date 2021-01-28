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
#include "hw/virtio/virtio-bus.h"
#include "sysemu/cryptodev-vhost.h"

#ifdef CONFIG_VHOST_CRYPTO
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-crypto.h"
#include "sysemu/cryptodev-vhost-user.h"

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

static int
cryptodev_vhost_start_one(CryptoDevBackendVhost *crypto,
                                  VirtIODevice *dev)
{
    int r;

    crypto->dev.nvqs = 1;
    crypto->dev.vqs = crypto->vqs;

    r = vhost_dev_enable_notifiers(&crypto->dev, dev);
    if (r < 0) {
        goto fail_notifiers;
    }

    r = vhost_dev_start(&crypto->dev, dev);
    if (r < 0) {
        goto fail_start;
    }

    return 0;

fail_start:
    vhost_dev_disable_notifiers(&crypto->dev, dev);
fail_notifiers:
    return r;
}

static void
cryptodev_vhost_stop_one(CryptoDevBackendVhost *crypto,
                                 VirtIODevice *dev)
{
    vhost_dev_stop(&crypto->dev, dev);
    vhost_dev_disable_notifiers(&crypto->dev, dev);
}

CryptoDevBackendVhost *
cryptodev_get_vhost(CryptoDevBackendClient *cc,
                            CryptoDevBackend *b,
                            uint16_t queue)
{
    CryptoDevBackendVhost *vhost_crypto = NULL;

    if (!cc) {
        return NULL;
    }

    switch (cc->type) {
#if defined(CONFIG_VHOST_USER) && defined(CONFIG_LINUX)
    case CRYPTODEV_BACKEND_TYPE_VHOST_USER:
        vhost_crypto = cryptodev_vhost_user_get_vhost(cc, b, queue);
        break;
#endif
    default:
        break;
    }

    return vhost_crypto;
}

static void
cryptodev_vhost_set_vq_index(CryptoDevBackendVhost *crypto,
                                     int vq_index)
{
    crypto->dev.vq_index = vq_index;
}

static int
vhost_set_vring_enable(CryptoDevBackendClient *cc,
                            CryptoDevBackend *b,
                            uint16_t queue, int enable)
{
    CryptoDevBackendVhost *crypto =
                       cryptodev_get_vhost(cc, b, queue);
    const VhostOps *vhost_ops;

    cc->vring_enable = enable;

    if (!crypto) {
        return 0;
    }

    vhost_ops = crypto->dev.vhost_ops;
    if (vhost_ops->vhost_set_vring_enable) {
        return vhost_ops->vhost_set_vring_enable(&crypto->dev, enable);
    }

    return 0;
}

int cryptodev_vhost_start(VirtIODevice *dev, int total_queues)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    int r, e;
    int i;
    CryptoDevBackend *b = vcrypto->cryptodev;
    CryptoDevBackendVhost *vhost_crypto;
    CryptoDevBackendClient *cc;

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    for (i = 0; i < total_queues; i++) {
        cc = b->conf.peers.ccs[i];

        vhost_crypto = cryptodev_get_vhost(cc, b, i);
        cryptodev_vhost_set_vq_index(vhost_crypto, i);

        /* Suppress the masking guest notifiers on vhost user
         * because vhost user doesn't interrupt masking/unmasking
         * properly.
         */
        if (cc->type == CRYPTODEV_BACKEND_TYPE_VHOST_USER) {
            dev->use_guest_notifier_mask = false;
        }
     }

    r = k->set_guest_notifiers(qbus->parent, total_queues, true);
    if (r < 0) {
        error_report("error binding guest notifier: %d", -r);
        goto err;
    }

    for (i = 0; i < total_queues; i++) {
        cc = b->conf.peers.ccs[i];

        vhost_crypto = cryptodev_get_vhost(cc, b, i);
        r = cryptodev_vhost_start_one(vhost_crypto, dev);

        if (r < 0) {
            goto err_start;
        }

        if (cc->vring_enable) {
            /* restore vring enable state */
            r = vhost_set_vring_enable(cc, b, i, cc->vring_enable);

            if (r < 0) {
                goto err_start;
            }
        }
    }

    return 0;

err_start:
    while (--i >= 0) {
        cc = b->conf.peers.ccs[i];
        vhost_crypto = cryptodev_get_vhost(cc, b, i);
        cryptodev_vhost_stop_one(vhost_crypto, dev);
    }
    e = k->set_guest_notifiers(qbus->parent, total_queues, false);
    if (e < 0) {
        error_report("vhost guest notifier cleanup failed: %d", e);
    }
err:
    return r;
}

void cryptodev_vhost_stop(VirtIODevice *dev, int total_queues)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    CryptoDevBackend *b = vcrypto->cryptodev;
    CryptoDevBackendVhost *vhost_crypto;
    CryptoDevBackendClient *cc;
    size_t i;
    int r;

    for (i = 0; i < total_queues; i++) {
        cc = b->conf.peers.ccs[i];

        vhost_crypto = cryptodev_get_vhost(cc, b, i);
        cryptodev_vhost_stop_one(vhost_crypto, dev);
    }

    r = k->set_guest_notifiers(qbus->parent, total_queues, false);
    if (r < 0) {
        error_report("vhost guest notifier cleanup failed: %d", r);
    }
    assert(r >= 0);
}

void cryptodev_vhost_virtqueue_mask(VirtIODevice *dev,
                                           int queue,
                                           int idx, bool mask)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    CryptoDevBackend *b = vcrypto->cryptodev;
    CryptoDevBackendVhost *vhost_crypto;
    CryptoDevBackendClient *cc;

    assert(queue < MAX_CRYPTO_QUEUE_NUM);

    cc = b->conf.peers.ccs[queue];
    vhost_crypto = cryptodev_get_vhost(cc, b, queue);

    vhost_virtqueue_mask(&vhost_crypto->dev, dev, idx, mask);
}

bool cryptodev_vhost_virtqueue_pending(VirtIODevice *dev,
                                              int queue, int idx)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    CryptoDevBackend *b = vcrypto->cryptodev;
    CryptoDevBackendVhost *vhost_crypto;
    CryptoDevBackendClient *cc;

    assert(queue < MAX_CRYPTO_QUEUE_NUM);

    cc = b->conf.peers.ccs[queue];
    vhost_crypto = cryptodev_get_vhost(cc, b, queue);

    return vhost_virtqueue_pending(&vhost_crypto->dev, idx);
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

CryptoDevBackendVhost *
cryptodev_get_vhost(CryptoDevBackendClient *cc,
                    CryptoDevBackend *b,
                    uint16_t queue)
{
    return NULL;
}

int cryptodev_vhost_start(VirtIODevice *dev, int total_queues)
{
    return -1;
}

void cryptodev_vhost_stop(VirtIODevice *dev, int total_queues)
{
}

void cryptodev_vhost_virtqueue_mask(VirtIODevice *dev,
                                    int queue,
                                    int idx, bool mask)
{
}

bool cryptodev_vhost_virtqueue_pending(VirtIODevice *dev,
                                       int queue, int idx)
{
    return false;
}
#endif
