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
#include "sysemu/cryptodev.h"
#include "qapi/error.h"
#include "standard-headers/linux/virtio_crypto.h"
#include "crypto/cipher.h"
#include "crypto/akcipher.h"
#include "qom/object.h"


/**
 * @TYPE_CRYPTODEV_BACKEND_BUILTIN:
 * name of backend that uses QEMU cipher API
 */
#define TYPE_CRYPTODEV_BACKEND_BUILTIN "cryptodev-backend-builtin"

OBJECT_DECLARE_SIMPLE_TYPE(CryptoDevBackendBuiltin, CRYPTODEV_BACKEND_BUILTIN)


typedef struct CryptoDevBackendBuiltinSession {
    QCryptoCipher *cipher;
    uint8_t direction; /* encryption or decryption */
    uint8_t type; /* cipher? hash? aead? */
    QCryptoAkCipher *akcipher;
    QTAILQ_ENTRY(CryptoDevBackendBuiltinSession) next;
} CryptoDevBackendBuiltinSession;

/* Max number of symmetric/asymmetric sessions */
#define MAX_NUM_SESSIONS 256

#define CRYPTODEV_BUITLIN_MAX_AUTH_KEY_LEN    512
#define CRYPTODEV_BUITLIN_MAX_CIPHER_KEY_LEN  64

struct CryptoDevBackendBuiltin {
    CryptoDevBackend parent_obj;

    CryptoDevBackendBuiltinSession *sessions[MAX_NUM_SESSIONS];
};

static void cryptodev_builtin_init_akcipher(CryptoDevBackend *backend)
{
    QCryptoAkCipherOptions opts;

    opts.alg = QCRYPTO_AKCIPHER_ALG_RSA;
    opts.u.rsa.padding_alg = QCRYPTO_RSA_PADDING_ALG_RAW;
    if (qcrypto_akcipher_supports(&opts)) {
        backend->conf.crypto_services |=
                     (1u << QCRYPTODEV_BACKEND_SERVICE_AKCIPHER);
        backend->conf.akcipher_algo = 1u << VIRTIO_CRYPTO_AKCIPHER_RSA;
    }
}

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

    cc = cryptodev_backend_new_client();
    cc->info_str = g_strdup_printf("cryptodev-builtin0");
    cc->queue_index = 0;
    cc->type = QCRYPTODEV_BACKEND_TYPE_BUILTIN;
    backend->conf.peers.ccs[0] = cc;

    backend->conf.crypto_services =
                         1u << QCRYPTODEV_BACKEND_SERVICE_CIPHER |
                         1u << QCRYPTODEV_BACKEND_SERVICE_HASH |
                         1u << QCRYPTODEV_BACKEND_SERVICE_MAC;
    backend->conf.cipher_algo_l = 1u << VIRTIO_CRYPTO_CIPHER_AES_CBC;
    backend->conf.hash_algo = 1u << VIRTIO_CRYPTO_HASH_SHA1;
    /*
     * Set the Maximum length of crypto request.
     * Why this value? Just avoid to overflow when
     * memory allocation for each crypto request.
     */
    backend->conf.max_size = LONG_MAX - sizeof(CryptoDevBackendOpInfo);
    backend->conf.max_cipher_key_len = CRYPTODEV_BUITLIN_MAX_CIPHER_KEY_LEN;
    backend->conf.max_auth_key_len = CRYPTODEV_BUITLIN_MAX_AUTH_KEY_LEN;
    cryptodev_builtin_init_akcipher(backend);

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

static int cryptodev_builtin_get_rsa_hash_algo(
    int virtio_rsa_hash, Error **errp)
{
    switch (virtio_rsa_hash) {
    case VIRTIO_CRYPTO_RSA_MD5:
        return QCRYPTO_HASH_ALG_MD5;

    case VIRTIO_CRYPTO_RSA_SHA1:
        return QCRYPTO_HASH_ALG_SHA1;

    case VIRTIO_CRYPTO_RSA_SHA256:
        return QCRYPTO_HASH_ALG_SHA256;

    case VIRTIO_CRYPTO_RSA_SHA512:
        return QCRYPTO_HASH_ALG_SHA512;

    default:
        error_setg(errp, "Unsupported rsa hash algo: %d", virtio_rsa_hash);
        return -1;
    }
}

