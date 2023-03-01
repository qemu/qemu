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
#include "qapi/qapi-commands-cryptodev.h"
#include "qapi/visitor.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qom/object_interfaces.h"
#include "hw/virtio/virtio-crypto.h"


static QTAILQ_HEAD(, CryptoDevBackendClient) crypto_clients;

static int qmp_query_cryptodev_foreach(Object *obj, void *data)
{
    CryptoDevBackend *backend;
    QCryptodevInfoList **infolist = data;
    uint32_t services, i;

    if (!object_dynamic_cast(obj, TYPE_CRYPTODEV_BACKEND)) {
        return 0;
    }

    QCryptodevInfo *info = g_new0(QCryptodevInfo, 1);
    info->id = g_strdup(object_get_canonical_path_component(obj));

    backend = CRYPTODEV_BACKEND(obj);
    services = backend->conf.crypto_services;
    for (i = 0; i < QCRYPTODEV_BACKEND_SERVICE__MAX; i++) {
        if (services & (1 << i)) {
            QAPI_LIST_PREPEND(info->service, i);
        }
    }

    for (i = 0; i < backend->conf.peers.queues; i++) {
        CryptoDevBackendClient *cc = backend->conf.peers.ccs[i];
        QCryptodevBackendClient *client = g_new0(QCryptodevBackendClient, 1);

        client->queue = cc->queue_index;
        client->type = cc->type;
        QAPI_LIST_PREPEND(info->client, client);
    }

    QAPI_LIST_PREPEND(*infolist, info);

    return 0;
}

QCryptodevInfoList *qmp_query_cryptodev(Error **errp)
{
    QCryptodevInfoList *list = NULL;
    Object *objs = container_get(object_get_root(), "/objects");

    object_child_foreach(objs, qmp_query_cryptodev_foreach, &list);

    return list;
}

CryptoDevBackendClient *cryptodev_backend_new_client(void)
{
    CryptoDevBackendClient *cc;

    cc = g_new0(CryptoDevBackendClient, 1);
    QTAILQ_INSERT_TAIL(&crypto_clients, cc, next);

    return cc;
}

void cryptodev_backend_free_client(
                  CryptoDevBackendClient *cc)
{
    QTAILQ_REMOVE(&crypto_clients, cc, next);
    g_free(cc->info_str);
    g_free(cc);
}

void cryptodev_backend_cleanup(
             CryptoDevBackend *backend,
             Error **errp)
{
    CryptoDevBackendClass *bc =
                  CRYPTODEV_BACKEND_GET_CLASS(backend);

    if (bc->cleanup) {
        bc->cleanup(backend, errp);
    }

    g_free(backend->sym_stat);
    g_free(backend->asym_stat);
}

int cryptodev_backend_create_session(
           CryptoDevBackend *backend,
           CryptoDevBackendSessionInfo *sess_info,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque)
{
    CryptoDevBackendClass *bc =
                      CRYPTODEV_BACKEND_GET_CLASS(backend);

    if (bc->create_session) {
        return bc->create_session(backend, sess_info, queue_index, cb, opaque);
    }
    return -VIRTIO_CRYPTO_NOTSUPP;
}

int cryptodev_backend_close_session(
           CryptoDevBackend *backend,
           uint64_t session_id,
           uint32_t queue_index,
           CryptoDevCompletionFunc cb,
           void *opaque)
{
    CryptoDevBackendClass *bc =
                      CRYPTODEV_BACKEND_GET_CLASS(backend);

    if (bc->close_session) {
        return bc->close_session(backend, session_id, queue_index, cb, opaque);
    }
    return -VIRTIO_CRYPTO_NOTSUPP;
}

static int cryptodev_backend_operation(
                 CryptoDevBackend *backend,
                 CryptoDevBackendOpInfo *op_info)
{
    CryptoDevBackendClass *bc =
                      CRYPTODEV_BACKEND_GET_CLASS(backend);

    if (bc->do_op) {
        return bc->do_op(backend, op_info);
    }
    return -VIRTIO_CRYPTO_NOTSUPP;
}

