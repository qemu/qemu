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
#include "hw/qdev.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-crypto.h"
#include "hw/virtio/virtio-access.h"
#include "standard-headers/linux/virtio_ids.h"

#define VIRTIO_CRYPTO_VM_VERSION 1

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

static int64_t
virtio_crypto_create_sym_session(VirtIOCrypto *vcrypto,
               struct virtio_crypto_sym_create_session_req *sess_req,
               uint32_t queue_id,
               uint32_t opcode,
               struct iovec *iov, unsigned int out_num)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vcrypto);
    CryptoDevBackendSymSessionInfo info;
    int64_t session_id;
    int queue_index;
    uint32_t op_type;
    Error *local_err = NULL;
    int ret;

    memset(&info, 0, sizeof(info));
    op_type = ldl_le_p(&sess_req->op_type);
    info.op_type = op_type;
    info.op_code = opcode;

    if (op_type == VIRTIO_CRYPTO_SYM_OP_CIPHER) {
        ret = virtio_crypto_cipher_session_helper(vdev, &info,
                           &sess_req->u.cipher.para,
                           &iov, &out_num);
        if (ret < 0) {
            goto err;
        }
    } else if (op_type == VIRTIO_CRYPTO_SYM_OP_ALGORITHM_CHAINING) {
        size_t s;
        /* cipher part */
        ret = virtio_crypto_cipher_session_helper(vdev, &info,
                           &sess_req->u.chain.para.cipher_param,
                           &iov, &out_num);
        if (ret < 0) {
            goto err;
        }
        /* hash part */
        info.alg_chain_order = ldl_le_p(
                                     &sess_req->u.chain.para.alg_chain_order);
        info.add_len = ldl_le_p(&sess_req->u.chain.para.aad_len);
        info.hash_mode = ldl_le_p(&sess_req->u.chain.para.hash_mode);
        if (info.hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_AUTH) {
            info.hash_alg = ldl_le_p(&sess_req->u.chain.para.u.mac_param.algo);
            info.auth_key_len = ldl_le_p(
                             &sess_req->u.chain.para.u.mac_param.auth_key_len);
            info.hash_result_len = ldl_le_p(
                           &sess_req->u.chain.para.u.mac_param.hash_result_len);
            if (info.auth_key_len > vcrypto->conf.max_auth_key_len) {
                error_report("virtio-crypto length of auth key is too big: %u",
                             info.auth_key_len);
                ret = -VIRTIO_CRYPTO_ERR;
                goto err;
            }
            /* get auth key */
            if (info.auth_key_len > 0) {
                DPRINTF("auth_keylen=%" PRIu32 "\n", info.auth_key_len);
                info.auth_key = g_malloc(info.auth_key_len);
                s = iov_to_buf(iov, out_num, 0, info.auth_key,
                               info.auth_key_len);
                if (unlikely(s != info.auth_key_len)) {
                    virtio_error(vdev,
                          "virtio-crypto authenticated key incorrect");
                    ret = -EFAULT;
                    goto err;
                }
                iov_discard_front(&iov, &out_num, info.auth_key_len);
            }
        } else if (info.hash_mode == VIRTIO_CRYPTO_SYM_HASH_MODE_PLAIN) {
            info.hash_alg = ldl_le_p(
                             &sess_req->u.chain.para.u.hash_param.algo);
            info.hash_result_len = ldl_le_p(
                        &sess_req->u.chain.para.u.hash_param.hash_result_len);
        } else {
            /* VIRTIO_CRYPTO_SYM_HASH_MODE_NESTED */
            error_report("unsupported hash mode");
            ret = -VIRTIO_CRYPTO_NOTSUPP;
            goto err;
        }
    } else {
        /* VIRTIO_CRYPTO_SYM_OP_NONE */
        error_report("unsupported cipher op_type: VIRTIO_CRYPTO_SYM_OP_NONE");
        ret = -VIRTIO_CRYPTO_NOTSUPP;
        goto err;
    }

    queue_index = virtio_crypto_vq2q(queue_id);
    session_id = cryptodev_backend_sym_create_session(
                                     vcrypto->cryptodev,
                                     &info, queue_index, &local_err);
    if (session_id >= 0) {
        DPRINTF("create session_id=%" PRIu64 " successfully\n",
                session_id);

        ret = session_id;
    } else {
        if (local_err) {
            error_report_err(local_err);
        }
        ret = -VIRTIO_CRYPTO_ERR;
    }

err:
    g_free(info.cipher_key);
    g_free(info.auth_key);
    return ret;
}

static uint8_t
virtio_crypto_handle_close_session(VirtIOCrypto *vcrypto,
         struct virtio_crypto_destroy_session_req *close_sess_req,
         uint32_t queue_id)
{
    int ret;
    uint64_t session_id;
    uint32_t status;
    Error *local_err = NULL;

    session_id = ldq_le_p(&close_sess_req->session_id);
    DPRINTF("close session, id=%" PRIu64 "\n", session_id);

    ret = cryptodev_backend_sym_close_session(
              vcrypto->cryptodev, session_id, queue_id, &local_err);
    if (ret == 0) {
        status = VIRTIO_CRYPTO_OK;
    } else {
        if (local_err) {
            error_report_err(local_err);
        } else {
            error_report("destroy session failed");
        }
        status = VIRTIO_CRYPTO_ERR;
    }

    return status;
}

