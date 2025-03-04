/*
 * Multifd VFIO migration
 *
 * Copyright (C) 2024,2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_MIGRATION_MULTIFD_H
#define HW_VFIO_MIGRATION_MULTIFD_H

#include "hw/vfio/vfio-common.h"

bool vfio_multifd_setup(VFIODevice *vbasedev, bool alloc_multifd, Error **errp);
void vfio_multifd_cleanup(VFIODevice *vbasedev);

bool vfio_multifd_transfer_supported(void);
bool vfio_multifd_transfer_enabled(VFIODevice *vbasedev);

bool vfio_multifd_load_state_buffer(void *opaque, char *data, size_t data_size,
                                    Error **errp);

void vfio_multifd_emit_dummy_eos(VFIODevice *vbasedev, QEMUFile *f);

bool
vfio_multifd_save_complete_precopy_thread(SaveLiveCompletePrecopyThreadData *d,
                                          Error **errp);

int vfio_multifd_switchover_start(VFIODevice *vbasedev);

#endif