static int cryptodev_backend_account(CryptoDevBackend *backend,
                 CryptoDevBackendOpInfo *op_info)
{
    enum QCryptodevBackendAlgType algtype = op_info->algtype;
    int len;

    if (algtype == QCRYPTODEV_BACKEND_ALG_ASYM) {
        CryptoDevBackendAsymOpInfo *asym_op_info = op_info->u.asym_op_info;
        len = asym_op_info->src_len;
        switch (op_info->op_code) {
        case VIRTIO_CRYPTO_AKCIPHER_ENCRYPT:
            CryptodevAsymStatIncEncrypt(backend, len);
            break;
        case VIRTIO_CRYPTO_AKCIPHER_DECRYPT:
            CryptodevAsymStatIncDecrypt(backend, len);
            break;
        case VIRTIO_CRYPTO_AKCIPHER_SIGN:
            CryptodevAsymStatIncSign(backend, len);
            break;
        case VIRTIO_CRYPTO_AKCIPHER_VERIFY:
            CryptodevAsymStatIncVerify(backend, len);
            break;
        default:
            return -VIRTIO_CRYPTO_NOTSUPP;
        }
    } else if (algtype == QCRYPTODEV_BACKEND_ALG_SYM) {
        CryptoDevBackendSymOpInfo *sym_op_info = op_info->u.sym_op_info;
        len = sym_op_info->src_len;
        switch (op_info->op_code) {
        case VIRTIO_CRYPTO_CIPHER_ENCRYPT:
            CryptodevSymStatIncEncrypt(backend, len);
            break;
        case VIRTIO_CRYPTO_CIPHER_DECRYPT:
            CryptodevSymStatIncDecrypt(backend, len);
            break;
        default:
            return -VIRTIO_CRYPTO_NOTSUPP;
        }
    } else {
        error_report("Unsupported cryptodev alg type: %" PRIu32 "", algtype);
        return -VIRTIO_CRYPTO_NOTSUPP;
    }

    return len;
}

static void cryptodev_backend_throttle_timer_cb(void *opaque)
{
    CryptoDevBackend *backend = (CryptoDevBackend *)opaque;
    CryptoDevBackendOpInfo *op_info, *tmpop;
    int ret;

    QTAILQ_FOREACH_SAFE(op_info, &backend->opinfos, next, tmpop) {
        QTAILQ_REMOVE(&backend->opinfos, op_info, next);
        ret = cryptodev_backend_account(backend, op_info);
        if (ret < 0) {
            op_info->cb(op_info->opaque, ret);
            continue;
        }

        throttle_account(&backend->ts, true, ret);
        cryptodev_backend_operation(backend, op_info);
        if (throttle_enabled(&backend->tc) &&
            throttle_schedule_timer(&backend->ts, &backend->tt, true)) {
            break;
        }
    }
}

int cryptodev_backend_crypto_operation(
                 CryptoDevBackend *backend,
                 CryptoDevBackendOpInfo *op_info)
{
    int ret;

    if (!throttle_enabled(&backend->tc)) {
        goto do_account;
    }

    if (throttle_schedule_timer(&backend->ts, &backend->tt, true) ||
        !QTAILQ_EMPTY(&backend->opinfos)) {
        QTAILQ_INSERT_TAIL(&backend->opinfos, op_info, next);
        return 0;
    }

do_account:
    ret = cryptodev_backend_account(backend, op_info);
    if (ret < 0) {
        return ret;
    }

    throttle_account(&backend->ts, true, ret);

    return cryptodev_backend_operation(backend, op_info);
}

static void
cryptodev_backend_get_queues(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);
    uint32_t value = backend->conf.peers.queues;

    visit_type_uint32(v, name, &value, errp);
}

static void
cryptodev_backend_set_queues(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (!value) {
        error_setg(errp, "Property '%s.%s' doesn't take value '%" PRIu32 "'",
                   object_get_typename(obj), name, value);
        return;
    }
    backend->conf.peers.queues = value;
}

static void cryptodev_backend_set_throttle(CryptoDevBackend *backend, int field,
                                           uint64_t value, Error **errp)
{
    uint64_t orig = backend->tc.buckets[field].avg;
    bool enabled = throttle_enabled(&backend->tc);

    if (orig == value) {
        return;
    }

    backend->tc.buckets[field].avg = value;
    if (!throttle_enabled(&backend->tc)) {
        throttle_timers_destroy(&backend->tt);
        cryptodev_backend_throttle_timer_cb(backend); /* drain opinfos */
        return;
    }

    if (!throttle_is_valid(&backend->tc, errp)) {
        backend->tc.buckets[field].avg = orig; /* revert change */
        return;
    }

    if (!enabled) {
        throttle_init(&backend->ts);
        throttle_timers_init(&backend->tt, qemu_get_aio_context(),
                             QEMU_CLOCK_REALTIME,
                             cryptodev_backend_throttle_timer_cb, /* FIXME */
                             cryptodev_backend_throttle_timer_cb, backend);
    }

    throttle_config(&backend->ts, QEMU_CLOCK_REALTIME, &backend->tc);
}

