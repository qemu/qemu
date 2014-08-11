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

#include "qemu/option.h"

typedef struct TPMState TPMState;

int tpm_config_parse(QemuOptsList *opts_list, const char *optarg);
int tpm_init(void);
void tpm_cleanup(void);

#define TYPE_TPM_TIS                "tpm-tis"

static inline bool tpm_find(void)
{
    return object_resolve_path_type("", TYPE_TPM_TIS, NULL);
}

#endif /* QEMU_TPM_H */
