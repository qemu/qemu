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
#include "sysemu/tpm_backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/tpm.h"
#include "qemu/thread.h"

static void tpm_backend_worker_thread(gpointer data, gpointer user_data)
{
    TPMBackend *s = TPM_BACKEND(user_data);
    TPMBackendClass *k  = TPM_BACKEND_GET_CLASS(s);

    assert(k->handle_request != NULL);
    k->handle_request(s, (TPMBackendCmd)data);
}

static void tpm_backend_thread_end(TPMBackend *s)
{
    if (s->thread_pool) {
        g_thread_pool_push(s->thread_pool, (gpointer)TPM_BACKEND_CMD_END, NULL);
        g_thread_pool_free(s->thread_pool, FALSE, TRUE);
        s->thread_pool = NULL;
    }
}

enum TpmType tpm_backend_get_type(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->ops->type;
}

int tpm_backend_init(TPMBackend *s, TPMState *state,
                     TPMRecvDataCB *datacb)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    s->tpm_state = state;
    s->recv_data_callback = datacb;
    s->had_startup_error = false;

    return k->ops->init ? k->ops->init(s) : 0;
}

int tpm_backend_startup_tpm(TPMBackend *s)
{
    int res = 0;
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    /* terminate a running TPM */
    tpm_backend_thread_end(s);

    s->thread_pool = g_thread_pool_new(tpm_backend_worker_thread, s, 1, TRUE,
                                       NULL);
    g_thread_pool_push(s->thread_pool, (gpointer)TPM_BACKEND_CMD_INIT, NULL);

    res = k->ops->startup_tpm ? k->ops->startup_tpm(s) : 0;

    s->had_startup_error = (res != 0);

    return res;
}

bool tpm_backend_had_startup_error(TPMBackend *s)
{
    return s->had_startup_error;
}

size_t tpm_backend_realloc_buffer(TPMBackend *s, TPMSizedBuffer *sb)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    assert(k->ops->realloc_buffer);

    return k->ops->realloc_buffer(sb);
}

void tpm_backend_deliver_request(TPMBackend *s)
{
    g_thread_pool_push(s->thread_pool, (gpointer)TPM_BACKEND_CMD_PROCESS_CMD,
                       NULL);
}

void tpm_backend_reset(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    if (k->ops->reset) {
        k->ops->reset(s);
    }

    tpm_backend_thread_end(s);

    s->had_startup_error = false;
}

void tpm_backend_cancel_cmd(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    assert(k->ops->cancel_cmd);

    k->ops->cancel_cmd(s);
}

bool tpm_backend_get_tpm_established_flag(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->ops->get_tpm_established_flag ?
           k->ops->get_tpm_established_flag(s) : false;
}

int tpm_backend_reset_tpm_established_flag(TPMBackend *s, uint8_t locty)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    return k->ops->reset_tpm_established_flag ?
           k->ops->reset_tpm_established_flag(s, locty) : 0;
}

TPMVersion tpm_backend_get_tpm_version(TPMBackend *s)
{
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);

    assert(k->ops->get_tpm_version);

    return k->ops->get_tpm_version(s);
}

static bool tpm_backend_prop_get_opened(Object *obj, Error **errp)
{
    TPMBackend *s = TPM_BACKEND(obj);

    return s->opened;
}

void tpm_backend_open(TPMBackend *s, Error **errp)
{
    object_property_set_bool(OBJECT(s), true, "opened", errp);
}

static void tpm_backend_prop_set_opened(Object *obj, bool value, Error **errp)
{
    TPMBackend *s = TPM_BACKEND(obj);
    TPMBackendClass *k = TPM_BACKEND_GET_CLASS(s);
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

static void tpm_backend_instance_init(Object *obj)
{
    TPMBackend *s = TPM_BACKEND(obj);

    object_property_add_bool(obj, "opened",
                             tpm_backend_prop_get_opened,
                             tpm_backend_prop_set_opened,
                             NULL);
    s->fe_model = -1;
}

static void tpm_backend_instance_finalize(Object *obj)
{
    TPMBackend *s = TPM_BACKEND(obj);

    g_free(s->id);
    g_free(s->path);
    g_free(s->cancel_path);
    tpm_backend_thread_end(s);
}

static const TypeInfo tpm_backend_info = {
    .name = TYPE_TPM_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(TPMBackend),
    .instance_init = tpm_backend_instance_init,
    .instance_finalize = tpm_backend_instance_finalize,
    .class_size = sizeof(TPMBackendClass),
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&tpm_backend_info);
}

type_init(register_types);
