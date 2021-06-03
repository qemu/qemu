/*
 * Accelerator CPUS Interface
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HVF_CPUS_H
#define HVF_CPUS_H

#include "sysemu/cpus.h"

int hvf_vcpu_exec(CPUState *);

#endif /* HVF_CPUS_H */
