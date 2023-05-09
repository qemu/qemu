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

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-crypto.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-access.h"
#include "standard-headers/linux/virtio_ids.h"
#include "sysemu/cryptodev-vhost.h"

#define VIRTIO_CRYPTO_VM_VERSION 1

typedef struct VirtIOCryptoSessionReq {
    VirtIODevice *vdev;
    VirtQueue *vq;
    VirtQueueElement *elem;
    CryptoDevBackendSessionInfo info;
    CryptoDevCompletionFunc cb;
} VirtIOCryptoSessionReq;

static void virtio_crypto_free_create_session_req(VirtIOCryptoSessionReq *sreq)
{
    switch (sreq->info.op_code) {
    case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
        g_free(sreq->info.u.sym_sess_info.cipher_key);
        g_free(sreq->info.u.sym_sess_info.auth_key);
        break;

    case VIRTIO_CRYPTO_AKCIPHER_CREATE_SESSION:
        g_free(sreq->info.u.asym_sess_info.key);
        break;

    case VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION:
    case VIRTIO_CRYPTO_HASH_DESTROY_SESSION:
    case VIRTIO_CRYPTO_MAC_DESTROY_SESSION:
    case VIRTIO_CRYPTO_AEAD_DESTROY_SESSION:
    case VIRTIO_CRYPTO_AKCIPHER_DESTROY_SESSION:
        break;

    default:
        error_report("Unknown opcode: %u", sreq->info.op_code);
    }
    g_free(sreq);
}

/*
 * Transfer virtqueue index to crypto queue index.
 * The control virtqueue is after the data virtqueues
 * so the input value doesn't need to be adjusted
 */
static inline int virtio_crypto_vq2q(int queue_index)
{
    return queue_index;
}

static int
virtio_crypto_cipher_session_helper(VirtIODevice *vdev,
           CryptoDevBackendSymSessionInfo *info,
           struct virtio_crypto_cipher_session_para *cipher_para,
           struct iovec **iov, unsigned int *out_num)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    unsigned int num = *out_num;

    info->cipher_alg = ldl_le_p(&cipher_para->algo);
    info->key_len = ldl_le_p(&cipher_para->keylen);
    info->direction = ldl_le_p(&cipher_para->op);
    DPRINTF("cipher_alg=%" PRIu32 ", info->direction=%" PRIu32 "\n",
             info->cipher_alg, info->direction);

    if (info->key_len > vcrypto->conf.max_cipher_key_len) {
        error_report("virtio-crypto length of cipher key is too big: %u",
                     info->key_len);
        return -VIRTIO_CRYPTO_ERR;
    }
    /* Get cipher key */
    if (info->key_len > 0) {
        size_t s;
        DPRINTF("keylen=%" PRIu32 "\n", info->key_len);

        info->cipher_key = g_malloc(info->key_len);
        s = iov_to_buf(*iov, num, 0, info->cipher_key, info->key_len);
        if (unlikely(s != info->key_len)) {
            virtio_error(vdev, "virtio-crypto cipher key incorrect");
            return -EFAULT;
        }
        iov_discard_front(iov, &num, info->key_len);
        *out_num = num;
    }

    return 0;
}

static int
virtio_crypto_create_sym_session(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_create_session_req *sess_req,
               uint32_t queue_id,
               uint32_t opcode,
               struct iovec *iov, unsigned int out_num,
               VirtIOCryptoSessionReq *sreq)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    CryptoDevBackendSymSessionInfo *sym_info = &sreq->info.u.sym_sess_info;
    int queue_index;
    uint32_t op_type;
    int ret;

    op_type = ldl_le_p(&sess_req->op_type);
    sreq->info.op_code = opcode;

    sym_info = &sreq->info.u.sym_sess_info;
    sym_info->op_type = op_type;

    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        ret = virtio_crypto_cipher_session_helper(vdev, sym_info,
                           &sess_req->u.cipher.para,
                           &iov, &out_num);
        if (ret < 0) {
            return ret;
        }
    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        size_t s;
        /* cipher part */
        ret = virtio_crypto_cipher_session_helper(vdev, sym_info,
                           &sess_req->u.chain.para.cipher_param,
                           &iov, &out_num);
        if (ret < 0) {
            return ret;
        }
        /* hash part */
        sym_info->alg_chain_order = ldl_le_p(
                                     &sess_req->u.chain.para.alg_chain_order);
        sym_info->add_len = ldl_le_p(&sess_req->u.chain.para.aad_len);
        sym_info->hash_mode = ldl_le_p(&sess_req->u.chain.para.hash_mode);
        if (sym_info->hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_AUTH) {
            sym_info->hash_alg =
                ldl_le_p(&sess_req->u.chain.para.u.mac_param.algo);
            sym_info->auth_key_len = ldl_le_p(
                             &sess_req->u.chain.para.u.mac_param.auth_key_len);
            sym_info->hash_result_len = ldl_le_p(
                           &sess_req->u.chain.para.u.mac_param.hash_result_len);
            if (sym_info->auth_key_len > vcrypto->conf.max_auth_key_len) {
                error_report("virtio-crypto length of auth key is too big: %u",
                             sym_info->auth_key_len);
                return -VIRTIO_CRYPTO_ERR;
            }
            /* get auth key */
            if (sym_info->auth_key_len > 0) {
                sym_info->auth_key = g_malloc(sym_info->auth_key_len);
                s = iov_to_buf(iov, out_num, 0, sym_info->auth_key,
                               sym_info->auth_key_len);
                if (unlikely(s != sym_info->auth_key_len)) {
                    virtio_error(vdev,
                          "virtio-crypto authenticated key incorrect");
                    return -EFAULT;
                }
                iov_discard_front(&iov, &out_num, sym_info->auth_key_len);
            }
        } else if (sym_info->hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_PLAIN) {
            sym_info->hash_alg = ldl_le_p(
                             &sess_req->u.chain.para.u.hash_param.algo);
            sym_info->hash_result_len = ldl_le_p(
                        &sess_req->u.chain.para.u.hash_param.hash_result_len);
        } else {
            /* VIRTIO_CRYPTO_SYM_HASH_MODE_NESTED */
            error_report("unsupported hash mode");
            return -VIRTIO_CRYPTO_NOTSUPP;
        }
    } else {
        /* VIRTIO_CRYPTO_SYM_OP_NONE */
        error_report("unsupported cipher op_type: VIRTIO_CRYPTO_SYM_OP_NONE");
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    queue_index = virtio_crypto_vq2q(queue_id);
    return cryptodev_backend_create_session(vcrypto->cryptodev, &sreq->info,
                                            queue_index, sreq->cb, sreq);
}

