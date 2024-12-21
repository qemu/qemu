/*
 * QEMU TPM Backend
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Stefan Berger   <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Based on backends/rng.c by Anthony Liguori
 */

#include "qemu/osdep.h"
#include "system/tpm_backend.h"
#include "qapi/error.h"
#include "system/tpm.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "block/thread-pool.h"
#include "qemu/error-report.h"

static void tpm_backend_request_completed(void *opaque, int ret)
{
    TPMBackend *s = TPM_BACKEND(opaque);
    TPMIfClass *tic = TPM_IF_GET_CLASS(s->tpmif);

    tic->request_completed(s->tpmif, ret);

    /* no need for atomic, as long the BQL is taken */
    s->cmd = NULL;
    object_unref(OBJECT(s));
}

static int tpm_backend_worker_thread(gpointer data)
{
    TPMBackend *s = TPM_BACKEND(data);
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);
    Error *err = NULL;

    k->handle_request(s, s->cmd, &err);
    if (err) {
        error_report_err(err);
        return -1;
    }

    return 0;
}

void tpm_backend_finish_sync(TPMBackend *s)
{
    while (s->cmd) {
        aio_poll(qemu_get_aio_context(), true);
    }
}

enum TpmType tpm_backend_get_type(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->type;
}

int tpm_backend_init(TPMBackend *s, TPMIf *tpmif, Error **errp)
{
    if (s->tpmif) {
        error_setg(errp, "TPM backend '%s' is already initialized", s->id);
        return -1;
    }

    s->tpmif = tpmif;
    object_ref(OBJECT(tpmif));

    s->had_startup_error = false;

    return 0;
}

int tpm_backend_startup_tpm(TPMBackend *s, size_t buffersize)
{
    int res = 0;
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    /* terminate a running TPM */
    tpm_backend_finish_sync(s);

    res = k->startup_tpm ? k->startup_tpm(s, buffersize) : 0;

    s->had_startup_error = (res != 0);

    return res;
}

bool tpm_backend_had_startup_error(TPMBackend *s)
{
    return s->had_startup_error;
}

void tpm_backend_deliver_request(TPMBackend *s, TPMBackendCmd *cmd)
{
    if (s->cmd != NULL) {
        error_report("There is a TPM request pending");
        return;
    }

    s->cmd = cmd;
    object_ref(OBJECT(s));
    thread_pool_submit_aio(tpm_backend_worker_thread, s,
                           tpm_backend_request_completed, s);
}

void tpm_backend_reset(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    if (k->reset) {
        k->reset(s);
    }

    tpm_backend_finish_sync(s);

    s->had_startup_error = false;
}

void tpm_backend_cancel_cmd(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    k->cancel_cmd(s);
}

bool tpm_backend_get_tpm_established_flag(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->get_tpm_established_flag ?
           k->get_tpm_established_flag(s) : false;
}

int tpm_backend_reset_tpm_established_flag(TPMBackend *s, uint8_t locty)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->reset_tpm_established_flag ?
           k->reset_tpm_established_flag(s, locty) : 0;
}

TPMVersion tpm_backend_get_tpm_version(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->get_tpm_version(s);
}

size_t tpm_backend_get_buffer_size(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->get_buffer_size(s);
}

TPMInfo *tpm_backend_query_tpm(TPMBackend *s)
{
    TPMInfo *info = g_new0(TPMInfo, 1);
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);
    TPMIfClass *tic = TPM_IF_GET_CLASS(s->tpmif);

    info->id = g_strdup(s->id);
    info->model = tic->model;
    info->options = k->get_tpm_options(s);

    return info;
}

static void tpm_backend_instance_finalize(Object *obj)
{
    TPMBackend *s = TPM_BACKEND(obj);

    object_unref(OBJECT(s->tpmif));
    g_free(s->id);
}

static const TypeInfo tpm_backend_info = {
    .name = TYPE_TPM_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(TPMBackend),
    .instance_finalize = tpm_backend_instance_finalize,
    .class_size = sizeof(TPMBackendClass),
    .abstract = true,
};

static const TypeInfo tpm_if_info = {
    .name = TYPE_TPM_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(TPMIfClass),
};

static void register_types(void)
{
    type_register_static(&tpm_backend_info);
    type_register_static(&tpm_if_info);
}

type_init(register_types);
