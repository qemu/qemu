/*
 * QEMU migration blockers
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

#ifndef MIGRATION_BLOCKER_H
#define MIGRATION_BLOCKER_H

/**
 * @migrate_add_blocker - prevent migration from proceeding
 *
 * @reason - an error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @returns - 0 on success, -EBUSY/-EACCES on failure, with errp set.
 */
int migrate_add_blocker(Error *reason, Error **errp);

/**
 * @migrate_del_blocker - remove a blocking error from migration
 *
 * @reason - the error blocking migration
 */
void migrate_del_blocker(Error *reason);

#endif