static int
virtio_crypto_create_asym_session(VirtIOCrypto *vcrypto,
               struct virtio_crypto_akcipher_create_session_req *sess_req,
               uint32_t queue_id, uint32_t opcode,
               struct iovec *iov, unsigned int out_num,
               VirtIOCryptoSessionReq *sreq)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    CryptoDevBackendAsymSessionInfo *asym_info = &sreq->info.u.asym_sess_info;
    int queue_index;
    uint32_t algo, keytype, keylen;

    algo = ldl_le_p(&sess_req->para.algo);
    keytype = ldl_le_p(&sess_req->para.keytype);
    keylen = ldl_le_p(&sess_req->para.keylen);

    if ((keytype != VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PUBLIC)
         && (keytype != VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE)) {
        error_report("unsupported asym keytype: %d", keytype);
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    if (keylen) {
        asym_info->key = g_malloc(keylen);
        if (iov_to_buf(iov, out_num, 0, asym_info->key, keylen) != keylen) {
            virtio_error(vdev, "virtio-crypto asym key incorrect");
            return -EFAULT;
        }
        iov_discard_front(&iov, &out_num, keylen);
    }

    sreq->info.op_code = opcode;
    asym_info = &sreq->info.u.asym_sess_info;
    asym_info->algo = algo;
    asym_info->keytype = keytype;
    asym_info->keylen = keylen;
    switch (asym_info->algo) {
    case VIRTIO_CRYPTO_AKCIPHER_RSA:
        asym_info->u.rsa.padding_algo =
            ldl_le_p(&sess_req->para.u.rsa.padding_algo);
        asym_info->u.rsa.hash_algo =
            ldl_le_p(&sess_req->para.u.rsa.hash_algo);
        break;

    /* TODO DSA&ECDSA handling */

    default:
        return -VIRTIO_CRYPTO_ERR;
    }

    queue_index = virtio_crypto_vq2q(queue_id);
    return cryptodev_backend_create_session(vcrypto->cryptodev, &sreq->info,
                                            queue_index, sreq->cb, sreq);
}

static int
virtio_crypto_handle_close_session(VirtIOCrypto *vcrypto,
         struct virtio_crypto_destroy_session_req *close_sess_req,
         uint32_t queue_id,
         VirtIOCryptoSessionReq *sreq)
{
    uint64_t session_id;

    session_id = ldq_le_p(&close_sess_req->session_id);
    DPRINTF("close session, id=%" PRIu64 "\n", session_id);

    return cryptodev_backend_close_session(
                vcrypto->cryptodev, session_id, queue_id, sreq->cb, sreq);
}

static void virtio_crypto_create_session_completion(void *opaque, int ret)
{
    VirtIOCryptoSessionReq *sreq = (VirtIOCryptoSessionReq *)opaque;
    VirtQueue *vq = sreq->vq;
    VirtQueueElement *elem = sreq->elem;
    VirtIODevice *vdev = sreq->vdev;
    struct virtio_crypto_session_input input;
    struct iovec *in_iov = elem->in_sg;
    unsigned in_num = elem->in_num;
    size_t s;

    memset(&input, 0, sizeof(input));
    /* Serious errors, need to reset virtio crypto device */
    if (ret == -EFAULT) {
        virtqueue_detach_element(vq, elem, 0);
        goto out;
    } else if (ret == -VIRTIO_CRYPTO_NOTSUPP) {
        stl_le_p(&input.status, VIRTIO_CRYPTO_NOTSUPP);
    } else if (ret == -VIRTIO_CRYPTO_KEY_REJECTED) {
        stl_le_p(&input.status, VIRTIO_CRYPTO_KEY_REJECTED);
    } else if (ret != VIRTIO_CRYPTO_OK) {
        stl_le_p(&input.status, VIRTIO_CRYPTO_ERR);
    } else {
        /* Set the session id */
        stq_le_p(&input.session_id, sreq->info.session_id);
        stl_le_p(&input.status, VIRTIO_CRYPTO_OK);
    }

    s = iov_from_buf(in_iov, in_num, 0, &input, sizeof(input));
    if (unlikely(s != sizeof(input))) {
        virtio_error(vdev, "virtio-crypto input incorrect");
        virtqueue_detach_element(vq, elem, 0);
        goto out;
    }
    virtqueue_push(vq, elem, sizeof(input));
    virtio_notify(vdev, vq);

out:
    g_free(elem);
    virtio_crypto_free_create_session_req(sreq);
}

