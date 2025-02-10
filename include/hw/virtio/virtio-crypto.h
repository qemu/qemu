/*
 * Virtio crypto Support
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VIRTIO_CRYPTO_H
#define QEMU_VIRTIO_CRYPTO_H

#include "standard-headers/linux/virtio_crypto.h"
#include "hw/virtio/virtio.h"
#include "system/iothread.h"
#include "system/cryptodev.h"
#include "qom/object.h"


#define DEBUG_VIRTIO_CRYPTO 0

#define DPRINTF(fmt, ...) \
do { \
    if (DEBUG_VIRTIO_CRYPTO) { \
        fprintf(stderr, "virtio_crypto: " fmt, ##__VA_ARGS__); \
    } \
} while (0)


#define TYPE_VIRTIO_CRYPTO "virtio-crypto-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOCrypto, VIRTIO_CRYPTO)
#define VIRTIO_CRYPTO_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_CRYPTO)


typedef struct VirtIOCryptoConf {
    CryptoDevBackend *cryptodev;

    /* Supported service mask */
    uint32_t crypto_services;

    /* Detailed algorithms mask */
    uint32_t cipher_algo_l;
    uint32_t cipher_algo_h;
    uint32_t hash_algo;
    uint32_t mac_algo_l;
    uint32_t mac_algo_h;
    uint32_t aead_algo;
    uint32_t akcipher_algo;

    /* Maximum length of cipher key */
    uint32_t max_cipher_key_len;
    /* Maximum length of authenticated key */
    uint32_t max_auth_key_len;
    /* Maximum size of each crypto request's content */
    uint64_t max_size;
} VirtIOCryptoConf;

struct VirtIOCrypto;

typedef struct VirtIOCryptoReq {
    VirtQueueElement elem;
    /* flags of operation, such as type of algorithm */
    uint32_t flags;
    struct virtio_crypto_inhdr *in;
    struct iovec *in_iov; /* Head address of dest iovec */
    unsigned int in_num; /* Number of dest iovec */
    size_t in_len;
    VirtQueue *vq;
    struct VirtIOCrypto *vcrypto;
    CryptoDevBackendOpInfo op_info;
} VirtIOCryptoReq;

typedef struct VirtIOCryptoQueue {
    VirtQueue *dataq;
    QEMUBH *dataq_bh;
    struct VirtIOCrypto *vcrypto;
} VirtIOCryptoQueue;

struct VirtIOCrypto {
    VirtIODevice parent_obj;

    VirtQueue *ctrl_vq;
    VirtIOCryptoQueue *vqs;
    VirtIOCryptoConf conf;
    CryptoDevBackend *cryptodev;

    uint32_t max_queues;
    uint32_t status;

    int multiqueue;
    uint32_t curr_queues;
    size_t config_size;
    uint8_t vhost_started;
};

#endif /* QEMU_VIRTIO_CRYPTO_H */
