/*
 * QEMU IGVM configuration backend for Confidential Guests
 *
 * Copyright (C) 2023-2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@randomman.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BACKENDS_IGVM_H
#define BACKENDS_IGVM_H

#include "system/confidential-guest-support.h"
#include "system/igvm-cfg.h"
#include "qapi/error.h"

int qigvm_process_file(IgvmCfg *igvm, ConfidentialGuestSupport *cgs,
                      bool onlyVpContext, Error **errp);

#endif
