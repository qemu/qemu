/*
 * QEMU snapshots
 *
 * Copyright (c) 2004-2008 Fabrice Bellard
 * Copyright (c) 2009-2015 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_SNAPSHOT_H
#define QEMU_MIGRATION_SNAPSHOT_H

int save_snapshot(const char *name, Error **errp);
int load_snapshot(const char *name, Error **errp);

#endif
