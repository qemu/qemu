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
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"

void rng_backend_request_entropy(RngBackend *s, size_t size,
                                 EntropyReceiveFunc *receive_entropy,
                                 void *opaque)
{
    RngBackendClass *k = RNG_BACKEND_GET_CLASS(s);
    RngRequest *req;

    if (k->request_entropy) {
        req = g_malloc(sizeof(*req));

        req->offset = 0;
        req->size = size;
        req->receive_entropy = receive_entropy;
        req->opaque = opaque;
        req->data = g_malloc(req->size);

        k->request_entropy(s, req);

        QSIMPLEQ_INSERT_TAIL(&s->requests, req, next);
    }
}

static bool rng_backend_prop_get_opened(Object *obj, Error **errp)
{
    RngBackend *s = RNG_BACKEND(obj);

    return s->opened;
}

static void rng_backend_complete(UserCreatable *uc, Error **errp)
{
    object_property_set_bool(OBJECT(uc), "opened", true, errp);
}

static void rng_backend_prop_set_opened(Object *obj, bool value, Error **errp)
{
    RngBackend *s = RNG_BACKEND(obj);
    RngBackendClass *k = RNG_BACKEND_GET_CLASS(s);
    Error *local_err = NULL;

    if (value == s->opened) {
        return;
    }

    if (!value && s->opened) {
        error_setg(errp, QERR_PERMISSION_DENIED);
        return;
    }

    if (k->opened) {
        k->opened(s, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    s->opened = true;
}

static void rng_backend_free_request(RngRequest *req)
{
    g_free(req->data);
    g_free(req);
}

static void rng_backend_free_requests(RngBackend *s)
{
    RngRequest *req, *next;

    QSIMPLEQ_FOREACH_SAFE(req, &s->requests, next, next) {
        rng_backend_free_request(req);
    }

    QSIMPLEQ_INIT(&s->requests);
}

void rng_backend_finalize_request(RngBackend *s, RngRequest *req)
{
    QSIMPLEQ_REMOVE(&s->requests, req, RngRequest, next);
    rng_backend_free_request(req);
}

static void rng_backend_init(Object *obj)
{
    RngBackend *s = RNG_BACKEND(obj);

    QSIMPLEQ_INIT(&s->requests);

    object_property_add_bool(obj, "opened",
                             rng_backend_prop_get_opened,
                             rng_backend_prop_set_opened);
}

static void rng_backend_finalize(Object *obj)
{
    RngBackend *s = RNG_BACKEND(obj);

    rng_backend_free_requests(s);
}

static void rng_backend_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = rng_backend_complete;
}

static const TypeInfo rng_backend_info = {
    .name = TYPE_RNG_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(RngBackend),
    .instance_init = rng_backend_init,
    .instance_finalize = rng_backend_finalize,
    .class_size = sizeof(RngBackendClass),
    .class_init = rng_backend_class_init,
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&rng_backend_info);
}

type_init(register_types);
