/*
 * CPER payload parser for error injection
 *
 * Copyright(C) 2024-2025 Huawei LTD.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qemu/base64.h"
#include "qemu/error-report.h"
#include "qemu/uuid.h"
#include "qapi/qapi-commands-acpi-hest.h"
#include "hw/acpi/ghes.h"

void qmp_inject_ghes_v2_error(const char *qmp_cper, Error **errp)
{
    AcpiGhesState *ags;
    uint8_t *cper;
    size_t  len;

    ags = acpi_ghes_get_state();
    if (!ags) {
        return;
    }

    cper = qbase64_decode(qmp_cper, -1, &len, errp);
    if (!cper) {
        error_setg(errp, "missing GHES CPER payload");
        return;
    }

    ghes_record_cper_errors(ags, cper, len, ACPI_HEST_SRC_ID_QMP, errp);

    g_free(cper);
}
