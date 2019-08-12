/*
 * QEMU Cryptodev backend for QEMU cipher APIs
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

#include "qemu/osdep.h"
#include "sysemu/cryptodev.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_crypto.h"
#include "crypto/cipher.h"


/**
 * @TYPE_CRYPTODEV_BACKEND_BUILTIN:
 * name of backend that uses QEMU cipher API
 */
#define TYPE_CRYPTODEV_BACKEND_BUILTIN "cryptodev-backend-builtin"

#define CRYPTODEV_BACKEND_BUILTIN(obj) \
    OBJECT_CHECK(CryptoDevBackendBuiltin, \
                 (obj), TYPE_CRYPTODEV_BACKEND_BUILTIN)

typedef struct CryptoDevBackendBuiltin
                         CryptoDevBackendBuiltin;

typedef struct CryptoDevBackendBuiltinSession {
    QCryptoCipher *cipher;
    uint8_t direction; /* encryption or decryption */
    uint8_t type; /* cipher? hash? aead? */
    QTAILQ_ENTRY(CryptoDevBackendBuiltinSession) next;
} CryptoDevBackendBuiltinSession;

/* Max number of symmetric sessions */
#define MAX_NUM_SESSIONS 256

#define CRYPTODEV_BUITLIN_MAX_AUTH_KEY_LEN    512
#define CRYPTODEV_BUITLIN_MAX_CIPHER_KEY_LEN  64

struct CryptoDevBackendBuiltin {
    CryptoDevBackend parent_obj;

    CryptoDevBackendBuiltinSession *sessions[MAX_NUM_SESSIONS];
};

static void cryptodev_builtin_init(
             CryptoDevBackend *backend, Error **errp)
{
    /* Only support one queue */
    int queues = backend->conf.peers.queues;
    CryptoDevBackendClient *cc;

    if (queues != 1) {
        error_setg(errp,
                  "Only support one queue in cryptdov-builtin backend");
        return;
    }

    cc = cryptodev_backend_new_client(
              "cryptodev-builtin", NULL);
    cc->info_str = g_strdup_printf("cryptodev-builtin0");
    cc->queue_index = 0;
    cc->type = CRYPTODEV_BACKEND_TYPE_BUILTIN;
    backend->conf.peers.ccs[0] = cc;

    backend->conf.crypto_services =
                         1u << VIRTIO_CRYPTO_SERVICE_CIPHER |
                         1u << VIRTIO_CRYPTO_SERVICE_HASH |
                         1u << VIRTIO_CRYPTO_SERVICE_MAC;
    backend->conf.cipher_algo_l = 1u << VIRTIO_CRYPTO_CIPHER_AES_CBC;
    backend->conf.hash_algo = 1u << VIRTIO_CRYPTO_HASH_SHA1;
    /*
     * Set the Maximum length of crypto request.
     * Why this value? Just avoid to overflow when
     * memory allocation for each crypto request.
     */
    backend->conf.max_size = LONG_MAX - sizeof(CryptoDevBackendSymOpInfo);
    backend->conf.max_cipher_key_len = CRYPTODEV_BUITLIN_MAX_CIPHER_KEY_LEN;
    backend->conf.max_auth_key_len = CRYPTODEV_BUITLIN_MAX_AUTH_KEY_LEN;

    cryptodev_backend_set_ready(backend, true);
}

static int
cryptodev_builtin_get_unused_session_index(
                 CryptoDevBackendBuiltin *builtin)
{
    size_t i;

    for (i = 0; i < MAX_NUM_SESSIONS; i++) {
        if (builtin->sessions[i] == NULL) {
            return i;
        }
    }

    return -1;
}

#define AES_KEYSIZE_128 16
#define AES_KEYSIZE_192 24
#define AES_KEYSIZE_256 32
#define AES_KEYSIZE_128_XTS AES_KEYSIZE_256
#define AES_KEYSIZE_256_XTS 64

static int
cryptodev_builtin_get_aes_algo(uint32_t key_len, int mode, Error **errp)
{
    int algo;

    if (key_len == AES_KEYSIZE_128) {
        algo = QCRYPTO_CIPHER_ALG_AES_128;
    } else if (key_len == AES_KEYSIZE_192) {
        algo = QCRYPTO_CIPHER_ALG_AES_192;
    } else if (key_len == AES_KEYSIZE_256) { /* equals AES_KEYSIZE_128_XTS */
        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            algo = QCRYPTO_CIPHER_ALG_AES_128;
        } else {
            algo = QCRYPTO_CIPHER_ALG_AES_256;
        }
    } else if (key_len == AES_KEYSIZE_256_XTS) {
        if (mode == QCRYPTO_CIPHER_MODE_XTS) {
            algo = QCRYPTO_CIPHER_ALG_AES_256;
        } else {
            goto err;
        }
    } else {
        goto err;
    }

    return algo;

