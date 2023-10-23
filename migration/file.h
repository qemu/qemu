/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_FILE_H
#define QEMU_MIGRATION_FILE_H

#include "qapi/qapi-types-migration.h"

void file_start_incoming_migration(FileMigrationArgs *file_args, Error **errp);

void file_start_outgoing_migration(MigrationState *s,
                                   FileMigrationArgs *file_args, Error **errp);
int file_parse_offset(char *filespec, uint64_t *offsetp, Error **errp);
#endif
