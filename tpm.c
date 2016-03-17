/*
 * TPM configuration
 *
 * Copyright (C) 2011-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Based on net.c
 */
#include "qemu/osdep.h"

#include "qapi/qmp/qerror.h"
#include "sysemu/tpm_backend.h"
#include "sysemu/tpm.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qmp-commands.h"

static QLIST_HEAD(, TPMBackend) tpm_backends =
    QLIST_HEAD_INITIALIZER(tpm_backends);


#define TPM_MAX_MODELS      1
#define TPM_MAX_DRIVERS     1

static TPMDriverOps const *be_drivers[TPM_MAX_DRIVERS] = {
    NULL,
};

static enum TpmModel tpm_models[TPM_MAX_MODELS] = {
    TPM_MODEL__MAX,
};

int tpm_register_model(enum TpmModel model)
{
    int i;

    for (i = 0; i < TPM_MAX_MODELS; i++) {
        if (tpm_models[i] == TPM_MODEL__MAX) {
            tpm_models[i] = model;
            return 0;
        }
    }
    error_report("Could not register TPM model");
    return 1;
}

static bool tpm_model_is_registered(enum TpmModel model)
{
    int i;

    for (i = 0; i < TPM_MAX_MODELS; i++) {
        if (tpm_models[i] == model) {
            return true;
        }
    }
    return false;
}

const TPMDriverOps *tpm_get_backend_driver(const char *type)
{
    int i;

    for (i = 0; i < TPM_MAX_DRIVERS && be_drivers[i] != NULL; i++) {
        if (!strcmp(TpmType_lookup[be_drivers[i]->type], type)) {
            return be_drivers[i];
        }
    }

    return NULL;
}

#ifdef CONFIG_TPM

int tpm_register_driver(const TPMDriverOps *tdo)
{
    int i;

    for (i = 0; i < TPM_MAX_DRIVERS; i++) {
        if (!be_drivers[i]) {
            be_drivers[i] = tdo;
            return 0;
        }
    }
    error_report("Could not register TPM driver");
    return 1;
}

/*
 * Walk the list of available TPM backend drivers and display them on the
 * screen.
 */
static void tpm_display_backend_drivers(void)
{
    int i;

    fprintf(stderr, "Supported TPM types (choose only one):\n");

    for (i = 0; i < TPM_MAX_DRIVERS && be_drivers[i] != NULL; i++) {
        fprintf(stderr, "%12s   %s\n",
                TpmType_lookup[be_drivers[i]->type], be_drivers[i]->desc());
    }
    fprintf(stderr, "\n");
}

/*
 * Find the TPM with the given Id
 */
TPMBackend *qemu_find_tpm(const char *id)
{
    TPMBackend *drv;

    if (id) {
        QLIST_FOREACH(drv, &tpm_backends, list) {
            if (!strcmp(drv->id, id)) {
                return drv;
            }
        }
    }

    return NULL;
}

static int configure_tpm(QemuOpts *opts)
{
    const char *value;
    const char *id;
    const TPMDriverOps *be;
    TPMBackend *drv;
    Error *local_err = NULL;

    if (!QLIST_EMPTY(&tpm_backends)) {
        error_report("Only one TPM is allowed.");
        return 1;
    }

    id = qemu_opts_id(opts);
    if (id == NULL) {
        error_report(QERR_MISSING_PARAMETER, "id");
        return 1;
    }

    value = qemu_opt_get(opts, "type");
    if (!value) {
        error_report(QERR_MISSING_PARAMETER, "type");
        tpm_display_backend_drivers();
        return 1;
    }

    be = tpm_get_backend_driver(value);
    if (be == NULL) {
        error_report(QERR_INVALID_PARAMETER_VALUE,
                     "type", "a TPM backend type");
        tpm_display_backend_drivers();
        return 1;
    }

    /* validate backend specific opts */
    qemu_opts_validate(opts, be->opts, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return 1;
    }

    drv = be->create(opts, id);
    if (!drv) {
        return 1;
    }

    tpm_backend_open(drv, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return 1;
    }

    QLIST_INSERT_HEAD(&tpm_backends, drv, list);

    return 0;
}

static int tpm_init_tpmdev(void *dummy, QemuOpts *opts, Error **errp)
{
    return configure_tpm(opts);
}

