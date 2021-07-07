/*
 * CPU models for s390x - User-mode
 *
 * Copyright 2016 IBM Corp.
 *
 * Author(s): David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "qapi/error.h"

void apply_cpu_model(const S390CPUModel *model, Error **errp)
{
}
