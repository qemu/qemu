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

#include "hw/vmstate-if.h"

typedef struct SaveVMHandlers {
    /* This runs inside the iothread lock.  */
    SaveStateHandler *save_state;

    /*
     * save_prepare is called early, even before migration starts, and can be
     * used to perform early checks.
     */
    int (*save_prepare)(void *opaque, Error **errp);
    int (*save_setup)(QEMUFile *f, void *opaque);
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
    /* Note for save_live_pending:
     * must_precopy:
     * - must be migrated in precopy or in stopped state
     * - i.e. must be migrated before target start
     *
     * can_postcopy:
     * - can migrate in postcopy or in stopped state
     * - i.e. can migrate after target start
     * - some can also be migrated during precopy (RAM)
     * - some must be migrated after source stops (block-dirty-bitmap)
     *
     * Sum of can_postcopy and must_postcopy is the whole amount of
     * pending data.
     */
    /* This estimates the remaining data to transfer */
    void (*state_pending_estimate)(void *opaque, uint64_t *must_precopy,
                                   uint64_t *can_postcopy);
    /* This calculate the exact remaining data to transfer */
    void (*state_pending_exact)(void *opaque, uint64_t *must_precopy,
                                uint64_t *can_postcopy);
    LoadStateHandler *load_state;
    int (*load_setup)(QEMUFile *f, void *opaque);
    int (*load_cleanup)(void *opaque);
    /* Called when postcopy migration wants to resume from failure */
    int (*resume_prepare)(MigrationState *s, void *opaque);
    /* Checks if switchover ack should be used. Called only in dest */
    bool (*switchover_ack_needed)(void *opaque);
} SaveVMHandlers;

int register_savevm_live(const char *idstr,
                         uint32_t instance_id,
                         int version_id,
                         const SaveVMHandlers *ops,
                         void *opaque);

void unregister_savevm(VMStateIf *obj, const char *idstr, void *opaque);

#endif