static int cryptodev_builtin_set_rsa_options(
                    int virtio_padding_algo,
                    int virtio_hash_algo,
                    QCryptoAkCipherOptionsRSA *opt,
                    Error **errp)
{
    if (virtio_padding_algo == VIRTIO_CRYPTO_RSA_PKCS1_PADDING) {
        int hash_alg;

        hash_alg = cryptodev_builtin_get_rsa_hash_algo(virtio_hash_algo, errp);
        if (hash_alg < 0) {
            return -1;
        }
        opt->hash_alg = hash_alg;
        opt->padding_alg = QCRYPTO_RSA_PADDING_ALG_PKCS1;
        return 0;
    }

    if (virtio_padding_algo == VIRTIO_CRYPTO_RSA_RAW_PADDING) {
        opt->padding_alg = QCRYPTO_RSA_PADDING_ALG_RAW;
        return 0;
    }

    error_setg(errp, "Unsupported rsa padding algo: %d", virtio_padding_algo);
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

static int cryptodev_builtin_create_akcipher_session(
                    CryptoDevBackendBuiltin *builtin,
                    CryptoDevBackendAsymSessionInfo *sess_info,
                    Error **errp)
{
    CryptoDevBackendBuiltinSession *sess;
    QCryptoAkCipher *akcipher;
    int index;
    QCryptoAkCipherKeyType type;
    QCryptoAkCipherOptions opts;

    switch (sess_info->algo) {
    case VIRTIO_CRYPTO_AKCIPHER_RSA:
        opts.alg = QCRYPTO_AKCIPHER_ALG_RSA;
        if (cryptodev_builtin_set_rsa_options(sess_info->u.rsa.padding_algo,
            sess_info->u.rsa.hash_algo, &opts.u.rsa, errp) != 0) {
            return -1;
        }
        break;

    /* TODO support DSA&ECDSA until qemu crypto framework support these */

    default:
        error_setg(errp, "Unsupported akcipher alg %u", sess_info->algo);
        return -1;
    }

    switch (sess_info->keytype) {
    case VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PUBLIC:
        type = QCRYPTO_AKCIPHER_KEY_TYPE_PUBLIC;
        break;

    case VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE:
        type = QCRYPTO_AKCIPHER_KEY_TYPE_PRIVATE;
        break;

    default:
        error_setg(errp, "Unsupported akcipher keytype %u", sess_info->keytype);
        return -1;
    }

    index = cryptodev_builtin_get_unused_session_index(builtin);
    if (index < 0) {
        error_setg(errp, "Total number of sessions created exceeds %u",
                   MAX_NUM_SESSIONS);
        return -1;
    }

    akcipher = qcrypto_akcipher_new(&opts, type, sess_info->key,
                                    sess_info->keylen, errp);
    if (!akcipher) {
        return -1;
    }

    sess = g_new0(CryptoDevBackendBuiltinSession, 1);
    sess->akcipher = akcipher;

    builtin->sessions[index] = sess;

    return index;
}

static int cryptodev_builtin_create_session(
           CryptoDevBackend *backend,
           CryptoDevBackendSessionInfo *sess_info,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque)
{
    CryptoDevBackendBuiltin *builtin =
                      CRYPTODEV_BACKEND_BUILTIN(backend);
    CryptoDevBackendSymSessionInfo *sym_sess_info;
    CryptoDevBackendAsymSessionInfo *asym_sess_info;
    int ret, status;
    Error *local_error = NULL;

    switch (sess_info->op_code) {
    case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
        sym_sess_info = &sess_info->u.sym_sess_info;
        ret = cryptodev_builtin_create_cipher_session(
                    builtin, sym_sess_info, &local_error);
        break;

    case VIRTIO_CRYPTO_AKCIPHER_CREATE_SESSION:
        asym_sess_info = &sess_info->u.asym_sess_info;
        ret = cryptodev_builtin_create_akcipher_session(
                           builtin, asym_sess_info, &local_error);
        break;

    case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
    case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
    default:
        error_setg(&local_error, "Unsupported opcode :%" PRIu32 "",
                   sess_info->op_code);
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    if (local_error) {
        error_report_err(local_error);
    }
    if (ret < 0) {
        status = -VIRTIO_CRYPTO_ERR;
    } else {
        sess_info->session_id = ret;
        status = VIRTIO_CRYPTO_OK;
    }
    if (cb) {
        cb(opaque, status);
    }
    return 0;
}

static int cryptodev_builtin_close_session(
           CryptoDevBackend *backend,
           uint64_t session_id,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque)
{
    CryptoDevBackendBuiltin *builtin =
                      CRYPTODEV_BACKEND_BUILTIN(backend);
    CryptoDevBackendBuiltinSession *session;

    assert(session_id < MAX_NUM_SESSIONS && builtin->sessions[session_id]);

    session = builtin->sessions[session_id];
    if (session->cipher) {
        qcrypto_cipher_free(session->cipher);
    } else if (session->akcipher) {
        qcrypto_akcipher_free(session->akcipher);
    }

    g_free(session);
    builtin->sessions[session_id] = NULL;
    if (cb) {
        cb(opaque, VIRTIO_CRYPTO_OK);
    }
    return 0;
}

static int cryptodev_builtin_sym_operation(
                 CryptoDevBackendBuiltinSession *sess,
                 CryptoDevBackendSymOpInfo *op_info, Error **errp)
{
    int ret;

    if (op_info->op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        error_setg(errp,
               "Algorithm chain is unsupported for cryptdoev-builtin");
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

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

static int cryptodev_builtin_asym_operation(
                 CryptoDevBackendBuiltinSession *sess, uint32_t op_code,
                 CryptoDevBackendAsymOpInfo *op_info, Error **errp)
{
    int ret;

    switch (op_code) {
    case VIRTIO_CRYPTO_AKCIPHER_ENCRYPT:
        ret = qcrypto_akcipher_encrypt(sess->akcipher,
                                       op_info->src, op_info->src_len,
                                       op_info->dst, op_info->dst_len, errp);
        break;

    case VIRTIO_CRYPTO_AKCIPHER_DECRYPT:
        ret = qcrypto_akcipher_decrypt(sess->akcipher,
                                       op_info->src, op_info->src_len,
                                       op_info->dst, op_info->dst_len, errp);
        break;

    case VIRTIO_CRYPTO_AKCIPHER_SIGN:
        ret = qcrypto_akcipher_sign(sess->akcipher,
                                    op_info->src, op_info->src_len,
                                    op_info->dst, op_info->dst_len, errp);
        break;

    case VIRTIO_CRYPTO_AKCIPHER_VERIFY:
        ret = qcrypto_akcipher_verify(sess->akcipher,
                                      op_info->src, op_info->src_len,
                                      op_info->dst, op_info->dst_len, errp);
        break;

    default:
        return -VIRTIO_CRYPTO_ERR;
    }

    if (ret < 0) {
        if (op_code == VIRTIO_CRYPTO_AKCIPHER_VERIFY) {
            return -VIRTIO_CRYPTO_KEY_REJECTED;
        }
        return -VIRTIO_CRYPTO_ERR;
    }

    /* Buffer is too short, typically the driver should handle this case */
    if (unlikely(ret > op_info->dst_len)) {
        if (errp && !*errp) {
            error_setg(errp, "dst buffer too short");
        }

        return -VIRTIO_CRYPTO_ERR;
    }

    op_info->dst_len = ret;

    return VIRTIO_CRYPTO_OK;
}

static int cryptodev_builtin_operation(
                 CryptoDevBackend *backend,
                 CryptoDevBackendOpInfo *op_info)
{
    CryptoDevBackendBuiltin *builtin =
                      CRYPTODEV_BACKEND_BUILTIN(backend);
    CryptoDevBackendBuiltinSession *sess;
    CryptoDevBackendSymOpInfo *sym_op_info;
    CryptoDevBackendAsymOpInfo *asym_op_info;
    QCryptodevBackendAlgType algtype = op_info->algtype;
    int status = -VIRTIO_CRYPTO_ERR;
    Error *local_error = NULL;

    if (op_info->session_id >= MAX_NUM_SESSIONS ||
              builtin->sessions[op_info->session_id] == NULL) {
        error_setg(&local_error, "Cannot find a valid session id: %" PRIu64 "",
                   op_info->session_id);
        return -VIRTIO_CRYPTO_INVSESS;
    }

    sess = builtin->sessions[op_info->session_id];
    if (algtype == QCRYPTODEV_BACKEND_ALG_SYM) {
        sym_op_info = op_info->u.sym_op_info;
        status = cryptodev_builtin_sym_operation(sess, sym_op_info,
                                                 &local_error);
    } else if (algtype == QCRYPTODEV_BACKEND_ALG_ASYM) {
        asym_op_info = op_info->u.asym_op_info;
        status = cryptodev_builtin_asym_operation(sess, op_info->op_code,
                                                  asym_op_info, &local_error);
    }

    if (local_error) {
        error_report_err(local_error);
    }
    if (op_info->cb) {
        op_info->cb(op_info->opaque, status);
    }
    return 0;
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
            cryptodev_builtin_close_session(backend, i, 0, NULL, NULL);
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
    bc->create_session = cryptodev_builtin_create_session;
    bc->close_session = cryptodev_builtin_close_session;
    bc->do_op = cryptodev_builtin_operation;
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
