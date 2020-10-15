/*
 * QEMU Random Number Generator Backend
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/rng.h"
#include "chardev/char-fe.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_RNG_EGD "rng-egd"
OBJECT_DECLARE_SIMPLE_TYPE(RngEgd, RNG_EGD)

struct RngEgd {
    RngBackend parent;

    CharBackend chr;
    char *chr_name;
};

static void rng_egd_request_entropy(RngBackend *b, RngRequest *req)
{
    RngEgd *s = RNG_EGD(b);
    size_t size = req->size;

    while (size > 0) {
        uint8_t header[2];
        uint8_t len = MIN(size, 255);

        /* synchronous entropy request */
        header[0] = 0x02;
        header[1] = len;

        /* XXX this blocks entire thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks */
        qemu_chr_fe_write_all(&s->chr, header, sizeof(header));

        size -= len;
    }
}

static int rng_egd_chr_can_read(void *opaque)
{
    RngEgd *s = RNG_EGD(opaque);
    RngRequest *req;
    int size = 0;

    QSIMPLEQ_FOREACH(req, &s->parent.requests, next) {
        size += req->size - req->offset;
    }

    return size;
}

static void rng_egd_chr_read(void *opaque, const uint8_t *buf, int size)
{
    RngEgd *s = RNG_EGD(opaque);
    size_t buf_offset = 0;

    while (size > 0 && !QSIMPLEQ_EMPTY(&s->parent.requests)) {
        RngRequest *req = QSIMPLEQ_FIRST(&s->parent.requests);
        int len = MIN(size, req->size - req->offset);

        memcpy(req->data + req->offset, buf + buf_offset, len);
        buf_offset += len;
        req->offset += len;
        size -= len;

        if (req->offset == req->size) {
            req->receive_entropy(req->opaque, req->data, req->size);

            rng_backend_finalize_request(&s->parent, req);
        }
    }
}

static void rng_egd_opened(RngBackend *b, Error **errp)
{
    RngEgd *s = RNG_EGD(b);
    Chardev *chr;

    if (s->chr_name == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "chardev", "a valid character device");
        return;
    }

    chr = qemu_chr_find(s->chr_name);
    if (chr == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", s->chr_name);
        return;
    }
    if (!qemu_chr_fe_init(&s->chr, chr, errp)) {
        return;
    }

    /* FIXME we should resubmit pending requests when the CDS reconnects. */
    qemu_chr_fe_set_handlers(&s->chr, rng_egd_chr_can_read,
                             rng_egd_chr_read, NULL, NULL, s, NULL, true);
}

static void rng_egd_set_chardev(Object *obj, const char *value, Error **errp)
{
    RngBackend *b = RNG_BACKEND(obj);
    RngEgd *s = RNG_EGD(b);

    if (b->opened) {
        error_setg(errp, QERR_PERMISSION_DENIED);
    } else {
        g_free(s->chr_name);
        s->chr_name = g_strdup(value);
    }
}

static char *rng_egd_get_chardev(Object *obj, Error **errp)
{
    RngEgd *s = RNG_EGD(obj);
    Chardev *chr = qemu_chr_fe_get_driver(&s->chr);

    if (chr && chr->label) {
        return g_strdup(chr->label);
    }

    return NULL;
}

static void rng_egd_finalize(Object *obj)
{
    RngEgd *s = RNG_EGD(obj);

    qemu_chr_fe_deinit(&s->chr, false);
    g_free(s->chr_name);
}

static void rng_egd_class_init(ObjectClass *klass, void *data)
{
    RngBackendClass *rbc = RNG_BACKEND_CLASS(klass);

    rbc->request_entropy = rng_egd_request_entropy;
    rbc->opened = rng_egd_opened;
    object_class_property_add_str(klass, "chardev",
                                  rng_egd_get_chardev, rng_egd_set_chardev);
}

static const TypeInfo rng_egd_info = {
    .name = TYPE_RNG_EGD,
    .parent = TYPE_RNG_BACKEND,
    .instance_size = sizeof(RngEgd),
    .class_init = rng_egd_class_init,
    .instance_finalize = rng_egd_finalize,
};

static void register_types(void)
{
    type_register_static(&rng_egd_info);
}

type_init(register_types);
