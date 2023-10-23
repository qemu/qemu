/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 * Copyright Dell MessageOne 2008
 * Copyright Red Hat, Inc. 2015-2016
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Charles Duffy     <charles_duffy@messageone.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_MIGRATION_EXEC_H
#define QEMU_MIGRATION_EXEC_H

#ifdef WIN32
const char *exec_get_cmd_path(void);
#endif
void exec_start_incoming_migration(strList *host_port, Error **errp);

void exec_start_outgoing_migration(MigrationState *s, strList *host_port,
                                   Error **errp);
#endif
