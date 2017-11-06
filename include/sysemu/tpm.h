/*
 * Public TPM functions
 *
 * Copyright (C) 2011-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_TPM_H
#define QEMU_TPM_H

#include "qom/object.h"
#include "qapi-types.h"

int tpm_config_parse(QemuOptsList *opts_list, const char *optarg);
int tpm_init(void);
void tpm_cleanup(void);

typedef enum  TPMVersion {
    TPM_VERSION_UNSPEC = 0,
    TPM_VERSION_1_2 = 1,
    TPM_VERSION_2_0 = 2,
} TPMVersion;

#define TYPE_TPM_IF "tpm-if"
#define TPM_IF_CLASS(klass)                                 \
    OBJECT_CLASS_CHECK(TPMIfClass, (klass), TYPE_TPM_IF)
#define TPM_IF_GET_CLASS(obj)                           \
    OBJECT_GET_CLASS(TPMIfClass, (obj), TYPE_TPM_IF)
#define TPM_IF(obj)                             \
    INTERFACE_CHECK(TPMIf, (obj), TYPE_TPM_IF)

typedef struct TPMIf {
    Object parent_obj;
} TPMIf;

typedef struct TPMIfClass {
    InterfaceClass parent_class;

    void (*request_completed)(TPMIf *obj);
} TPMIfClass;

TPMVersion tpm_tis_get_tpm_version(Object *obj);

#define TYPE_TPM_TIS                "tpm-tis"

static inline TPMVersion tpm_get_version(void)
{
#ifdef CONFIG_TPM
    Object *obj = object_resolve_path_type("", TYPE_TPM_TIS, NULL);

    if (obj) {
        return tpm_tis_get_tpm_version(obj);
    }
#endif
    return TPM_VERSION_UNSPEC;
}

#endif /* QEMU_TPM_H */
