/*
 * QEMU Plugin API - linux-user-mode only implementations
 *
 * Common user-mode only APIs are in plugins/api-user. These helpers
 * are only specific to linux-user.
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019-2025, Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "common-user/plugin-api.c.inc"
