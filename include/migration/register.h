/*
 * QEMU migration vmstate registration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MIGRATION_REGISTER_H
#define MIGRATION_REGISTER_H

typedef struct SaveVMHandlers {
    /* This runs inside the iothread lock.  */
    SaveStateHandler *save_state;

    void (*save_cleanup)(void *opaque);
    int (*save_live_complete_postcopy)(QEMUFile *f, void *opaque);
    int (*save_live_complete_precopy)(QEMUFile *f, void *opaque);

    /* This runs both outside and inside the iothread lock.  */
    bool (*is_active)(void *opaque);

    /* This runs outside the iothread lock in the migration case, and
     * within the lock in the savevm case.  The callback had better only
     * use data that is local to the migration thread or protected
     * by other locks.
     */
    int (*save_live_iterate)(QEMUFile *f, void *opaque);

    /* This runs outside the iothread lock!  */
    int (*save_setup)(QEMUFile *f, void *opaque);
    void (*save_live_pending)(QEMUFile *f, void *opaque,
                              uint64_t threshold_size,
                              uint64_t *non_postcopiable_pending,
                              uint64_t *postcopiable_pending);
    LoadStateHandler *load_state;
    int (*load_setup)(QEMUFile *f, void *opaque);
    int (*load_cleanup)(void *opaque);
} SaveVMHandlers;

int register_savevm_live(DeviceState *dev,
                         const char *idstr,
                         int instance_id,
                         int version_id,
                         SaveVMHandlers *ops,
                         void *opaque);

void unregister_savevm(DeviceState *dev, const char *idstr, void *opaque);

#endif
