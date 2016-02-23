/*
 * QEMU TPM Backend
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Stefan Berger  <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef _QEMU_TPM_H
#define _QEMU_TPM_H

#include "qom/object.h"
#include "qemu-common.h"
#include "qapi-types.h"
#include "qemu/option.h"
#include "sysemu/tpm.h"

#define TYPE_TPM_BACKEND "tpm-backend"
#define TPM_BACKEND(obj) \
    OBJECT_CHECK(TPMBackend, (obj), TYPE_TPM_BACKEND)
#define TPM_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(TPMBackendClass, (obj), TYPE_TPM_BACKEND)
#define TPM_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(TPMBackendClass, (klass), TYPE_TPM_BACKEND)

typedef struct TPMBackendClass TPMBackendClass;
typedef struct TPMBackend TPMBackend;

typedef struct TPMDriverOps TPMDriverOps;

struct TPMBackendClass {
    ObjectClass parent_class;

    const TPMDriverOps *ops;

    void (*opened)(TPMBackend *s, Error **errp);
};

struct TPMBackend {
    Object parent;

    /*< protected >*/
    bool opened;

    char *id;
    enum TpmModel fe_model;
    char *path;
    char *cancel_path;
    const TPMDriverOps *ops;

    QLIST_ENTRY(TPMBackend) list;
};

typedef void (TPMRecvDataCB)(TPMState *, uint8_t locty, bool selftest_done);

typedef struct TPMSizedBuffer {
    uint32_t size;
    uint8_t  *buffer;
} TPMSizedBuffer;

struct TPMDriverOps {
    enum TpmType type;
    const QemuOptDesc *opts;
    /* get a descriptive text of the backend to display to the user */
    const char *(*desc)(void);

    TPMBackend *(*create)(QemuOpts *opts, const char *id);
    void (*destroy)(TPMBackend *t);

    /* initialize the backend */
    int (*init)(TPMBackend *t, TPMState *s, TPMRecvDataCB *datacb);
    /* start up the TPM on the backend */
    int (*startup_tpm)(TPMBackend *t);
    /* returns true if nothing will ever answer TPM requests */
    bool (*had_startup_error)(TPMBackend *t);

    size_t (*realloc_buffer)(TPMSizedBuffer *sb);

    void (*deliver_request)(TPMBackend *t);

    void (*reset)(TPMBackend *t);

    void (*cancel_cmd)(TPMBackend *t);

    bool (*get_tpm_established_flag)(TPMBackend *t);

    int (*reset_tpm_established_flag)(TPMBackend *t, uint8_t locty);

    TPMVersion (*get_tpm_version)(TPMBackend *t);
};


/**
 * tpm_backend_get_type:
 * @s: the backend
 *
 * Returns the TpmType of the backend.
 */
enum TpmType tpm_backend_get_type(TPMBackend *s);

/**
 * tpm_backend_get_desc:
 * @s: the backend
 *
 * Returns a human readable description of the backend.
 */
const char *tpm_backend_get_desc(TPMBackend *s);

/**
 * tpm_backend_destroy:
 * @s: the backend to destroy
 */
void tpm_backend_destroy(TPMBackend *s);

/**
 * tpm_backend_init:
 * @s: the backend to initialized
 * @state: TPMState
 * @datacb: callback for sending data to frontend
 *
 * Initialize the backend with the given variables.
 *
 * Returns 0 on success.
 */
int tpm_backend_init(TPMBackend *s, TPMState *state,
                     TPMRecvDataCB *datacb);

/**
 * tpm_backend_startup_tpm:
 * @s: the backend whose TPM support is to be started
 *
 * Returns 0 on success.
 */
int tpm_backend_startup_tpm(TPMBackend *s);

/**
 * tpm_backend_had_startup_error:
 * @s: the backend to query for a statup error
 *
 * Check whether the backend had an error during startup. Returns
 * false if no error occurred and the backend can be used, true
 * otherwise.
 */
bool tpm_backend_had_startup_error(TPMBackend *s);

/**
 * tpm_backend_realloc_buffer:
 * @s: the backend
 * @sb: the TPMSizedBuffer to re-allocated to the size suitable for the
 *      backend.
 *
 * This function returns the size of the allocated buffer
 */
size_t tpm_backend_realloc_buffer(TPMBackend *s, TPMSizedBuffer *sb);

/**
 * tpm_backend_deliver_request:
 * @s: the backend to send the request to
 *
 * Send a request to the backend. The backend will then send the request
 * to the TPM implementation.
 */
void tpm_backend_deliver_request(TPMBackend *s);

/**
 * tpm_backend_reset:
 * @s: the backend to reset
 *
 * Reset the backend into a well defined state with all previous errors
 * reset.
 */
void tpm_backend_reset(TPMBackend *s);

/**
 * tpm_backend_cancel_cmd:
 * @s: the backend
 *
 * Cancel any ongoing command being processed by the TPM implementation
 * on behalf of the QEMU guest.
 */
void tpm_backend_cancel_cmd(TPMBackend *s);

/**
 * tpm_backend_get_tpm_established_flag:
 * @s: the backend
 *
 * Get the TPM establishment flag. This function may be called very
 * frequently by the frontend since for example in the TIS implementation
 * this flag is part of a register.
 */
bool tpm_backend_get_tpm_established_flag(TPMBackend *s);

/**
 * tpm_backend_reset_tpm_established_flag:
 * @s: the backend
 * @locty: the locality number
 *
 * Reset the TPM establishment flag.
 */
int tpm_backend_reset_tpm_established_flag(TPMBackend *s, uint8_t locty);

/**
 * tpm_backend_open:
 * @s: the backend to open
 * @errp: a pointer to return the #Error object if an error occurs.
 *
 * This function will open the backend if it is not already open.  Calling this
 * function on an already opened backend will not result in an error.
 */
void tpm_backend_open(TPMBackend *s, Error **errp);

/**
 * tpm_backend_get_tpm_version:
 * @s: the backend to call into
 *
 * Get the TPM Version that is emulated at the backend.
 *
 * Returns TPMVersion.
 */
TPMVersion tpm_backend_get_tpm_version(TPMBackend *s);

TPMBackend *qemu_find_tpm(const char *id);

const TPMDriverOps *tpm_get_backend_driver(const char *type);
int tpm_register_model(enum TpmModel model);
int tpm_register_driver(const TPMDriverOps *tdo);

#endif
