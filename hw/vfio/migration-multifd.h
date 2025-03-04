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

bool vfio_multifd_transfer_supported(void);

#endif