static void cryptodev_backend_get_bps(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);
    uint64_t value = backend->tc.buckets[THROTTLE_BPS_TOTAL].avg;

    visit_type_uint64(v, name, &value, errp);
}

static void cryptodev_backend_set_bps(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);
    uint64_t value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    cryptodev_backend_set_throttle(backend, THROTTLE_BPS_TOTAL, value, errp);
}

static void cryptodev_backend_get_ops(Object *obj, Visitor *v, const char *name,
                                      void *opaque, Error **errp)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);
    uint64_t value = backend->tc.buckets[THROTTLE_OPS_TOTAL].avg;

    visit_type_uint64(v, name, &value, errp);
}

static void cryptodev_backend_set_ops(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);
    uint64_t value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    cryptodev_backend_set_throttle(backend, THROTTLE_OPS_TOTAL, value, errp);
}

static void
cryptodev_backend_complete(UserCreatable *uc, Error **errp)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(uc);
    CryptoDevBackendClass *bc = CRYPTODEV_BACKEND_GET_CLASS(uc);
    uint32_t services;
    uint64_t value;

    QTAILQ_INIT(&backend->opinfos);
    value = backend->tc.buckets[THROTTLE_OPS_TOTAL].avg;
    cryptodev_backend_set_throttle(backend, THROTTLE_OPS_TOTAL, value, errp);
    value = backend->tc.buckets[THROTTLE_BPS_TOTAL].avg;
    cryptodev_backend_set_throttle(backend, THROTTLE_BPS_TOTAL, value, errp);

    if (bc->init) {
        bc->init(backend, errp);
    }

    services = backend->conf.crypto_services;
    if (services & (1 << QCRYPTODEV_BACKEND_SERVICE_CIPHER)) {
        backend->sym_stat = g_new0(CryptodevBackendSymStat, 1);
    }

    if (services & (1 << QCRYPTODEV_BACKEND_SERVICE_AKCIPHER)) {
        backend->asym_stat = g_new0(CryptodevBackendAsymStat, 1);
    }
}

void cryptodev_backend_set_used(CryptoDevBackend *backend, bool used)
{
    backend->is_used = used;
}

bool cryptodev_backend_is_used(CryptoDevBackend *backend)
{
    return backend->is_used;
}

void cryptodev_backend_set_ready(CryptoDevBackend *backend, bool ready)
{
    backend->ready = ready;
}

bool cryptodev_backend_is_ready(CryptoDevBackend *backend)
{
    return backend->ready;
}

static bool
cryptodev_backend_can_be_deleted(UserCreatable *uc)
{
    return !cryptodev_backend_is_used(CRYPTODEV_BACKEND(uc));
}

static void cryptodev_backend_instance_init(Object *obj)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);

    /* Initialize devices' queues property to 1 */
    object_property_set_int(obj, "queues", 1, NULL);

    throttle_config_init(&backend->tc);
}

static void cryptodev_backend_finalize(Object *obj)
{
    CryptoDevBackend *backend = CRYPTODEV_BACKEND(obj);

    cryptodev_backend_cleanup(backend, NULL);
    if (throttle_enabled(&backend->tc)) {
        throttle_timers_destroy(&backend->tt);
    }
}

static void
cryptodev_backend_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = cryptodev_backend_complete;
    ucc->can_be_deleted = cryptodev_backend_can_be_deleted;

    QTAILQ_INIT(&crypto_clients);
    object_class_property_add(oc, "queues", "uint32",
                              cryptodev_backend_get_queues,
                              cryptodev_backend_set_queues,
                              NULL, NULL);
    object_class_property_add(oc, "throttle-bps", "uint64",
                              cryptodev_backend_get_bps,
                              cryptodev_backend_set_bps,
                              NULL, NULL);
    object_class_property_add(oc, "throttle-ops", "uint64",
                              cryptodev_backend_get_ops,
                              cryptodev_backend_set_ops,
                              NULL, NULL);
}

static const TypeInfo cryptodev_backend_info = {
    .name = TYPE_CRYPTODEV_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(CryptoDevBackend),
    .instance_init = cryptodev_backend_instance_init,
    .instance_finalize = cryptodev_backend_finalize,
    .class_size = sizeof(CryptoDevBackendClass),
    .class_init = cryptodev_backend_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
cryptodev_backend_register_types(void)
{
    type_register_static(&cryptodev_backend_info);
}

type_init(cryptodev_backend_register_types);
