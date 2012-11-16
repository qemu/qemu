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

#include "qemu/rng.h"
#include "qemu-char.h"
#include "qerror.h"
#include "hw/qdev.h" /* just for DEFINE_PROP_CHR */

#define TYPE_RNG_EGD "rng-egd"
#define RNG_EGD(obj) OBJECT_CHECK(RngEgd, (obj), TYPE_RNG_EGD)

typedef struct RngEgd
{
    RngBackend parent;

    CharDriverState *chr;
    char *chr_name;

    GSList *requests;
} RngEgd;

typedef struct RngRequest
{
    EntropyReceiveFunc *receive_entropy;
    uint8_t *data;
    void *opaque;
    size_t offset;
    size_t size;
} RngRequest;

static void rng_egd_request_entropy(RngBackend *b, size_t size,
                                    EntropyReceiveFunc *receive_entropy,
                                    void *opaque)
{
    RngEgd *s = RNG_EGD(b);
    RngRequest *req;

    req = g_malloc(sizeof(*req));

    req->offset = 0;
    req->size = size;
    req->receive_entropy = receive_entropy;
    req->opaque = opaque;
    req->data = g_malloc(req->size);

    while (size > 0) {
        uint8_t header[2];
        uint8_t len = MIN(size, 255);

        /* synchronous entropy request */
        header[0] = 0x02;
        header[1] = len;

        qemu_chr_fe_write(s->chr, header, sizeof(header));

        size -= len;
    }

    s->requests = g_slist_append(s->requests, req);
}

static void rng_egd_free_request(RngRequest *req)
{
    g_free(req->data);
    g_free(req);
}

static int rng_egd_chr_can_read(void *opaque)
{
    RngEgd *s = RNG_EGD(opaque);
    GSList *i;
    int size = 0;

    for (i = s->requests; i; i = i->next) {
        RngRequest *req = i->data;
        size += req->size - req->offset;
    }

    return size;
}

static void rng_egd_chr_read(void *opaque, const uint8_t *buf, int size)
{
    RngEgd *s = RNG_EGD(opaque);

    while (size > 0 && s->requests) {
        RngRequest *req = s->requests->data;
        int len = MIN(size, req->size - req->offset);

        memcpy(req->data + req->offset, buf, len);
        req->offset += len;
        size -= len;

        if (req->offset == req->size) {
            s->requests = g_slist_remove_link(s->requests, s->requests);

            req->receive_entropy(req->opaque, req->data, req->size);

            rng_egd_free_request(req);
        }
    }
}

static void rng_egd_free_requests(RngEgd *s)
{
    GSList *i;

    for (i = s->requests; i; i = i->next) {
        rng_egd_free_request(i->data);
    }

    g_slist_free(s->requests);
    s->requests = NULL;
}

static void rng_egd_cancel_requests(RngBackend *b)
{
    RngEgd *s = RNG_EGD(b);

    /* We simply delete the list of pending requests.  If there is data in the 
     * queue waiting to be read, this is okay, because there will always be
     * more data than we requested originally
     */
    rng_egd_free_requests(s);
}

static void rng_egd_opened(RngBackend *b, Error **errp)
{
    RngEgd *s = RNG_EGD(b);

    if (s->chr_name == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE,
                  "chardev", "a valid character device");
        return;
    }

    s->chr = qemu_chr_find(s->chr_name);
    if (s->chr == NULL) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, s->chr_name);
        return;
    }

    /* FIXME we should resubmit pending requests when the CDS reconnects. */
    qemu_chr_add_handlers(s->chr, rng_egd_chr_can_read, rng_egd_chr_read,
                          NULL, s);
}

static void rng_egd_set_chardev(Object *obj, const char *value, Error **errp)
{
    RngBackend *b = RNG_BACKEND(obj);
    RngEgd *s = RNG_EGD(b);

    if (b->opened) {
        error_set(errp, QERR_PERMISSION_DENIED);
    } else {
        g_free(s->chr_name);
        s->chr_name = g_strdup(value);
    }
}

static char *rng_egd_get_chardev(Object *obj, Error **errp)
{
    RngEgd *s = RNG_EGD(obj);

    if (s->chr && s->chr->label) {
        return g_strdup(s->chr->label);
    }

    return NULL;
}

static void rng_egd_init(Object *obj)
{
    object_property_add_str(obj, "chardev",
                            rng_egd_get_chardev, rng_egd_set_chardev,
                            NULL);
}

static void rng_egd_finalize(Object *obj)
{
    RngEgd *s = RNG_EGD(obj);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, NULL, NULL, NULL, NULL);
    }

    g_free(s->chr_name);

    rng_egd_free_requests(s);
}

static void rng_egd_class_init(ObjectClass *klass, void *data)
{
    RngBackendClass *rbc = RNG_BACKEND_CLASS(klass);

    rbc->request_entropy = rng_egd_request_entropy;
    rbc->cancel_requests = rng_egd_cancel_requests;
    rbc->opened = rng_egd_opened;
}

static TypeInfo rng_egd_info = {
    .name = TYPE_RNG_EGD,
    .parent = TYPE_RNG_BACKEND,
    .instance_size = sizeof(RngEgd),
    .class_init = rng_egd_class_init,
    .instance_init = rng_egd_init,
    .instance_finalize = rng_egd_finalize,
};

static void register_types(void)
{
    type_register_static(&rng_egd_info);
}

type_init(register_types);
