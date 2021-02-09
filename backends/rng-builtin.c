/*
 * QEMU Builtin Random Number Generator Backend
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/rng.h"
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "qom/object.h"
#include "sysemu/replay.h"

OBJECT_DECLARE_SIMPLE_TYPE(RngBuiltin, RNG_BUILTIN)

struct RngBuiltin {
    RngBackend parent;
    QEMUBH *bh;
};

static void rng_builtin_receive_entropy_bh(void *opaque)
{
    RngBuiltin *s = opaque;

    while (!QSIMPLEQ_EMPTY(&s->parent.requests)) {
        RngRequest *req = QSIMPLEQ_FIRST(&s->parent.requests);

        qemu_guest_getrandom_nofail(req->data, req->size);

        req->receive_entropy(req->opaque, req->data, req->size);

        rng_backend_finalize_request(&s->parent, req);
    }
}

static void rng_builtin_request_entropy(RngBackend *b, RngRequest *req)
{
    RngBuiltin *s = RNG_BUILTIN(b);

    replay_bh_schedule_event(s->bh);
}

static void rng_builtin_init(Object *obj)
{
    RngBuiltin *s = RNG_BUILTIN(obj);

    s->bh = qemu_bh_new(rng_builtin_receive_entropy_bh, s);
}

static void rng_builtin_finalize(Object *obj)
{
    RngBuiltin *s = RNG_BUILTIN(obj);

    qemu_bh_delete(s->bh);
}

static void rng_builtin_class_init(ObjectClass *klass, void *data)
{
    RngBackendClass *rbc = RNG_BACKEND_CLASS(klass);

    rbc->request_entropy = rng_builtin_request_entropy;
}

static const TypeInfo rng_builtin_info = {
    .name = TYPE_RNG_BUILTIN,
    .parent = TYPE_RNG_BACKEND,
    .instance_size = sizeof(RngBuiltin),
    .instance_init = rng_builtin_init,
    .instance_finalize = rng_builtin_finalize,
    .class_init = rng_builtin_class_init,
};

static void register_types(void)
{
    type_register_static(&rng_builtin_info);
}

type_init(register_types);
