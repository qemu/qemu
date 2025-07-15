/*
 * VFIO migration
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_MIGRATION_INTERNAL_H
#define HW_VFIO_VFIO_MIGRATION_INTERNAL_H

#ifdef CONFIG_LINUX
#include <linux/vfio.h>
#endif

#include "qemu/typedefs.h"
#include "qemu/notify.h"

/*
 * Flags to be used as unique delimiters for VFIO devices in the migration
 * stream. These flags are composed as:
 * 0xffffffff => MSB 32-bit all 1s
 * 0xef10     => Magic ID, represents emulated (virtual) function IO
 * 0x0000     => 16-bits reserved for flags
 *
 * The beginning of state information is marked by _DEV_CONFIG_STATE,
 * _DEV_SETUP_STATE, or _DEV_DATA_STATE, respectively. The end of a
 * certain state information is marked by _END_OF_STATE.
 */
#define VFIO_MIG_FLAG_END_OF_STATE      (0xffffffffef100001ULL)
#define VFIO_MIG_FLAG_DEV_CONFIG_STATE  (0xffffffffef100002ULL)
#define VFIO_MIG_FLAG_DEV_SETUP_STATE   (0xffffffffef100003ULL)
#define VFIO_MIG_FLAG_DEV_DATA_STATE    (0xffffffffef100004ULL)
#define VFIO_MIG_FLAG_DEV_INIT_DATA_SENT (0xffffffffef100005ULL)
#define VFIO_MIG_FLAG_DEV_CONFIG_LOAD_READY (0xffffffffef100006ULL)

typedef struct VFIODevice VFIODevice;
typedef struct VFIOMultifd VFIOMultifd;

typedef struct VFIOMigration {
    struct VFIODevice *vbasedev;
    VMChangeStateEntry *vm_state;
    NotifierWithReturn migration_state;
    uint32_t device_state;
    int data_fd;
    void *data_buffer;
    size_t data_buffer_size;
    uint64_t mig_flags;
    uint64_t precopy_init_size;
    uint64_t precopy_dirty_size;
    bool multifd_transfer;
    VFIOMultifd *multifd;
    bool initial_data_sent;

    bool event_save_iterate_started;
    bool event_precopy_empty_hit;
} VFIOMigration;

bool vfio_migration_realize(VFIODevice *vbasedev, Error **errp);
void vfio_migration_exit(VFIODevice *vbasedev);
bool vfio_device_state_is_running(VFIODevice *vbasedev);
bool vfio_device_state_is_precopy(VFIODevice *vbasedev);
int vfio_save_device_config_state(QEMUFile *f, void *opaque, Error **errp);
int vfio_load_device_config_state(QEMUFile *f, void *opaque);

#ifdef CONFIG_LINUX
int vfio_migration_set_state(VFIODevice *vbasedev,
                             enum vfio_device_mig_state new_state,
                             enum vfio_device_mig_state recover_state,
                             Error **errp);
#endif

void vfio_migration_add_bytes_transferred(unsigned long val);

#endif /* HW_VFIO_VFIO_MIGRATION_INTERNAL_H */
