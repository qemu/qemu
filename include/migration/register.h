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
    bool (*has_postcopy)(void *opaque);

    /* is_active_iterate
     * If it is not NULL then qemu_savevm_state_iterate will skip iteration if
     * it returns false. For example, it is needed for only-postcopy-states,
     * which needs to be handled by qemu_savevm_state_setup and
     * qemu_savevm_state_pending, but do not need iterations until not in
     * postcopy stage.
     */
    bool (*is_active_iterate)(void *opaque);

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
                              uint64_t *res_precopy_only,
                              uint64_t *res_compatible,
                              uint64_t *res_postcopy_only);
    /* Note for save_live_pending:
     * - res_precopy_only is for data which must be migrated in precopy phase
     *     or in stopped state, in other words - before target vm start
     * - res_compatible is for data which may be migrated in any phase
     * - res_postcopy_only is for data which must be migrated in postcopy phase
     *     or in stopped state, in other words - after source vm stop
     *
     * Sum of res_postcopy_only, res_compatible and res_postcopy_only is the
     * whole amount of pending data.
     */


    LoadStateHandler *load_state;
    int (*load_setup)(QEMUFile *f, void *opaque);
    int (*load_cleanup)(void *opaque);
    /* Called when postcopy migration wants to resume from failure */
    int (*resume_prepare)(MigrationState *s, void *opaque);
} SaveVMHandlers;

int register_savevm_live(const char *idstr,
                         int instance_id,
                         int version_id,
                         const SaveVMHandlers *ops,
                         void *opaque);

void unregister_savevm(DeviceState *dev, const char *idstr, void *opaque);

#endif
