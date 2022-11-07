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
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "hw/virtio/vhost-user.h"
#include "standard-headers/linux/virtio_crypto.h"
#include "sysemu/cryptodev-vhost.h"
#include "chardev/char-fe.h"
#include "sysemu/cryptodev-vhost-user.h"
#include "qom/object.h"


/**
 * @TYPE_CRYPTODEV_BACKEND_VHOST_USER:
 * name of backend that uses vhost user server
 */
#define TYPE_CRYPTODEV_BACKEND_VHOST_USER "cryptodev-vhost-user"

OBJECT_DECLARE_SIMPLE_TYPE(CryptoDevBackendVhostUser, CRYPTODEV_BACKEND_VHOST_USER)


struct CryptoDevBackendVhostUser {
    CryptoDevBackend parent_obj;

    VhostUserState vhost_user;
    CharBackend chr;
    char *chr_name;
    bool opened;
    CryptoDevBackendVhost *vhost_crypto[MAX_CRYPTO_QUEUE_NUM];
};

static int
cryptodev_vhost_user_running(
             CryptoDevBackendVhost *crypto)
{
    return crypto ? 1 : 0;
}

CryptoDevBackendVhost *
cryptodev_vhost_user_get_vhost(
                         CryptoDevBackendClient *cc,
                         CryptoDevBackend *b,
                         uint16_t queue)
{
    CryptoDevBackendVhostUser *s =
                      CRYPTODEV_BACKEND_VHOST_USER(b);
    assert(cc->type == CRYPTODEV_BACKEND_TYPE_VHOST_USER);
    assert(queue < MAX_CRYPTO_QUEUE_NUM);

    return s->vhost_crypto[queue];
}

static void cryptodev_vhost_user_stop(int queues,
                          CryptoDevBackendVhostUser *s)
{
    size_t i;

    for (i = 0; i < queues; i++) {
        if (!cryptodev_vhost_user_running(s->vhost_crypto[i])) {
            continue;
        }

        cryptodev_vhost_cleanup(s->vhost_crypto[i]);
        s->vhost_crypto[i] = NULL;
    }
}

static int
cryptodev_vhost_user_start(int queues,
                         CryptoDevBackendVhostUser *s)
{
    CryptoDevBackendVhostOptions options;
    CryptoDevBackend *b = CRYPTODEV_BACKEND(s);
    int max_queues;
    size_t i;

    for (i = 0; i < queues; i++) {
        if (cryptodev_vhost_user_running(s->vhost_crypto[i])) {
            continue;
        }

        options.opaque = &s->vhost_user;
        options.backend_type = VHOST_BACKEND_TYPE_USER;
        options.cc = b->conf.peers.ccs[i];
        s->vhost_crypto[i] = cryptodev_vhost_init(&options);
        if (!s->vhost_crypto[i]) {
            error_report("failed to init vhost_crypto for queue %zu", i);
            goto err;
        }

        if (i == 0) {
            max_queues =
              cryptodev_vhost_get_max_queues(s->vhost_crypto[i]);
            if (queues > max_queues) {
                error_report("you are asking more queues than supported: %d",
                             max_queues);
                goto err;
            }
        }
    }

    return 0;

err:
    cryptodev_vhost_user_stop(i + 1, s);
    return -1;
}

static Chardev *
cryptodev_vhost_claim_chardev(CryptoDevBackendVhostUser *s,
                                    Error **errp)
{
    Chardev *chr;

    if (s->chr_name == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "chardev", "a valid character device");
        return NULL;
    }

    chr = qemu_chr_find(s->chr_name);
    if (chr == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", s->chr_name);
        return NULL;
    }

    return chr;
}

