/*
 * VFIO migration interface
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_MIGRATION_H
#define HW_VFIO_VFIO_MIGRATION_H

bool vfio_migration_active(void);
int64_t vfio_migration_bytes_transferred(void);
void vfio_migration_reset_bytes_transferred(void);

#endif /* HW_VFIO_VFIO_MIGRATION_H */
