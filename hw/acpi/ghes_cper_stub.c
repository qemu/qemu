/*
 * Stub interface for CPER payload parser for error injection
 *
 * Copyright(C) 2024-2025 Huawei LTD.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-acpi-hest.h"
#include "hw/acpi/ghes.h"

void qmp_inject_ghes_v2_error(const char *cper, Error **errp)
{
    error_setg(errp, "GHES QMP error inject is not compiled in");
}