err:
   error_setg(errp, "Unsupported key length :%u", key_len);
   return -1;
}

static int cryptodev_builtin_create_cipher_session(
                    CryptoDevBackendBuiltin *builtin,
                    CryptoDevBackendSymSessionInfo *sess_info,
                    Error **errp)
{
    int algo;
    int mode;
    QCryptoCipher *cipher;
    int index;
    CryptoDevBackendBuiltinSession *sess;

    if (sess_info->op_type != VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        error_setg(errp, "Unsupported optype :%u", sess_info->op_type);
        return -1;
    }

    index = cryptodev_builtin_get_unused_session_index(builtin);
    if (index < 0) {
        error_setg(errp, "Total number of sessions created exceeds %u",
                  MAX_NUM_SESSIONS);
        return -1;
    }

    switch (sess_info->cipher_alg) {
    case VIRTIO_CRYPTO_CIPHER_AES_ECB:
        mode = QCRYPTO_CIPHER_MODE_ECB;
        algo = cryptodev_builtin_get_aes_algo(sess_info->key_len,
                                                    mode, errp);
        if (algo < 0)  {
            return -1;
        }
        break;
    case VIRTIO_CRYPTO_CIPHER_AES_CBC:
        mode = QCRYPTO_CIPHER_MODE_CBC;
        algo = cryptodev_builtin_get_aes_algo(sess_info->key_len,
                                                    mode, errp);
        if (algo < 0)  {
            return -1;
        }
        break;
    case VIRTIO_CRYPTO_CIPHER_AES_CTR:
        mode = QCRYPTO_CIPHER_MODE_CTR;
        algo = cryptodev_builtin_get_aes_algo(sess_info->key_len,
                                                    mode, errp);
        if (algo < 0)  {
            return -1;
        }
        break;
    case VIRTIO_CRYPTO_CIPHER_AES_XTS:
        mode = QCRYPTO_CIPHER_MODE_XTS;
        algo = cryptodev_builtin_get_aes_algo(sess_info->key_len,
                                                    mode, errp);
        if (algo < 0)  {
            return -1;
        }
        break;
    case VIRTIO_CRYPTO_CIPHER_3DES_ECB:
        mode = QCRYPTO_CIPHER_MODE_ECB;
        algo = QCRYPTO_CIPHER_ALG_3DES;
        break;
    case VIRTIO_CRYPTO_CIPHER_3DES_CBC:
        mode = QCRYPTO_CIPHER_MODE_CBC;
        algo = QCRYPTO_CIPHER_ALG_3DES;
        break;
    case VIRTIO_CRYPTO_CIPHER_3DES_CTR:
        mode = QCRYPTO_CIPHER_MODE_CTR;
        algo = QCRYPTO_CIPHER_ALG_3DES;
        break;
    default:
        error_setg(errp, "Unsupported cipher alg :%u",
                   sess_info->cipher_alg);
        return -1;
    }

    cipher = qcrypto_cipher_new(algo, mode,
                               sess_info->cipher_key,
                               sess_info->key_len,
                               errp);
    if (!cipher) {
        return -1;
    }

    sess = g_new0(CryptoDevBackendBuiltinSession, 1);
    sess->cipher = cipher;
    sess->direction = sess_info->direction;
    sess->type = sess_info->op_type;

    builtin->sessions[index] = sess;

    return index;
}

static int64_t cryptodev_builtin_sym_create_session(
           CryptoDevBackend *backend,
           CryptoDevBackendSymSessionInfo *sess_info,
           uint32_t queue_index, Error **errp)
{
    CryptoDevBackendBuiltin *builtin =
                      CRYPTODEV_BACKEND_BUILTIN(backend);
    int64_t session_id = -1;
    int ret;

    switch (sess_info->op_code) {
    case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
        ret = cryptodev_builtin_create_cipher_session(
                           builtin, sess_info, errp);
        if (ret < 0) {
            return ret;
        } else {
            session_id = ret;
        }
        break;
    case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
    case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
    default:
        error_setg(errp, "Unsupported opcode :%" PRIu32 "",
                   sess_info->op_code);
        return -1;
    }

    return session_id;
}