static void cryptodev_vhost_user_event(void *opaque, QEMUChrEvent event)
{
    CryptoDevBackendVhostUser *s = opaque;
    CryptoDevBackend *b = CRYPTODEV_BACKEND(s);
    int queues = b->conf.peers.queues;

    assert(queues < MAX_CRYPTO_QUEUE_NUM);

    switch (event) {
    case CHR_EVENT_OPENED:
        if (cryptodev_vhost_user_start(queues, s) < 0) {
            exit(1);
        }
        b->ready = true;
        break;
    case CHR_EVENT_CLOSED:
        b->ready = false;
        cryptodev_vhost_user_stop(queues, s);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

static void cryptodev_vhost_user_init(
             CryptoDevBackend *backend, Error **errp)
{
    int queues = backend->conf.peers.queues;
    size_t i;
    Error *local_err = NULL;
    Chardev *chr;
    CryptoDevBackendClient *cc;
    CryptoDevBackendVhostUser *s =
                      CRYPTODEV_BACKEND_VHOST_USER(backend);

    chr = cryptodev_vhost_claim_chardev(s, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    s->opened = true;

    for (i = 0; i < queues; i++) {
        cc = cryptodev_backend_new_client(
                  "cryptodev-vhost-user", NULL);
        cc->info_str = g_strdup_printf("cryptodev-vhost-user%zu to %s ",
                                       i, chr->label);
        cc->queue_index = i;
        cc->type = CRYPTODEV_BACKEND_TYPE_VHOST_USER;

        backend->conf.peers.ccs[i] = cc;

        if (i == 0) {
            if (!qemu_chr_fe_init(&s->chr, chr, errp)) {
                return;
            }
        }
    }

    if (!vhost_user_init(&s->vhost_user, &s->chr, errp)) {
        return;
    }

    qemu_chr_fe_set_handlers(&s->chr, NULL, NULL,
                     cryptodev_vhost_user_event, NULL, s, NULL, true);

    backend->conf.crypto_services =
                         1u << VIRTIO_CRYPTO_SERVICE_CIPHER |
                         1u << VIRTIO_CRYPTO_SERVICE_HASH |
                         1u << VIRTIO_CRYPTO_SERVICE_MAC;
    backend->conf.cipher_algo_l = 1u << VIRTIO_CRYPTO_CIPHER_AES_CBC;
    backend->conf.hash_algo = 1u << VIRTIO_CRYPTO_HASH_SHA1;

    backend->conf.max_size = UINT64_MAX;
    backend->conf.max_cipher_key_len = VHOST_USER_MAX_CIPHER_KEY_LEN;
    backend->conf.max_auth_key_len = VHOST_USER_MAX_AUTH_KEY_LEN;
}

static int64_t cryptodev_vhost_user_sym_create_session(
           CryptoDevBackend *backend,
           CryptoDevBackendSymSessionInfo *sess_info,
           uint32_t queue_index, Error **errp)
{
    CryptoDevBackendClient *cc =
                   backend->conf.peers.ccs[queue_index];
    CryptoDevBackendVhost *vhost_crypto;
    uint64_t session_id = 0;
    int ret;

    vhost_crypto = cryptodev_vhost_user_get_vhost(cc, backend, queue_index);
    if (vhost_crypto) {
        struct vhost_dev *dev = &(vhost_crypto->dev);
        ret = dev->vhost_ops->vhost_crypto_create_session(dev,
                                                          sess_info,
                                                          &session_id);
        if (ret < 0) {
            return -1;
        } else {
            return session_id;
        }
    }
    return -1;
}

static int cryptodev_vhost_user_create_session(
           CryptoDevBackend *backend,
           CryptoDevBackendSessionInfo *sess_info,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque)
{
    uint32_t op_code = sess_info->op_code;
    CryptoDevBackendSymSessionInfo *sym_sess_info;
    int64_t ret;
    Error *local_error = NULL;
    int status;

    switch (op_code) {
    case VIRTIO_CRYPTO_CIPHER_CREATE_SESSION:
    case VIRTIO_CRYPTO_HASH_CREATE_SESSION:
    case VIRTIO_CRYPTO_MAC_CREATE_SESSION:
    case VIRTIO_CRYPTO_AEAD_CREATE_SESSION:
        sym_sess_info = &sess_info->u.sym_sess_info;
        ret = cryptodev_vhost_user_sym_create_session(backend, sym_sess_info,
                   queue_index, &local_error);
        break;

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

static int cryptodev_vhost_user_close_session(
           CryptoDevBackend *backend,
           uint64_t session_id,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque)
{
    CryptoDevBackendClient *cc =
                  backend->conf.peers.ccs[queue_index];
    CryptoDevBackendVhost *vhost_crypto;
    int ret = -1, status;

    vhost_crypto = cryptodev_vhost_user_get_vhost(cc, backend, queue_index);
    if (vhost_crypto) {
        struct vhost_dev *dev = &(vhost_crypto->dev);
        ret = dev->vhost_ops->vhost_crypto_close_session(dev,
                                                         session_id);
        if (ret < 0) {
            status = -VIRTIO_CRYPTO_ERR;
        } else {
            status = VIRTIO_CRYPTO_OK;
        }
    } else {
        status = -VIRTIO_CRYPTO_NOTSUPP;
    }
    if (cb) {
        cb(opaque, status);
    }
    return 0;
}

static void cryptodev_vhost_user_cleanup(
             CryptoDevBackend *backend,
             Error **errp)
{
    CryptoDevBackendVhostUser *s =
                      CRYPTODEV_BACKEND_VHOST_USER(backend);
    size_t i;
    int queues = backend->conf.peers.queues;
    CryptoDevBackendClient *cc;

    cryptodev_vhost_user_stop(queues, s);

    for (i = 0; i < queues; i++) {
        cc = backend->conf.peers.ccs[i];
        if (cc) {
            cryptodev_backend_free_client(cc);
            backend->conf.peers.ccs[i] = NULL;
        }
    }

    vhost_user_cleanup(&s->vhost_user);
}

static void cryptodev_vhost_user_set_chardev(Object *obj,
                                    const char *value, Error **errp)
{
    CryptoDevBackendVhostUser *s =
                      CRYPTODEV_BACKEND_VHOST_USER(obj);

    if (s->opened) {
        error_setg(errp, "Property 'chardev' can no longer be set");
    } else {
        g_free(s->chr_name);
        s->chr_name = g_strdup(value);
    }
}

static char *
cryptodev_vhost_user_get_chardev(Object *obj, Error **errp)
{
    CryptoDevBackendVhostUser *s =
                      CRYPTODEV_BACKEND_VHOST_USER(obj);
    Chardev *chr = qemu_chr_fe_get_driver(&s->chr);

    if (chr && chr->label) {
        return g_strdup(chr->label);
    }

    return NULL;
}

static void cryptodev_vhost_user_finalize(Object *obj)
{
    CryptoDevBackendVhostUser *s =
                      CRYPTODEV_BACKEND_VHOST_USER(obj);

    qemu_chr_fe_deinit(&s->chr, false);

    g_free(s->chr_name);
}

static void
cryptodev_vhost_user_class_init(ObjectClass *oc, void *data)
{
    CryptoDevBackendClass *bc = CRYPTODEV_BACKEND_CLASS(oc);

    bc->init = cryptodev_vhost_user_init;
    bc->cleanup = cryptodev_vhost_user_cleanup;
    bc->create_session = cryptodev_vhost_user_create_session;
    bc->close_session = cryptodev_vhost_user_close_session;
    bc->do_op = NULL;

    object_class_property_add_str(oc, "chardev",
                                  cryptodev_vhost_user_get_chardev,
                                  cryptodev_vhost_user_set_chardev);

}

static const TypeInfo cryptodev_vhost_user_info = {
    .name = TYPE_CRYPTODEV_BACKEND_VHOST_USER,
    .parent = TYPE_CRYPTODEV_BACKEND,
    .class_init = cryptodev_vhost_user_class_init,
    .instance_finalize = cryptodev_vhost_user_finalize,
    .instance_size = sizeof(CryptoDevBackendVhostUser),
};

static void
cryptodev_vhost_user_register_types(void)
{
    type_register_static(&cryptodev_vhost_user_info);
}

type_init(cryptodev_vhost_user_register_types);