static void virtio_crypto_destroy_session_completion(void *opaque, int ret)
{
    VirtIOCryptoSessionReq *sreq = (VirtIOCryptoSessionReq *)opaque;
    VirtQueue *vq = sreq->vq;
    VirtQueueElement *elem = sreq->elem;
    VirtIODevice *vdev = sreq->vdev;
    struct iovec *in_iov = elem->in_sg;
    unsigned in_num = elem->in_num;
    uint8_t status;
    size_t s;

    if (ret < 0) {
        status = VIRTIO_CRYPTO_ERR;
    } else {
        status = VIRTIO_CRYPTO_OK;
    }
    s = iov_from_buf(in_iov, in_num, 0, &status, sizeof(status));
    if (unlikely(s != sizeof(status))) {
        virtio_error(vdev, "virtio-crypto status incorrect");
        virtqueue_detach_element(vq, elem, 0);
        goto out;
    }
    virtqueue_push(vq, elem, sizeof(status));
    virtio_notify(vdev, vq);

out:
    g_free(elem);
    g_free(sreq);
}

static void virtio_crypto_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_op_ctrl_req ctrl;
    VirtQueueElement *elem;
    VirtIOCryptoSessionReq *sreq;
    unsigned out_num;
    unsigned in_num;
    uint32_t queue_id;
    uint32_t opcode;
    struct virtio_crypto_session_input input;
    size_t s;
    int ret;
    struct iovec *out_iov;
    struct iovec *in_iov;

    for (;;) {
        g_autofree struct iovec *out_iov_copy = NULL;

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }
        if (elem->out_num < 1 || elem->in_num < 1) {
            virtio_error(vdev, "virtio-crypto ctrl missing headers");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }

        out_num = elem->out_num;
        out_iov_copy = g_memdup2(elem->out_sg, sizeof(out_iov[0]) * out_num);
        out_iov = out_iov_copy;

        in_num = elem->in_num;
        in_iov = elem->in_sg;

        if (unlikely(iov_to_buf(out_iov, out_num, 0, &ctrl, sizeof(ctrl))
                    != sizeof(ctrl))) {
            virtio_error(vdev, "virtio-crypto request ctrl_hdr too short");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }
        iov_discard_front(&out_iov, &out_num, sizeof(ctrl));

        opcode = ldl_le_p(&ctrl.header.opcode);
        queue_id = ldl_le_p(&ctrl.header.queue_id);

        sreq = g_new0(VirtIOCryptoSessionReq, 1);
        sreq->vdev = vdev;
        sreq->vq = vq;
        sreq->elem = elem;

        switch (opcode) {
        case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
            sreq->cb = virtio_crypto_create_session_completion;
            ret = virtio_crypto_create_sym_session(vcrypto,
                            &ctrl.u.sym_create_session,
                            queue_id, opcode,
                            out_iov, out_num,
                            sreq);
            if (ret < 0) {
                virtio_crypto_create_session_completion(sreq, ret);
            }
            break;

        case VIRTIO_CRYPTO_AKCIPHER_CREATE_SESSION:
            sreq->cb = virtio_crypto_create_session_completion;
            ret = virtio_crypto_create_asym_session(vcrypto,
                             &ctrl.u.akcipher_create_session,
                             queue_id, opcode,
                             out_iov, out_num,
                             sreq);
            if (ret < 0) {
                virtio_crypto_create_session_completion(sreq, ret);
            }
            break;

        case VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION:
        case VIRTIO_CRYPTO_HASH_DESTROY_SESSION:
        case VIRTIO_CRYPTO_MAC_DESTROY_SESSION:
        case VIRTIO_CRYPTO_AEAD_DESTROY_SESSION:
        case VIRTIO_CRYPTO_AKCIPHER_DESTROY_SESSION:
            sreq->cb = virtio_crypto_destroy_session_completion;
            ret = virtio_crypto_handle_close_session(vcrypto,
                   &ctrl.u.destroy_session, queue_id,
                   sreq);
            if (ret < 0) {
                virtio_crypto_destroy_session_completion(sreq, ret);
            }
            break;

        case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
        case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
        case VIRTIO_CRYPTO_AEAD_CREATE_SESSION:
        default:
            memset(&input, 0, sizeof(input));
            error_report("virtio-crypto unsupported ctrl opcode: %d", opcode);
            stl_le_p(&input.status, VIRTIO_CRYPTO_NOTSUPP);
            s = iov_from_buf(in_iov, in_num, 0, &input, sizeof(input));
            if (unlikely(s != sizeof(input))) {
                virtio_error(vdev, "virtio-crypto input incorrect");
                virtqueue_detach_element(vq, elem, 0);
            } else {
                virtqueue_push(vq, elem, sizeof(input));
                virtio_notify(vdev, vq);
            }
            g_free(sreq);
            g_free(elem);

            break;
        } /* end switch case */

    } /* end for loop */
}

static void virtio_crypto_init_request(VirtIOCrypto *vcrypto, VirtQueue *vq,
                                VirtIOCryptoReq *req)
{
    req->vcrypto = vcrypto;
    req->vq = vq;
    req->in = NULL;
    req->in_iov = NULL;
    req->in_num = 0;
    req->in_len = 0;
    req->flags = CRYPTODEV_BACKEND_ALG__MAX;
    memset(&req->op_info, 0x00, sizeof(req->op_info));
}