/*
 * Walk the list of TPM backend drivers that are in use and call their
 * destroy function to have them cleaned up.
 */
void tpm_cleanup(void)
{
    TPMBackend *drv, *next;

    QLIST_FOREACH_SAFE(drv, &tpm_backends, list, next) {
        QLIST_REMOVE(drv, list);
        tpm_backend_destroy(drv);
    }
}

/*
 * Initialize the TPM. Process the tpmdev command line options describing the
 * TPM backend.
 */
int tpm_init(void)
{
    if (qemu_opts_foreach(qemu_find_opts("tpmdev"),
                          tpm_init_tpmdev, NULL, NULL)) {
        return -1;
    }

    atexit(tpm_cleanup);
    return 0;
}

/*
 * Parse the TPM configuration options.
 * To display all available TPM backends the user may use '-tpmdev help'
 */
int tpm_config_parse(QemuOptsList *opts_list, const char *optarg)
{
    QemuOpts *opts;

    if (!strcmp(optarg, "help")) {
        tpm_display_backend_drivers();
        return -1;
    }
    opts = qemu_opts_parse_noisily(opts_list, optarg, true);
    if (!opts) {
        return -1;
    }
    return 0;
}

#endif /* CONFIG_TPM */

static const TPMDriverOps *tpm_driver_find_by_type(enum TpmType type)
{
    int i;

    for (i = 0; i < TPM_MAX_DRIVERS && be_drivers[i] != NULL; i++) {
        if (be_drivers[i]->type == type) {
            return be_drivers[i];
        }
    }
    return NULL;
}

static TPMInfo *qmp_query_tpm_inst(TPMBackend *drv)
{
    TPMInfo *res = g_new0(TPMInfo, 1);
    TPMPassthroughOptions *tpo;

    res->id = g_strdup(drv->id);
    res->model = drv->fe_model;
    res->options = g_new0(TpmTypeOptions, 1);

    switch (drv->ops->type) {
    case TPM_TYPE_PASSTHROUGH:
        res->options->type = TPM_TYPE_OPTIONS_KIND_PASSTHROUGH;
        tpo = g_new0(TPMPassthroughOptions, 1);
        res->options->u.passthrough.data = tpo;
        if (drv->path) {
            tpo->path = g_strdup(drv->path);
            tpo->has_path = true;
        }
        if (drv->cancel_path) {
            tpo->cancel_path = g_strdup(drv->cancel_path);
            tpo->has_cancel_path = true;
        }
        break;
    case TPM_TYPE__MAX:
        break;
    }

    return res;
}

/*
 * Walk the list of active TPM backends and collect information about them
 * following the schema description in qapi-schema.json.
 */
TPMInfoList *qmp_query_tpm(Error **errp)
{
    TPMBackend *drv;
    TPMInfoList *info, *head = NULL, *cur_item = NULL;

    QLIST_FOREACH(drv, &tpm_backends, list) {
        if (!tpm_model_is_registered(drv->fe_model)) {
            continue;
        }
        info = g_new0(TPMInfoList, 1);
        info->value = qmp_query_tpm_inst(drv);

        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }
    }

    return head;
}

TpmTypeList *qmp_query_tpm_types(Error **errp)
{
    unsigned int i = 0;
    TpmTypeList *head = NULL, *prev = NULL, *cur_item;

    for (i = 0; i < TPM_TYPE__MAX; i++) {
        if (!tpm_driver_find_by_type(i)) {
            continue;
        }
        cur_item = g_new0(TpmTypeList, 1);
        cur_item->value = i;

        if (prev) {
            prev->next = cur_item;
        }
        if (!head) {
            head = cur_item;
        }
        prev = cur_item;
    }

    return head;
}

TpmModelList *qmp_query_tpm_models(Error **errp)
{
    unsigned int i = 0;
    TpmModelList *head = NULL, *prev = NULL, *cur_item;

    for (i = 0; i < TPM_MODEL__MAX; i++) {
        if (!tpm_model_is_registered(i)) {
            continue;
        }
        cur_item = g_new0(TpmModelList, 1);
        cur_item->value = i;

        if (prev) {
            prev->next = cur_item;
        }
        if (!head) {
            head = cur_item;
        }
        prev = cur_item;
    }

    return head;
}
