/*
 * QEMU IGVM interface
 *
 * Copyright (C) 2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@randomman.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_IGVM_CFG_H
#define QEMU_IGVM_CFG_H

#include "qom/object.h"

typedef struct IgvmCfg {
    ObjectClass parent_class;

    /*
     * filename: Filename that specifies a file that contains the configuration
     *           of the guest in Independent Guest Virtual Machine (IGVM)
     *           format.
     */
    char *filename;
} IgvmCfg;

typedef struct IgvmCfgClass {
    ObjectClass parent_class;

    /*
     * If an IGVM filename has been specified then process the IGVM file.
     * Performs a no-op if no filename has been specified.
     * If onlyVpContext is true then only the IGVM_VHT_VP_CONTEXT entries
     * in the IGVM file will be processed, allowing information about the
     * CPU state to be determined before processing the entire file.
     *
     * Returns 0 for ok and -1 on error.
     */
    int (*process)(IgvmCfg *cfg, ConfidentialGuestSupport *cgs,
                   bool onlyVpContext, Error **errp);

} IgvmCfgClass;

#define TYPE_IGVM_CFG "igvm-cfg"

OBJECT_DECLARE_TYPE(IgvmCfg, IgvmCfgClass, IGVM_CFG)

#endif
