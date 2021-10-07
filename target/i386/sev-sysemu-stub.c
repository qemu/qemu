/*
 * QEMU SEV system stub
 *
 * Copyright Advanced Micro Devices 2018
 *
 * Authors:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "sev.h"

SevInfo *sev_get_info(void)
{
    return NULL;
}

char *sev_get_launch_measurement(void)
{
    return NULL;
}

SevCapability *sev_get_capabilities(Error **errp)
{
    error_setg(errp, "SEV is not available in this QEMU");
    return NULL;
}

int sev_inject_launch_secret(const char *hdr, const char *secret,
                             uint64_t gpa, Error **errp)
{
    return 1;
}

int sev_encrypt_flash(uint8_t *ptr, uint64_t len, Error **errp)
{
    g_assert_not_reached();
}

void sev_es_set_reset_vector(CPUState *cpu)
{
}

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size)
{
    g_assert_not_reached();
}

SevAttestationReport *qmp_query_sev_attestation_report(const char *mnonce,
                                                       Error **errp)
{
    error_setg(errp, "SEV is not available in this QEMU");
    return NULL;
}
