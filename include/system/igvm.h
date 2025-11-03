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

/* x86 native */
int qigvm_x86_get_mem_map_entry(int index,
                                ConfidentialGuestMemoryMapEntry *entry,
                                Error **errp);
int qigvm_x86_set_vp_context(void *data, int index,
                             Error **errp);

#endif