static void virtio_crypto_free_request(VirtIOCryptoReq *req)
{
    if (!req) {
        return;
    }

    if (req->flags == CRYPTODEV_BACKEND_ALG_SYM) {
        size_t max_len;
        CryptoDevBackendSymOpInfo *op_info = req->op_info.u.sym_op_info;

        if (op_info) {
            max_len = op_info->iv_len +
                      op_info->aad_len +
                      op_info->src_len +
                      op_info->dst_len +
                      op_info->digest_result_len;

            /* Zeroize and free request data structure */
            memset(op_info, 0, sizeof(*op_info) + max_len);
            g_free(op_info);
        }
    } else if (req->flags == CRYPTODEV_BACKEND_ALG_ASYM) {
        CryptoDevBackendAsymOpInfo *op_info = req->op_info.u.asym_op_info;
        if (op_info) {
            g_free(op_info->src);
            g_free(op_info->dst);
            memset(op_info, 0, sizeof(*op_info));
            g_free(op_info);
        }
    }

    g_free(req->in_iov);
    g_free(req);
}

static void
virtio_crypto_sym_input_data_helper(VirtIODevice *vdev,
                VirtIOCryptoReq *req,
                uint32_t status,
                CryptoDevBackendSymOpInfo *sym_op_info)
{
    size_t s, len;
    struct iovec *in_iov = req->in_iov;

    if (status != VIRTIO_CRYPTO_OK) {
        return;
    }

    len = sym_op_info->src_len;
    /* Save the cipher result */
    s = iov_from_buf(in_iov, req->in_num, 0, sym_op_info->dst, len);
    if (s != len) {
        virtio_error(vdev, "virtio-crypto dest data incorrect");
        return;
    }

    iov_discard_front(&in_iov, &req->in_num, len);

    if (sym_op_info->op_type ==
                      VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        /* Save the digest result */
        s = iov_from_buf(in_iov, req->in_num, 0,
                         sym_op_info->digest_result,
                         sym_op_info->digest_result_len);
        if (s != sym_op_info->digest_result_len) {
            virtio_error(vdev, "virtio-crypto digest result incorrect");
        }
    }
}

static void
virtio_crypto_akcipher_input_data_helper(VirtIODevice *vdev,
        VirtIOCryptoReq *req, int32_t status,
        CryptoDevBackendAsymOpInfo *asym_op_info)
{
    size_t s, len;
    struct iovec *in_iov = req->in_iov;

    if (status != VIRTIO_CRYPTO_OK) {
        return;
    }

    len = asym_op_info->dst_len;
    if (!len) {
        return;
    }

    s = iov_from_buf(in_iov, req->in_num, 0, asym_op_info->dst, len);
    if (s != len) {
        virtio_error(vdev, "virtio-crypto asym dest data incorrect");
        return;
    }

    iov_discard_front(&in_iov, &req->in_num, len);

    /* For akcipher, dst_len may be changed after operation */
    req->in_len = sizeof(struct virtio_crypto_inhdr) + asym_op_info->dst_len;
}

static void virtio_crypto_req_complete(void *opaque, int ret)
{
    VirtIOCryptoReq *req = (VirtIOCryptoReq *)opaque;
    VirtIOCrypto *vcrypto = req->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    uint8_t status = -ret;

    if (req->flags == CRYPTODEV_BACKEND_ALG_SYM) {
        virtio_crypto_sym_input_data_helper(vdev, req, status,
                                            req->op_info.u.sym_op_info);
    } else if (req->flags == CRYPTODEV_BACKEND_ALG_ASYM) {
        virtio_crypto_akcipher_input_data_helper(vdev, req, status,
                                             req->op_info.u.asym_op_info);
    }
    stb_p(&req->in->status, status);
    virtqueue_push(req->vq, &req->elem, req->in_len);
    virtio_notify(vdev, req->vq);
    virtio_crypto_free_request(req);
}

static VirtIOCryptoReq *
virtio_crypto_get_request(VirtIOCrypto *s, VirtQueue *vq)
{
    VirtIOCryptoReq *req = virtqueue_pop(vq, sizeof(VirtIOCryptoReq));

    if (req) {
        virtio_crypto_init_request(s, vq, req);
    }
    return req;
}

