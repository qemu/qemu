/*
 * QEMU Crypto Device Implementation
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
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
#ifndef CRYPTODEV_H
#define CRYPTODEV_H

#include "qom/object.h"
#include "qemu-common.h"

/**
 * CryptoDevBackend:
 *
 * The CryptoDevBackend object is an interface
 * for different cryptodev backends, which provides crypto
 * operation wrapper.
 *
 */

#define TYPE_CRYPTODEV_BACKEND "cryptodev-backend"

#define CRYPTODEV_BACKEND(obj) \
    OBJECT_CHECK(CryptoDevBackend, \
                 (obj), TYPE_CRYPTODEV_BACKEND)
#define CRYPTODEV_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(CryptoDevBackendClass, \
                 (obj), TYPE_CRYPTODEV_BACKEND)
#define CRYPTODEV_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(CryptoDevBackendClass, \
                (klass), TYPE_CRYPTODEV_BACKEND)


#define MAX_CRYPTO_QUEUE_NUM  64

typedef struct CryptoDevBackendConf CryptoDevBackendConf;
typedef struct CryptoDevBackendPeers CryptoDevBackendPeers;
typedef struct CryptoDevBackendClient
                     CryptoDevBackendClient;
typedef struct CryptoDevBackend CryptoDevBackend;


typedef struct CryptoDevBackendClass {
    ObjectClass parent_class;

    void (*init)(CryptoDevBackend *backend, Error **errp);
    void (*cleanup)(CryptoDevBackend *backend, Error **errp);
} CryptoDevBackendClass;


struct CryptoDevBackendClient {
    char *model;
    char *name;
    char *info_str;
    unsigned int queue_index;
    QTAILQ_ENTRY(CryptoDevBackendClient) next;
};

struct CryptoDevBackendPeers {
    CryptoDevBackendClient *ccs[MAX_CRYPTO_QUEUE_NUM];
    uint32_t queues;
};

struct CryptoDevBackendConf {
    CryptoDevBackendPeers peers;

    /* Supported service mask */
    uint32_t crypto_services;

    /* Detailed algorithms mask */
    uint32_t cipher_algo_l;
    uint32_t cipher_algo_h;
    uint32_t hash_algo;
    uint32_t mac_algo_l;
    uint32_t mac_algo_h;
    uint32_t aead_algo;
    /* Maximum length of cipher key */
    uint32_t max_cipher_key_len;
    /* Maximum length of authenticated key */
    uint32_t max_auth_key_len;
    /* Maximum size of each crypto request's content */
    uint64_t max_size;
};

struct CryptoDevBackend {
    Object parent_obj;

    bool ready;
    CryptoDevBackendConf conf;
};

/**
 * cryptodev_backend_new_client:
 * @model: the cryptodev backend model
 * @name: the cryptodev backend name, can be NULL
 *
 * Creates a new cryptodev backend client object
 * with the @name in the model @model.
 *
 * The returned object must be released with
 * cryptodev_backend_free_client() when no
 * longer required
 *
 * Returns: a new cryptodev backend client object
 */
CryptoDevBackendClient *
cryptodev_backend_new_client(const char *model,
                                    const char *name);
/**
 * cryptodev_backend_free_client:
 * @cc: the cryptodev backend client object
 *
 * Release the memory associated with @cc that
 * was previously allocated by cryptodev_backend_new_client()
 */
void cryptodev_backend_free_client(
                  CryptoDevBackendClient *cc);

/**
 * cryptodev_backend_cleanup:
 * @backend: the cryptodev backend object
 * @errp: pointer to a NULL-initialized error object
 *
 * Clean the resouce associated with @backend that realizaed
 * by the specific backend's init() callback
 */
void cryptodev_backend_cleanup(
           CryptoDevBackend *backend,
           Error **errp);

#endif /* CRYPTODEV_H */
