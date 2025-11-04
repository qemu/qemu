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

#include "qapi/qapi-types-migration.h"

/**
 * @migrate_add_blocker - prevent all modes of migration from proceeding
 *
 * @reasonp - address of an error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @returns - 0 on success, -EBUSY/-EACCES on failure, with errp set.
 *
 * *@reasonp is freed and set to NULL if failure is returned.
 * On success, the caller must not free @reasonp, except by
 *   calling migrate_del_blocker.
 */
int migrate_add_blocker(Error **reasonp, Error **errp);

/**
 * @migrate_add_blocker_internal - prevent all modes of migration from
 *                                 proceeding, but ignore -only-migratable
 *
 * @reasonp - address of an error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @returns - 0 on success, -EBUSY on failure, with errp set.
 *
 * Some of the migration blockers can be temporary (e.g., for a few seconds),
 * so it shouldn't need to conflict with "-only-migratable".  For those cases,
 * we can call this function rather than @migrate_add_blocker().
 *
 * *@reasonp is freed and set to NULL if failure is returned.
 * On success, the caller must not free @reasonp, except by
 *   calling migrate_del_blocker.
 */
int migrate_add_blocker_internal(Error **reasonp, Error **errp);

/**
 * @migrate_del_blocker - remove a migration blocker from all modes and free it.
 *
 * @reasonp - address of the error blocking migration
 *
 * This function frees *@reasonp and sets it to NULL.
 */
void migrate_del_blocker(Error **reasonp);

/**
 * @migrate_add_blocker_normal - prevent normal migration mode from proceeding
 *
 * @reasonp - address of an error to be returned whenever migration is attempted
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @returns - 0 on success, -EBUSY/-EACCES on failure, with errp set.
 *
 * *@reasonp is freed and set to NULL if failure is returned.
 * On success, the caller must not free @reasonp, except by
 *   calling migrate_del_blocker.
 */
int migrate_add_blocker_normal(Error **reasonp, Error **errp);

/**
 * @migrate_add_blocker_modes - prevent some modes of migration from proceeding
 *
 * @reasonp - address of an error to be returned whenever migration is attempted
 *
 * @modes - the migration modes to be blocked, a bit set of MigMode
 *
 * @errp - [out] The reason (if any) we cannot block migration right now.
 *
 * @returns - 0 on success, -EBUSY/-EACCES on failure, with errp set.
 *
 * *@reasonp is freed and set to NULL if failure is returned.
 * On success, the caller must not free *@reasonp before the blocker is removed.
 */
int migrate_add_blocker_modes(Error **reasonp, unsigned modes, Error **errp);

#endif