static CryptoDevBackendSymOpInfo *
virtio_crypto_sym_op_helper(VirtIODevice *vdev,
           struct virtio_crypto_cipher_para *cipher_para,
           struct virtio_crypto_alg_chain_data_para *alg_chain_para,
           struct iovec *iov, unsigned int out_num)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    CryptoDevBackendSymOpInfo *op_info;
    uint32_t src_len = 0, dst_len = 0;
    uint32_t iv_len = 0;
    uint32_t aad_len = 0, hash_result_len = 0;
    uint32_t hash_start_src_offset = 0, len_to_hash = 0;
    uint32_t cipher_start_src_offset = 0, len_to_cipher = 0;

    uint64_t max_len, curr_size = 0;
    size_t s;

    /* Plain cipher */
    if (cipher_para) {
        iv_len = ldl_le_p(&cipher_para->iv_len);
        src_len = ldl_le_p(&cipher_para->src_data_len);
        dst_len = ldl_le_p(&cipher_para->dst_data_len);
    } else if (alg_chain_para) { /* Algorithm chain */
        iv_len = ldl_le_p(&alg_chain_para->iv_len);
        src_len = ldl_le_p(&alg_chain_para->src_data_len);
        dst_len = ldl_le_p(&alg_chain_para->dst_data_len);

        aad_len = ldl_le_p(&alg_chain_para->aad_len);
        hash_result_len = ldl_le_p(&alg_chain_para->hash_result_len);
        hash_start_src_offset = ldl_le_p(
                         &alg_chain_para->hash_start_src_offset);
        cipher_start_src_offset = ldl_le_p(
                         &alg_chain_para->cipher_start_src_offset);
        len_to_cipher = ldl_le_p(&alg_chain_para->len_to_cipher);
        len_to_hash = ldl_le_p(&alg_chain_para->len_to_hash);
    } else {
        return NULL;
    }

    max_len = (uint64_t)iv_len + aad_len + src_len + dst_len + hash_result_len;
    if (unlikely(max_len > vcrypto->conf.max_size)) {
        virtio_error(vdev, "virtio-crypto too big length");
        return NULL;
    }

    op_info = g_malloc0(sizeof(CryptoDevBackendSymOpInfo) + max_len);
    op_info->iv_len = iv_len;
    op_info->src_len = src_len;
    op_info->dst_len = dst_len;
    op_info->aad_len = aad_len;
    op_info->digest_result_len = hash_result_len;
    op_info->hash_start_src_offset = hash_start_src_offset;
    op_info->len_to_hash = len_to_hash;
    op_info->cipher_start_src_offset = cipher_start_src_offset;
    op_info->len_to_cipher = len_to_cipher;
    /* Handle the initilization vector */
    if (op_info->iv_len > 0) {
        DPRINTF("iv_len=%" PRIu32 "\n", op_info->iv_len);
        op_info->iv = op_info->data + curr_size;

        s = iov_to_buf(iov, out_num, 0, op_info->iv, op_info->iv_len);
        if (unlikely(s != op_info->iv_len)) {
            virtio_error(vdev, "virtio-crypto iv incorrect");
            goto err;
        }
        iov_discard_front(&iov, &out_num, op_info->iv_len);
        curr_size += op_info->iv_len;
    }

    /* Handle additional authentication data if exists */
    if (op_info->aad_len > 0) {
        DPRINTF("aad_len=%" PRIu32 "\n", op_info->aad_len);
        op_info->aad_data = op_info->data + curr_size;

        s = iov_to_buf(iov, out_num, 0, op_info->aad_data, op_info->aad_len);
        if (unlikely(s != op_info->aad_len)) {
            virtio_error(vdev, "virtio-crypto additional auth data incorrect");
            goto err;
        }
        iov_discard_front(&iov, &out_num, op_info->aad_len);

        curr_size += op_info->aad_len;
    }

    /* Handle the source data */
    if (op_info->src_len > 0) {
        DPRINTF("src_len=%" PRIu32 "\n", op_info->src_len);
        op_info->src = op_info->data + curr_size;

        s = iov_to_buf(iov, out_num, 0, op_info->src, op_info->src_len);
        if (unlikely(s != op_info->src_len)) {
            virtio_error(vdev, "virtio-crypto source data incorrect");
            goto err;
        }
        iov_discard_front(&iov, &out_num, op_info->src_len);

        curr_size += op_info->src_len;
    }

    /* Handle the destination data */
    op_info->dst = op_info->data + curr_size;
    curr_size += op_info->dst_len;

    DPRINTF("dst_len=%" PRIu32 "\n", op_info->dst_len);

    /* Handle the hash digest result */
    if (hash_result_len > 0) {
        DPRINTF("hash_result_len=%" PRIu32 "\n", hash_result_len);
        op_info->digest_result = op_info->data + curr_size;
    }

    return op_info;

err:
    g_free(op_info);
    return NULL;
}

static int
virtio_crypto_handle_sym_req(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_data_req *req,
               CryptoDevBackendOpInfo *op_info,
               struct iovec *iov, unsigned int out_num)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    CryptoDevBackendSymOpInfo *sym_op_info;
    uint32_t op_type;

    op_type = ldl_le_p(&req->op_type);
    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        sym_op_info = virtio_crypto_sym_op_helper(vdev, &req->u.cipher.para,
                                              NULL, iov, out_num);
        if (!sym_op_info) {
            return -EFAULT;
        }
    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        sym_op_info = virtio_crypto_sym_op_helper(vdev, NULL,
                                              &req->u.chain.para,
                                              iov, out_num);
        if (!sym_op_info) {
            return -EFAULT;
        }
    } else {
        /* VIRTIO_CRYPTO_SYM_OP_NONE */
        error_report("virtio-crypto unsupported cipher type");
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    sym_op_info->op_type = op_type;
    op_info->u.sym_op_info = sym_op_info;

    return 0;
}

static int
virtio_crypto_handle_asym_req(VirtIOCrypto *vcrypto,
               struct virtio_crypto_akcipher_data_req *req,
               CryptoDevBackendOpInfo *op_info,
               struct iovec *iov, unsigned int out_num)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    CryptoDevBackendAsymOpInfo *asym_op_info;
    uint32_t src_len;
    uint32_t dst_len;
    uint32_t len;
    uint8_t *src = NULL;
    uint8_t *dst = NULL;

    asym_op_info = g_new0(CryptoDevBackendAsymOpInfo, 1);
    src_len = ldl_le_p(&req->para.src_data_len);
    dst_len = ldl_le_p(&req->para.dst_data_len);

    if (src_len > 0) {
        src = g_malloc0(src_len);
        len = iov_to_buf(iov, out_num, 0, src, src_len);
        if (unlikely(len != src_len)) {
            virtio_error(vdev, "virtio-crypto asym src data incorrect"
                         "expected %u, actual %u", src_len, len);
            goto err;
        }

        iov_discard_front(&iov, &out_num, src_len);
    }

    if (dst_len > 0) {
        dst = g_malloc0(dst_len);

        if (op_info->op_code == VIRTIO_CRYPTO_AKCIPHER_VERIFY) {
            len = iov_to_buf(iov, out_num, 0, dst, dst_len);
            if (unlikely(len != dst_len)) {
                virtio_error(vdev, "virtio-crypto asym dst data incorrect"
                             "expected %u, actual %u", dst_len, len);
                goto err;
            }

            iov_discard_front(&iov, &out_num, dst_len);
        }
    }

    asym_op_info->src_len = src_len;
    asym_op_info->dst_len = dst_len;
    asym_op_info->src = src;
    asym_op_info->dst = dst;
    op_info->u.asym_op_info = asym_op_info;

    return 0;

 err:
    g_free(asym_op_info);
    g_free(src);
    g_free(dst);

    return -EFAULT;
}

