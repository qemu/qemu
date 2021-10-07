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
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qapi/qapi-commands-misc-target.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "sev.h"

SevInfo *qmp_query_sev(Error **errp)
{
    error_setg(errp, "SEV is not available in this QEMU");
    return NULL;
}

SevLaunchMeasureInfo *qmp_query_sev_launch_measure(Error **errp)
{
    error_setg(errp, "SEV is not available in this QEMU");
    return NULL;
}

SevCapability *qmp_query_sev_capabilities(Error **errp)
{
    error_setg(errp, "SEV is not available in this QEMU");
    return NULL;
}

void qmp_sev_inject_launch_secret(const char *packet_header, const char *secret,
                                  bool has_gpa, uint64_t gpa, Error **errp)
{
    error_setg(errp, "SEV is not available in this QEMU");
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

void hmp_info_sev(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "SEV is not available in this QEMU\n");
}
