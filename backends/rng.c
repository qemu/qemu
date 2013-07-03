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

#include "sysemu/rng.h"
#include "qapi/qmp/qerror.h"

void rng_backend_request_entropy(RngBackend *s, size_t size,
                                 EntropyReceiveFunc *receive_entropy,
                                 void *opaque)
{
    RngBackendClass *k = RNG_BACKEND_GET_CLASS(s);

    if (k->request_entropy) {
        k->request_entropy(s, size, receive_entropy, opaque);
    }
}

void rng_backend_cancel_requests(RngBackend *s)
{
    RngBackendClass *k = RNG_BACKEND_GET_CLASS(s);

    if (k->cancel_requests) {
        k->cancel_requests(s);
    }
}

static bool rng_backend_prop_get_opened(Object *obj, Error **errp)
{
    RngBackend *s = RNG_BACKEND(obj);

    return s->opened;
}

void rng_backend_open(RngBackend *s, Error **errp)
{
    object_property_set_bool(OBJECT(s), true, "opened", errp);
}

static void rng_backend_prop_set_opened(Object *obj, bool value, Error **errp)
{
    RngBackend *s = RNG_BACKEND(obj);
    RngBackendClass *k = RNG_BACKEND_GET_CLASS(s);

    if (value == s->opened) {
        return;
    }

    if (!value && s->opened) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    if (k->opened) {
        k->opened(s, errp);
    }

    if (!error_is_set(errp)) {
        s->opened = value;
    }
}

static void rng_backend_init(Object *obj)
{
    object_property_add_bool(obj, "opened",
                             rng_backend_prop_get_opened,
                             rng_backend_prop_set_opened,
                             NULL);
}

static const TypeInfo rng_backend_info = {
    .name = TYPE_RNG_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(RngBackend),
    .instance_init = rng_backend_init,
    .class_size = sizeof(RngBackendClass),
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&rng_backend_info);
}

type_init(register_types);
