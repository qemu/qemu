/*
 * TPM stubs
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-tpm.h"
#include "sysemu/tpm.h"

void tpm_init(void)
{
}

void tpm_cleanup(void)
{
}

TPMInfoList *qmp_query_tpm(Error **errp)
{
    return NULL;
}

TpmTypeList *qmp_query_tpm_types(Error **errp)
{
    return NULL;
}

TpmModelList *qmp_query_tpm_models(Error **errp)
{
    return NULL;
}
