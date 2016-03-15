/*
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 FUJITSU LIMITED
 * Copyright (c) 2016 Intel Corporation
 *
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "net/filter.h"
#include "net/net.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qapi-visit.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "sysemu/char.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"

#define FILTER_MIRROR(obj) \
    OBJECT_CHECK(MirrorState, (obj), TYPE_FILTER_MIRROR)

#define TYPE_FILTER_MIRROR "filter-mirror"

typedef struct MirrorState {
    NetFilterState parent_obj;
    char *outdev;
    CharDriverState *chr_out;
} MirrorState;

static int filter_mirror_send(CharDriverState *chr_out,
                              const struct iovec *iov,
                              int iovcnt)
{
    int ret = 0;
    ssize_t size = 0;
    uint32_t len =  0;
    char *buf;

    size = iov_size(iov, iovcnt);
    if (!size) {
        return 0;
    }

    len = htonl(size);
    ret = qemu_chr_fe_write_all(chr_out, (uint8_t *)&len, sizeof(len));
    if (ret != sizeof(len)) {
        goto err;
    }

    buf = g_malloc(size);
    iov_to_buf(iov, iovcnt, 0, buf, size);
    ret = qemu_chr_fe_write_all(chr_out, (uint8_t *)buf, size);
    g_free(buf);
    if (ret != size) {
        goto err;
    }

    return 0;

err:
    return ret < 0 ? ret : -EIO;
}

static ssize_t filter_mirror_receive_iov(NetFilterState *nf,
                                         NetClientState *sender,
                                         unsigned flags,
                                         const struct iovec *iov,
                                         int iovcnt,
                                         NetPacketSent *sent_cb)
{
    MirrorState *s = FILTER_MIRROR(nf);
    int ret;

    ret = filter_mirror_send(s->chr_out, iov, iovcnt);
    if (ret) {
        error_report("filter_mirror_send failed(%s)", strerror(-ret));
    }

    /*
     * we don't hope this error interrupt the normal
     * path of net packet, so we always return zero.
     */
    return 0;
}

static void filter_mirror_cleanup(NetFilterState *nf)
{
    MirrorState *s = FILTER_MIRROR(nf);

    if (s->chr_out) {
        qemu_chr_fe_release(s->chr_out);
    }
}

static void filter_mirror_setup(NetFilterState *nf, Error **errp)
{
    MirrorState *s = FILTER_MIRROR(nf);

    if (!s->outdev) {
        error_setg(errp, "filter filter mirror needs 'outdev' "
                "property set");
        return;
    }

    s->chr_out = qemu_chr_find(s->outdev);
    if (s->chr_out == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", s->outdev);
        return;
    }

    if (qemu_chr_fe_claim(s->chr_out) != 0) {
        error_setg(errp, QERR_DEVICE_IN_USE, s->outdev);
        return;
    }
}

static void filter_mirror_class_init(ObjectClass *oc, void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);

    nfc->setup = filter_mirror_setup;
    nfc->cleanup = filter_mirror_cleanup;
    nfc->receive_iov = filter_mirror_receive_iov;
}

static char *filter_mirror_get_outdev(Object *obj, Error **errp)
{
    MirrorState *s = FILTER_MIRROR(obj);

    return g_strdup(s->outdev);
}

static void
filter_mirror_set_outdev(Object *obj, const char *value, Error **errp)
{
    MirrorState *s = FILTER_MIRROR(obj);

    g_free(s->outdev);
    s->outdev = g_strdup(value);
    if (!s->outdev) {
        error_setg(errp, "filter filter mirror needs 'outdev' "
                "property set");
        return;
    }
}

static void filter_mirror_init(Object *obj)
{
    object_property_add_str(obj, "outdev", filter_mirror_get_outdev,
                            filter_mirror_set_outdev, NULL);
}

static void filter_mirror_fini(Object *obj)
{
    MirrorState *s = FILTER_MIRROR(obj);

    g_free(s->outdev);
}

static const TypeInfo filter_mirror_info = {
    .name = TYPE_FILTER_MIRROR,
    .parent = TYPE_NETFILTER,
    .class_init = filter_mirror_class_init,
    .instance_init = filter_mirror_init,
    .instance_finalize = filter_mirror_fini,
    .instance_size = sizeof(MirrorState),
};

static void register_types(void)
{
    type_register_static(&filter_mirror_info);
}

type_init(register_types);
