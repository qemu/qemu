/*
 * QEMU System Emulation accel internal functions
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ACCEL_SOFTMMU_H
#define ACCEL_SOFTMMU_H

void accel_init_ops_interfaces(AccelClass *ac);

#endif /* ACCEL_SOFTMMU_H */
