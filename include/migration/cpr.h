/*
 * Copyright (c) 2021, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

#include "qapi/qapi-types-migration.h"

#define MIG_MODE_NONE           -1

#define QEMU_CPR_FILE_MAGIC     0x51435052
#define QEMU_CPR_FILE_VERSION   0x00000001

void cpr_save_fd(const char *name, int id, int fd);
void cpr_delete_fd(const char *name, int id);
int cpr_find_fd(const char *name, int id);

MigMode cpr_get_incoming_mode(void);
void cpr_set_incoming_mode(MigMode mode);
bool cpr_is_incoming(void);

int cpr_state_save(MigrationChannel *channel, Error **errp);
int cpr_state_load(MigrationChannel *channel, Error **errp);
void cpr_state_close(void);
struct QIOChannel *cpr_state_ioc(void);

QEMUFile *cpr_transfer_output(MigrationChannel *channel, Error **errp);
QEMUFile *cpr_transfer_input(MigrationChannel *channel, Error **errp);

#endif
