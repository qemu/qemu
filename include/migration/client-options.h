/*
 * QEMU public migration capabilities
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_CLIENT_OPTIONS_H
#define QEMU_MIGRATION_CLIENT_OPTIONS_H


/* properties */
bool migrate_send_switchover_start(void);

/* capabilities */

bool migrate_background_snapshot(void);
bool migrate_dirty_limit(void);
bool migrate_postcopy_ram(void);
bool migrate_switchover_ack(void);

/* parameters */

MigMode migrate_mode(void);
uint64_t migrate_vcpu_dirty_limit_period(void);

#endif
