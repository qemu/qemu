/*
 * QEMU live migration
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

#ifndef QEMU_MIGRATION_H
#define QEMU_MIGRATION_H

#define MIG_STATE_ERROR		-1
#define MIG_STATE_COMPLETED	0
#define MIG_STATE_CANCELLED	1
#define MIG_STATE_ACTIVE	2

typedef struct MigrationState MigrationState;

struct MigrationState
{
    /* FIXME: add more accessors to print migration info */
    void (*cancel)(MigrationState *s);
    int (*get_status)(MigrationState *s);
    void (*release)(MigrationState *s);
};

void qemu_start_incoming_migration(const char *uri);

void do_migrate(int detach, const char *uri);

void do_migrate_cancel(void);

void do_migrate_set_speed(const char *value);

void do_info_migrate(void);

#endif