static int
virtio_crypto_handle_request(VirtIOCryptoReq *request)
{
    VirtIOCrypto *vcrypto = request->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    VirtQueueElement *elem = &request->elem;
    int queue_index = virtio_crypto_vq2q(virtio_get_queue_index(request->vq));
    struct virtio_crypto_op_data_req req;
    int ret;
    g_autofree struct iovec *in_iov_copy = NULL;
    g_autofree struct iovec *out_iov_copy = NULL;
    struct iovec *in_iov;
    struct iovec *out_iov;
    unsigned in_num;
    unsigned out_num;
    uint32_t opcode;
    CryptoDevBackendOpInfo *op_info = &request->op_info;

    if (elem->out_num < 1 || elem->in_num < 1) {
        virtio_error(vdev, "virtio-crypto dataq missing headers");
        return -1;
    }

    out_num = elem->out_num;
    out_iov_copy = g_memdup2(elem->out_sg, sizeof(out_iov[0]) * out_num);
    out_iov = out_iov_copy;

    in_num = elem->in_num;
    in_iov_copy = g_memdup2(elem->in_sg, sizeof(in_iov[0]) * in_num);
    in_iov = in_iov_copy;

    if (unlikely(iov_to_buf(out_iov, out_num, 0, &req, sizeof(req))
                != sizeof(req))) {
        virtio_error(vdev, "virtio-crypto request outhdr too short");
        return -1;
    }
    iov_discard_front(&out_iov, &out_num, sizeof(req));

    if (in_iov[in_num - 1].iov_len <
            sizeof(struct virtio_crypto_inhdr)) {
        virtio_error(vdev, "virtio-crypto request inhdr too short");
        return -1;
    }
    /* We always touch the last byte, so just see how big in_iov is. */
    request->in_len = iov_size(in_iov, in_num);
    request->in = (void *)in_iov[in_num - 1].iov_base
              + in_iov[in_num - 1].iov_len
              - sizeof(struct virtio_crypto_inhdr);
    iov_discard_back(in_iov, &in_num, sizeof(struct virtio_crypto_inhdr));

    /*
     * The length of operation result, including dest_data
     * and digest_result if exists.
     */
    request->in_num = in_num;
    request->in_iov = in_iov;
    /* now, we free the in_iov_copy inside virtio_crypto_free_request */
    in_iov_copy = NULL;

    opcode = ldl_le_p(&req.header.opcode);
    op_info->session_id = ldq_le_p(&req.header.session_id);
    op_info->op_code = opcode;

    switch (opcode) {
    case VIRTIO_CRYPTO_CIPHER_ENCRYPT:
    case VIRTIO_CRYPTO_CIPHER_DECRYPT:
        op_info->algtype = request->flags = CRYPTODEV_BACKEND_ALG_SYM;
        ret = virtio_crypto_handle_sym_req(vcrypto,
                         &req.u.sym_req, op_info,
                         out_iov, out_num);
        goto check_result;

    case VIRTIO_CRYPTO_AKCIPHER_ENCRYPT:
    case VIRTIO_CRYPTO_AKCIPHER_DECRYPT:
    case VIRTIO_CRYPTO_AKCIPHER_SIGN:
    case VIRTIO_CRYPTO_AKCIPHER_VERIFY:
        op_info->algtype = request->flags = CRYPTODEV_BACKEND_ALG_ASYM;
        ret = virtio_crypto_handle_asym_req(vcrypto,
                         &req.u.akcipher_req, op_info,
                         out_iov, out_num);

check_result:
        /* Serious errors, need to reset virtio crypto device */
        if (ret == -EFAULT) {
            return -1;
        } else if (ret == -VIRTIO_CRYPTO_NOTSUPP) {
            virtio_crypto_req_complete(request, -VIRTIO_CRYPTO_NOTSUPP);
        } else {
            ret = cryptodev_backend_crypto_operation(vcrypto->cryptodev,
                                    request, queue_index,
                                    virtio_crypto_req_complete,
                                    request);
            if (ret < 0) {
                virtio_crypto_req_complete(request, ret);
            }
        }
        break;

    case VIRTIO_CRYPTO_HASH:
    case VIRTIO_CRYPTO_MAC:
    case VIRTIO_CRYPTO_AEAD_ENCRYPT:
    case VIRTIO_CRYPTO_AEAD_DECRYPT:
    default:
        error_report("virtio-crypto unsupported dataq opcode: %u",
                     opcode);
        virtio_crypto_req_complete(request, -VIRTIO_CRYPTO_NOTSUPP);
    }

    return 0;
}

static void virtio_crypto_handle_dataq(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    VirtIOCryptoReq *req;

    while ((req = virtio_crypto_get_request(vcrypto, vq))) {
        if (virtio_crypto_handle_request(req) < 0) {
            virtqueue_detach_element(req->vq, &req->elem, 0);
            virtio_crypto_free_request(req);
            break;
        }
    }
}

