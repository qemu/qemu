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

#ifndef QEMU_RNG_H
#define QEMU_RNG_H

#include "qemu/queue.h"
#include "qom/object.h"

#define TYPE_RNG_BACKEND "rng-backend"
#define RNG_BACKEND(obj) \
    OBJECT_CHECK(RngBackend, (obj), TYPE_RNG_BACKEND)
#define RNG_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RngBackendClass, (obj), TYPE_RNG_BACKEND)
#define RNG_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(RngBackendClass, (klass), TYPE_RNG_BACKEND)

#define TYPE_RNG_BUILTIN "rng-builtin"

typedef struct RngRequest RngRequest;
typedef struct RngBackendClass RngBackendClass;
typedef struct RngBackend RngBackend;

typedef void (EntropyReceiveFunc)(void *opaque,
                                  const void *data,
                                  size_t size);

struct RngRequest
{
    EntropyReceiveFunc *receive_entropy;
    uint8_t *data;
    void *opaque;
    size_t offset;
    size_t size;
    QSIMPLEQ_ENTRY(RngRequest) next;
};

struct RngBackendClass
{
    ObjectClass parent_class;

    void (*request_entropy)(RngBackend *s, RngRequest *req);

    void (*opened)(RngBackend *s, Error **errp);
};

struct RngBackend
{
    Object parent;

    /*< protected >*/
    bool opened;
    QSIMPLEQ_HEAD(, RngRequest) requests;
};


/**
 * rng_backend_request_entropy:
 * @s: the backend to request entropy from
 * @size: the number of bytes of data to request
 * @receive_entropy: a function to be invoked when entropy is available
 * @opaque: data that should be passed to @receive_entropy
 *
 * This function is used by the front-end to request entropy from an entropy
 * source.  This function can be called multiple times before @receive_entropy
 * is invoked with different values of @receive_entropy and @opaque.  The
 * backend will queue each request and handle appropriately.
 *
 * The backend does not need to pass the full amount of data to @receive_entropy
 * but will pass a value greater than 0.
 */
void rng_backend_request_entropy(RngBackend *s, size_t size,
                                 EntropyReceiveFunc *receive_entropy,
                                 void *opaque);

/**
 * rng_backend_free_request:
 * @s: the backend that created the request
 * @req: the request to finalize
 *
 * Used by child rng backend classes to finalize requests once they've been
 * processed. The request is removed from the list of active requests and
 * deleted.
 */
void rng_backend_finalize_request(RngBackend *s, RngRequest *req);
#endif
