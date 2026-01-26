/*
 * QEMU IGVM private data structures
 *
 * Everything which depends on igvm library headers goes here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_IGVM_INTERNAL_H
#define QEMU_IGVM_INTERNAL_H

#include "qemu/typedefs.h"
#include "qom/object.h"

struct IgvmCfg {
    ObjectClass parent_class;

    /*
     * filename: Filename that specifies a file that contains the configuration
     *           of the guest in Independent Guest Virtual Machine (IGVM)
     *           format.
     */
    char *filename;
};

#endif
