/*
 * Memory manager for remote device
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_MEMORY_H
#define REMOTE_MEMORY_H

#include "exec/hwaddr.h"
#include "hw/remote/mpqemu-link.h"

void remote_sysmem_reconfig(MPQemuMsg *msg, Error **errp);

#endif
