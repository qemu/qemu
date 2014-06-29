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

#include "sysemu/rng-random.h"
#include "sysemu/rng.h"
#include "qapi/qmp/qerror.h"
#include "qemu/main-loop.h"

struct RndRandom
{
    RngBackend parent;

    int fd;
    char *filename;

    EntropyReceiveFunc *receive_func;
    void *opaque;
    size_t size;
};

/**
 * A simple and incomplete backend to request entropy from /dev/random.
 *
 * This backend exposes an additional "filename" property that can be used to
 * set the filename to use to open the backend.
 */

static void entropy_available(void *opaque)
{
    RndRandom *s = RNG_RANDOM(opaque);
    uint8_t buffer[s->size];
    ssize_t len;

    len = read(s->fd, buffer, s->size);
    if (len < 0 && errno == EAGAIN) {
        return;
    }
    g_assert(len != -1);

    s->receive_func(s->opaque, buffer, len);
    s->receive_func = NULL;

    qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
}

static void rng_random_request_entropy(RngBackend *b, size_t size,
                                        EntropyReceiveFunc *receive_entropy,
                                        void *opaque)
{
    RndRandom *s = RNG_RANDOM(b);

    if (s->receive_func) {
        s->receive_func(s->opaque, NULL, 0);
    }

    s->receive_func = receive_entropy;
    s->opaque = opaque;
    s->size = size;

    qemu_set_fd_handler(s->fd, entropy_available, NULL, s);
}

static void rng_random_opened(RngBackend *b, Error **errp)
{
    RndRandom *s = RNG_RANDOM(b);

    if (s->filename == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE,
                  "filename", "a valid filename");
    } else {
        s->fd = qemu_open(s->filename, O_RDONLY | O_NONBLOCK);
        if (s->fd == -1) {
            error_setg_file_open(errp, errno, s->filename);
        }
    }
}

static char *rng_random_get_filename(Object *obj, Error **errp)
{
    RndRandom *s = RNG_RANDOM(obj);

    if (s->filename) {
        return g_strdup(s->filename);
    }

    return NULL;
}

static void rng_random_set_filename(Object *obj, const char *filename,
                                 Error **errp)
{
    RngBackend *b = RNG_BACKEND(obj);
    RndRandom *s = RNG_RANDOM(obj);

    if (b->opened) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    g_free(s->filename);
    s->filename = g_strdup(filename);
}

static void rng_random_init(Object *obj)
{
    RndRandom *s = RNG_RANDOM(obj);

    object_property_add_str(obj, "filename",
                            rng_random_get_filename,
                            rng_random_set_filename,
                            NULL);

    s->filename = g_strdup("/dev/random");
    s->fd = -1;
}

static void rng_random_finalize(Object *obj)
{
    RndRandom *s = RNG_RANDOM(obj);

    if (s->fd != -1) {
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        qemu_close(s->fd);
    }

    g_free(s->filename);
}

static void rng_random_class_init(ObjectClass *klass, void *data)
{
    RngBackendClass *rbc = RNG_BACKEND_CLASS(klass);

    rbc->request_entropy = rng_random_request_entropy;
    rbc->opened = rng_random_opened;
}

static const TypeInfo rng_random_info = {
    .name = TYPE_RNG_RANDOM,
    .parent = TYPE_RNG_BACKEND,
    .instance_size = sizeof(RndRandom),
    .class_init = rng_random_class_init,
    .instance_init = rng_random_init,
    .instance_finalize = rng_random_finalize,
};

static void register_types(void)
{
    type_register_static(&rng_random_info);
}

type_init(register_types);