static void virtio_crypto_dataq_bh(void *opaque)
{
    VirtIOCryptoQueue *q = opaque;
    VirtIOCrypto *vcrypto = q->vcrypto;
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);

    /* This happens when device was stopped but BH wasn't. */
    if (!vdev->vm_running) {
        return;
    }

    /* Just in case the driver is not ready on more */
    if (unlikely(!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))) {
        return;
    }

    for (;;) {
        virtio_crypto_handle_dataq(vdev, q->dataq);
        virtio_queue_set_notification(q->dataq, 1);

        /* Are we done or did the guest add more buffers? */
        if (virtio_queue_empty(q->dataq)) {
            break;
        }

        virtio_queue_set_notification(q->dataq, 0);
    }
}

static void
virtio_crypto_handle_dataq_bh(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    VirtIOCryptoQueue *q =
         &vcrypto->vqs[virtio_crypto_vq2q(virtio_get_queue_index(vq))];

    /* This happens when device was stopped but VCPU wasn't. */
    if (!vdev->vm_running) {
        return;
    }
    virtio_queue_set_notification(vq, 0);
    qemu_bh_schedule(q->dataq_bh);
}

static uint64_t virtio_crypto_get_features(VirtIODevice *vdev,
                                           uint64_t features,
                                           Error **errp)
{
    return features;
}

static void virtio_crypto_reset(VirtIODevice *vdev)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    /* multiqueue is disabled by default */
    vcrypto->curr_queues = 1;
    if (!cryptodev_backend_is_ready(vcrypto->cryptodev)) {
        vcrypto->status &= ~VIRTIO_CRYPTO_S_HW_READY;
    } else {
        vcrypto->status |= VIRTIO_CRYPTO_S_HW_READY;
    }
}

static void virtio_crypto_init_config(VirtIODevice *vdev)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);

    vcrypto->conf.crypto_services =
                     vcrypto->conf.cryptodev->conf.crypto_services;
    vcrypto->conf.cipher_algo_l =
                     vcrypto->conf.cryptodev->conf.cipher_algo_l;
    vcrypto->conf.cipher_algo_h =
                     vcrypto->conf.cryptodev->conf.cipher_algo_h;
    vcrypto->conf.hash_algo = vcrypto->conf.cryptodev->conf.hash_algo;
    vcrypto->conf.mac_algo_l = vcrypto->conf.cryptodev->conf.mac_algo_l;
    vcrypto->conf.mac_algo_h = vcrypto->conf.cryptodev->conf.mac_algo_h;
    vcrypto->conf.aead_algo = vcrypto->conf.cryptodev->conf.aead_algo;
    vcrypto->conf.akcipher_algo = vcrypto->conf.cryptodev->conf.akcipher_algo;
    vcrypto->conf.max_cipher_key_len =
                  vcrypto->conf.cryptodev->conf.max_cipher_key_len;
    vcrypto->conf.max_auth_key_len =
                  vcrypto->conf.cryptodev->conf.max_auth_key_len;
    vcrypto->conf.max_size = vcrypto->conf.cryptodev->conf.max_size;
}

static void virtio_crypto_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    int i;

    vcrypto->cryptodev = vcrypto->conf.cryptodev;
    if (vcrypto->cryptodev == NULL) {
        error_setg(errp, "'cryptodev' parameter expects a valid object");
        return;
    } else if (cryptodev_backend_is_used(vcrypto->cryptodev)) {
        error_setg(errp, "can't use already used cryptodev backend: %s",
                   object_get_canonical_path_component(OBJECT(vcrypto->conf.cryptodev)));
        return;
    }

    vcrypto->max_queues = MAX(vcrypto->cryptodev->conf.peers.queues, 1);
    if (vcrypto->max_queues + 1 > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "Invalid number of queues (= %" PRIu32 "), "
                   "must be a positive integer less than %d.",
                   vcrypto->max_queues, VIRTIO_QUEUE_MAX);
        return;
    }

    virtio_init(vdev, VIRTIO_ID_CRYPTO, vcrypto->config_size);
    vcrypto->curr_queues = 1;
    vcrypto->vqs = g_new0(VirtIOCryptoQueue, vcrypto->max_queues);
    for (i = 0; i < vcrypto->max_queues; i++) {
        vcrypto->vqs[i].dataq =
                 virtio_add_queue(vdev, 1024, virtio_crypto_handle_dataq_bh);
        vcrypto->vqs[i].dataq_bh =
                 qemu_bh_new(virtio_crypto_dataq_bh, &vcrypto->vqs[i]);
        vcrypto->vqs[i].vcrypto = vcrypto;
    }

    vcrypto->ctrl_vq = virtio_add_queue(vdev, 1024, virtio_crypto_handle_ctrl);
    if (!cryptodev_backend_is_ready(vcrypto->cryptodev)) {
        vcrypto->status &= ~VIRTIO_CRYPTO_S_HW_READY;
    } else {
        vcrypto->status |= VIRTIO_CRYPTO_S_HW_READY;
    }

    virtio_crypto_init_config(vdev);
    cryptodev_backend_set_used(vcrypto->cryptodev, true);
}

static void virtio_crypto_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(dev);
    VirtIOCryptoQueue *q;
    int i, max_queues;

    max_queues = vcrypto->multiqueue ? vcrypto->max_queues : 1;
    for (i = 0; i < max_queues; i++) {
        virtio_delete_queue(vcrypto->vqs[i].dataq);
        q = &vcrypto->vqs[i];
        qemu_bh_delete(q->dataq_bh);
    }

    g_free(vcrypto->vqs);
    virtio_delete_queue(vcrypto->ctrl_vq);

    virtio_cleanup(vdev);
    cryptodev_backend_set_used(vcrypto->cryptodev, false);
}