static void virtio_crypto_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_op_ctrl_req ctrl;
    VirtQueueElement *elem;
    struct iovec *in_iov;
    struct iovec *out_iov;
    unsigned in_num;
    unsigned out_num;
    uint32_t queue_id;
    uint32_t opcode;
    struct virtio_crypto_session_input input;
    int64_t session_id;
    uint8_t status;
    size_t s;

    for (;;) {
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
        out_iov = elem->out_sg;
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

        switch (opcode) {
        case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
            memset(&input, 0, sizeof(input));
            session_id = virtio_crypto_create_sym_session(vcrypto,
                             &ctrl.u.sym_create_session,
                             queue_id, opcode,
                             out_iov, out_num);
            /* Serious errors, need to reset virtio crypto device */
            if (session_id == -EFAULT) {
                virtqueue_detach_element(vq, elem, 0);
                break;
            } else if (session_id == -VIRTIO_CRYPTO_NOTSUPP) {
                stl_le_p(&input.status, VIRTIO_CRYPTO_NOTSUPP);
            } else if (session_id == -VIRTIO_CRYPTO_ERR) {
                stl_le_p(&input.status, VIRTIO_CRYPTO_ERR);
            } else {
                /* Set the session id */
                stq_le_p(&input.session_id, session_id);
                stl_le_p(&input.status, VIRTIO_CRYPTO_OK);
            }

            s = iov_from_buf(in_iov, in_num, 0, &input, sizeof(input));
            if (unlikely(s != sizeof(input))) {
                virtio_error(vdev, "virtio-crypto input incorrect");
                virtqueue_detach_element(vq, elem, 0);
                break;
            }
            virtqueue_push(vq, elem, sizeof(input));
            virtio_notify(vdev, vq);
            break;
        case VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION:
        case VIRTIO_CRYPTO_HASH_DESTROY_SESSION:
        case VIRTIO_CRYPTO_MAC_DESTROY_SESSION:
        case VIRTIO_CRYPTO_AEAD_DESTROY_SESSION:
            status = virtio_crypto_handle_close_session(vcrypto,
                   &ctrl.u.destroy_session, queue_id);
            /* The status only occupy one byte, we can directly use it */
            s = iov_from_buf(in_iov, in_num, 0, &status, sizeof(status));
            if (unlikely(s != sizeof(status))) {
                virtio_error(vdev, "virtio-crypto status incorrect");
                virtqueue_detach_element(vq, elem, 0);
                break;
            }
            virtqueue_push(vq, elem, sizeof(status));
            virtio_notify(vdev, vq);
            break;
        case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
        case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
        case VIRTIO_CRYPTO_AEAD_CREATE_SESSION:
        default:
            error_report("virtio-crypto unsupported ctrl opcode: %d", opcode);
            memset(&input, 0, sizeof(input));
            stl_le_p(&input.status, VIRTIO_CRYPTO_NOTSUPP);
            s = iov_from_buf(in_iov, in_num, 0, &input, sizeof(input));
            if (unlikely(s != sizeof(input))) {
                virtio_error(vdev, "virtio-crypto input incorrect");
                virtqueue_detach_element(vq, elem, 0);
                break;
            }
            virtqueue_push(vq, elem, sizeof(input));
            virtio_notify(vdev, vq);

            break;
        } /* end switch case */

        g_free(elem);
    } /* end for loop */
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
    if (!vcrypto->cryptodev->ready) {
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
    }

    vcrypto->max_queues = MAX(vcrypto->cryptodev->conf.peers.queues, 1);
    if (vcrypto->max_queues + 1 > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "Invalid number of queues (= %" PRIu32 "), "
                   "must be a postive integer less than %d.",
                   vcrypto->max_queues, VIRTIO_QUEUE_MAX);
        return;
    }

    virtio_init(vdev, "virtio-crypto", VIRTIO_ID_CRYPTO, vcrypto->config_size);
    vcrypto->curr_queues = 1;

    for (i = 0; i < vcrypto->max_queues; i++) {
        virtio_add_queue(vdev, 1024, NULL);
    }

    vcrypto->ctrl_vq = virtio_add_queue(vdev, 64, virtio_crypto_handle_ctrl);
    if (!vcrypto->cryptodev->ready) {
        vcrypto->status &= ~VIRTIO_CRYPTO_S_HW_READY;
    } else {
        vcrypto->status |= VIRTIO_CRYPTO_S_HW_READY;
    }

    virtio_crypto_init_config(vdev);
}

static void virtio_crypto_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    virtio_cleanup(vdev);
}

static const VMStateDescription vmstate_virtio_crypto = {
    .name = "virtio-crypto",
    .minimum_version_id = VIRTIO_CRYPTO_VM_VERSION,
    .version_id = VIRTIO_CRYPTO_VM_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_crypto_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_crypto_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIOCrypto *c = VIRTIO_CRYPTO(vdev);
    struct virtio_crypto_config crypto_cfg;

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

    memcpy(config, &crypto_cfg, c->config_size);
}

static void virtio_crypto_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_crypto_properties;
    dc->vmsd = &vmstate_virtio_crypto;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_crypto_device_realize;
    vdc->unrealize = virtio_crypto_device_unrealize;
    vdc->get_config = virtio_crypto_get_config;
    vdc->get_features = virtio_crypto_get_features;
    vdc->reset = virtio_crypto_reset;
}

static void virtio_crypto_instance_init(Object *obj)
{
    VirtIOCrypto *vcrypto = VIRTIO_CRYPTO(obj);

    /*
     * The default config_size is sizeof(struct virtio_crypto_config).
     * Can be overriden with virtio_crypto_set_config_size.
     */
    vcrypto->config_size = sizeof(struct virtio_crypto_config);

    object_property_add_link(obj, "cryptodev",
                             TYPE_CRYPTODEV_BACKEND,
                             (Object **)&vcrypto->conf.cryptodev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);
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