static int cryptodev_builtin_sym_close_session(
           CryptoDevBackend *backend,
           uint64_t session_id,
           uint32_t queue_index, Error **errp)
{
    CryptoDevBackendBuiltin *builtin =
                      CRYPTODEV_BACKEND_BUILTIN(backend);

    if (session_id >= MAX_NUM_SESSIONS ||
              builtin->sessions[session_id] == NULL) {
        error_setg(errp, "Cannot find a valid session id: %" PRIu64 "",
                      session_id);
        return -1;
    }

    qcrypto_cipher_free(builtin->sessions[session_id]->cipher);
    g_free(builtin->sessions[session_id]);
    builtin->sessions[session_id] = NULL;
    return 0;
}

static int cryptodev_builtin_sym_operation(
                 CryptoDevBackend *backend,
                 CryptoDevBackendSymOpInfo *op_info,
                 uint32_t queue_index, Error **errp)
{
    CryptoDevBackendBuiltin *builtin =
                      CRYPTODEV_BACKEND_BUILTIN(backend);
    CryptoDevBackendBuiltinSession *sess;
    int ret;

    if (op_info->session_id >= MAX_NUM_SESSIONS ||
              builtin->sessions[op_info->session_id] == NULL) {
        error_setg(errp, "Cannot find a valid session id: %" PRIu64 "",
                   op_info->session_id);
        return -VIRTIO_CRYPTO_INVSESS;
    }

    if (op_info->op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        error_setg(errp,
               "Algorithm chain is unsupported for cryptdoev-builtin");
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    sess = builtin->sessions[op_info->session_id];

    if (op_info->iv_len > 0) {
        ret = qcrypto_cipher_setiv(sess->cipher, op_info->iv,
                                   op_info->iv_len, errp);
        if (ret < 0) {
            return -VIRTIO_CRYPTO_ERR;
        }
    }

    if (sess->direction == VIRTIO_CRYPTO_OP_ENCRYPT) {
        ret = qcrypto_cipher_encrypt(sess->cipher, op_info->src,
                                     op_info->dst, op_info->src_len, errp);
        if (ret < 0) {
            return -VIRTIO_CRYPTO_ERR;
        }
    } else {
        ret = qcrypto_cipher_decrypt(sess->cipher, op_info->src,
                                     op_info->dst, op_info->src_len, errp);
        if (ret < 0) {
            return -VIRTIO_CRYPTO_ERR;
        }
    }
    return VIRTIO_CRYPTO_OK;
}

static void cryptodev_builtin_cleanup(
             CryptoDevBackend *backend,
             Error **errp)
{
    CryptoDevBackendBuiltin *builtin =
                      CRYPTODEV_BACKEND_BUILTIN(backend);
    size_t i;
    int queues = backend->conf.peers.queues;
    CryptoDevBackendClient *cc;

    for (i = 0; i < MAX_NUM_SESSIONS; i++) {
        if (builtin->sessions[i] != NULL) {
            cryptodev_builtin_sym_close_session(
                    backend, i, 0, errp);
        }
    }

    for (i = 0; i < queues; i++) {
        cc = backend->conf.peers.ccs[i];
        if (cc) {
            cryptodev_backend_free_client(cc);
            backend->conf.peers.ccs[i] = NULL;
        }
    }

    cryptodev_backend_set_ready(backend, false);
}

static void
cryptodev_builtin_class_init(ObjectClass *oc, void *data)
{
    CryptoDevBackendClass *bc = CRYPTODEV_BACKEND_CLASS(oc);

    bc->init = cryptodev_builtin_init;
    bc->cleanup = cryptodev_builtin_cleanup;
    bc->create_session = cryptodev_builtin_sym_create_session;
    bc->close_session = cryptodev_builtin_sym_close_session;
    bc->do_sym_op = cryptodev_builtin_sym_operation;
}

static const TypeInfo cryptodev_builtin_info = {
    .name = TYPE_CRYPTODEV_BACKEND_BUILTIN,
    .parent = TYPE_CRYPTODEV_BACKEND,
    .class_init = cryptodev_builtin_class_init,
    .instance_size = sizeof(CryptoDevBackendBuiltin),
};

static void
cryptodev_builtin_register_types(void)
{
    type_register_static(&cryptodev_builtin_info);
}

type_init(cryptodev_builtin_register_types);
