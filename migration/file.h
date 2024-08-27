/*
 * Copyright (c) 2021-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_FILE_H
#define QEMU_MIGRATION_FILE_H

#include "qapi/qapi-types-migration.h"
#include "io/task.h"
#include "channel.h"
#include "multifd.h"

void file_start_incoming_migration(FileMigrationArgs *file_args, Error **errp);

void file_start_outgoing_migration(MigrationState *s,
                                   FileMigrationArgs *file_args, Error **errp);
int file_parse_offset(char *filespec, uint64_t *offsetp, Error **errp);
void file_cleanup_outgoing_migration(void);
bool file_send_channel_create(gpointer opaque, Error **errp);
int file_write_ramblock_iov(QIOChannel *ioc, const struct iovec *iov,
                            int niov, MultiFDPages_t *pages, Error **errp);
int multifd_file_recv_data(MultiFDRecvParams *p, Error **errp);
#endif
