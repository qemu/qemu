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

#include "qapi/qapi-types-tpm.h"
#include "qom/object.h"

#ifdef CONFIG_TPM

int tpm_config_parse(QemuOptsList *opts_list, const char *optstr);
int tpm_init(void);
void tpm_cleanup(void);

typedef enum TPMVersion {
    TPM_VERSION_UNSPEC = 0,
    TPM_VERSION_1_2 = 1,
    TPM_VERSION_2_0 = 2,
} TPMVersion;

#define TYPE_TPM_IF "tpm-if"
typedef struct TPMIfClass TPMIfClass;
DECLARE_CLASS_CHECKERS(TPMIfClass, TPM_IF,
                       TYPE_TPM_IF)
#define TPM_IF(obj)                             \
    INTERFACE_CHECK(TPMIf, (obj), TYPE_TPM_IF)

typedef struct TPMIf TPMIf;

struct TPMIfClass {
    InterfaceClass parent_class;

    enum TpmModel model;
    void (*request_completed)(TPMIf *obj, int ret);
    enum TPMVersion (*get_version)(TPMIf *obj);
};

#define TYPE_TPM_TIS_ISA            "tpm-tis"
#define TYPE_TPM_TIS_SYSBUS         "tpm-tis-device"
#define TYPE_TPM_CRB                "tpm-crb"
#define TYPE_TPM_SPAPR              "tpm-spapr"
#define TYPE_TPM_TIS_I2C            "tpm-tis-i2c"

#define TPM_IS_TIS_ISA(chr)                         \
    object_dynamic_cast(OBJECT(chr), TYPE_TPM_TIS_ISA)
#define TPM_IS_TIS_SYSBUS(chr)                      \
    object_dynamic_cast(OBJECT(chr), TYPE_TPM_TIS_SYSBUS)
#define TPM_IS_CRB(chr)                             \
    object_dynamic_cast(OBJECT(chr), TYPE_TPM_CRB)
#define TPM_IS_SPAPR(chr)                           \
    object_dynamic_cast(OBJECT(chr), TYPE_TPM_SPAPR)
#define TPM_IS_TIS_I2C(chr)                      \
    object_dynamic_cast(OBJECT(chr), TYPE_TPM_TIS_I2C)

/* returns NULL unless there is exactly one TPM device */
static inline TPMIf *tpm_find(void)
{
    Object *obj = object_resolve_path_type("", TYPE_TPM_IF, NULL);

    return TPM_IF(obj);
}

static inline TPMVersion tpm_get_version(TPMIf *ti)
{
    if (!ti) {
        return TPM_VERSION_UNSPEC;
    }

    return TPM_IF_GET_CLASS(ti)->get_version(ti);
}

#else /* CONFIG_TPM */

#define tpm_init()  (0)
#define tpm_cleanup()

/* needed for an alignment check in non-tpm code */
static inline Object *TPM_IS_CRB(Object *obj)
{
     return NULL;
}

#endif /* CONFIG_TPM */

#endif /* QEMU_TPM_H */
