/*
 * QEMU live migration via generic fd
 *
 * Copyright Red Hat, Inc. 2009-2016
 *
 * Authors:
 *  Chris Lalancette <clalance@redhat.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_MIGRATION_FD_H
#define QEMU_MIGRATION_FD_H
void fd_start_incoming_migration(const char *path, Error **errp);

void fd_start_outgoing_migration(MigrationState *s, const char *fdname,
                                 Error **errp);
#endif