static const VMStateDescription vmstate_virtio_crypto = {
    .name = "virtio-crypto",
    .unmigratable = 1,
    .minimum_version_id = VIRTIO_CRYPTO_VM_VERSION,
    .version_id = VIRTIO_CRYPTO_VM_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_crypto_properties[] = {
    DEFINE_PROP_LINK("cryptodev", VirtIOCrypto, conf.cryptodev,
                     TYPE_CRYPTODEV_BACKEND, CryptoDevBackend *),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_crypto_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOCrypto *c = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_config crypto_cfg = {};

    /*
     * Virtio-crypto device conforms to VIRTIO 1.0 which is always LE,
     * so we can use LE accessors directly.
     */
    stl_le_p(&crypto_cfg.status, c->status);
    stl_le_p(&crypto_cfg.max_dataqueues, c->max_queues);
    stl_le_p(&crypto_cfg.crypto_services, c->conf.crypto_services);
    stl_le_p(&crypto_cfg.cipher_algo_l, c->conf.cipher_algo_l);
    stl_le_p(&crypto_cfg.cipher_algo_h, c->conf.cipher_algo_h);
    stl_le_p(&crypto_cfg.hash_algo, c->conf.hash_algo);
    stl_le_p(&crypto_cfg.mac_algo_l, c->conf.mac_algo_l);
    stl_le_p(&crypto_cfg.mac_algo_h, c->conf.mac_algo_h);
    stl_le_p(&crypto_cfg.aead_algo, c->conf.aead_algo);
    stl_le_p(&crypto_cfg.max_cipher_key_len, c->conf.max_cipher_key_len);
    stl_le_p(&crypto_cfg.max_auth_key_len, c->conf.max_auth_key_len);
    stq_le_p(&crypto_cfg.max_size, c->conf.max_size);
    stl_le_p(&crypto_cfg.akcipher_algo, c->conf.akcipher_algo);

    memcpy(config, &crypto_cfg, c->config_size);
}

static bool virtio_crypto_started(VirtIOCrypto *c, uint8_t status)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(c);
    return (status & VIRTIO_CONFIG_S_DRIVER_OK) &&
        (c->status & VIRTIO_CRYPTO_S_HW_READY) && vdev->vm_running;
}

static void virtio_crypto_vhost_status(VirtIOCrypto *c, uint8_t status)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(c);
    int queues = c->multiqueue ? c->max_queues : 1;
    CryptoDevBackend *b = c->cryptodev;
    CryptoDevBackendClient *cc = b->conf.peers.ccs[0];

    if (!cryptodev_get_vhost(cc, b, 0)) {
        return;
    }

    if ((virtio_crypto_started(c, status)) == !!c->vhost_started) {
        return;
    }

    if (!c->vhost_started) {
        int r;

        c->vhost_started = 1;
        r = cryptodev_vhost_start(vdev, queues);
        if (r < 0) {
            error_report("unable to start vhost crypto: %d: "
                         "falling back on userspace virtio", -r);
            c->vhost_started = 0;
        }
    } else {
        cryptodev_vhost_stop(vdev, queues);
        c->vhost_started = 0;
    }
}

static void virtio_crypto_set_status(VirtIODevice *vdev, uint8_t status)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);

    virtio_crypto_vhost_status(vcrypto, status);
}

static void virtio_crypto_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                           bool mask)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    int queue = virtio_crypto_vq2q(idx);

    assert(vcrypto->vhost_started);

    cryptodev_vhost_virtqueue_mask(vdev, queue, idx, mask);
}

static bool virtio_crypto_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    int queue = virtio_crypto_vq2q(idx);

    assert(vcrypto->vhost_started);

    return cryptodev_vhost_virtqueue_pending(vdev, queue, idx);
}

static struct vhost_dev *virtio_crypto_get_vhost(VirtIODevice *vdev)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    CryptoDevBackend *b = vcrypto->cryptodev;
    CryptoDevBackendClient *cc = b->conf.peers.ccs[0];
    CryptoDevBackendVhost *vhost_crypto = cryptodev_get_vhost(cc, b, 0);
    return &vhost_crypto->dev;
}

static void virtio_crypto_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_crypto_properties);
    dc->vmsd = &vmstate_virtio_crypto;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_crypto_device_realize;
    vdc->unrealize = virtio_crypto_device_unrealize;
    vdc->get_config = virtio_crypto_get_config;
    vdc->get_features = virtio_crypto_get_features;
    vdc->reset = virtio_crypto_reset;
    vdc->set_status = virtio_crypto_set_status;
    vdc->guest_notifier_mask = virtio_crypto_guest_notifier_mask;
    vdc->guest_notifier_pending = virtio_crypto_guest_notifier_pending;
    vdc->get_vhost = virtio_crypto_get_vhost;
}

static void virtio_crypto_instance_init(Object *obj)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(obj);

    /*
     * The default config_size is sizeof(struct virtio_crypto_config).
     * Can be overriden with virtio_crypto_set_config_size.
     */
    vcrypto->config_size = sizeof(struct virtio_crypto_config);
}

static const TypeInfo virtio_crypto_info = {
    .name = TYPE_VIRTIO_CRYPTO,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOCrypto),
    .instance_init = virtio_crypto_instance_init,
    .class_init = virtio_crypto_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_crypto_info);
}

type_init(virtio_register_types)
