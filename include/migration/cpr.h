/*
 * Copyright (c) 2021, 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIGRATION_CPR_H
#define MIGRATION_CPR_H

#include "qapi/qapi-types-migration.h"
#include "qemu/queue.h"

#define MIG_MODE_NONE           -1

#define QEMU_CPR_FILE_MAGIC     0x51435052
#define QEMU_CPR_FILE_VERSION   0x00000001
#define CPR_STATE "CprState"

typedef QLIST_HEAD(CprFdList, CprFd) CprFdList;
typedef QLIST_HEAD(CprVFIODeviceList, CprVFIODevice) CprVFIODeviceList;

typedef struct CprState {
    CprFdList fds;
    CprVFIODeviceList vfio_devices;
} CprState;

extern CprState cpr_state;

void cpr_save_fd(const char *name, int id, int fd);
void cpr_delete_fd(const char *name, int id);
int cpr_find_fd(const char *name, int id);
void cpr_resave_fd(const char *name, int id, int fd);
int cpr_open_fd(const char *path, int flags, const char *name, int id,
                Error **errp);

typedef bool (*cpr_walk_fd_cb)(int fd);
bool cpr_walk_fd(cpr_walk_fd_cb cb);

MigMode cpr_get_incoming_mode(void);
void cpr_set_incoming_mode(MigMode mode);
bool cpr_is_incoming(void);

bool cpr_state_save(MigrationChannel *channel, Error **errp);
int cpr_state_load(MigrationChannel *channel, Error **errp);
void cpr_state_close(void);
struct QIOChannel *cpr_state_ioc(void);

bool cpr_incoming_needed(void *opaque);
int cpr_get_fd_param(const char *name, const char *fdname, int index,
                     Error **errp);

QEMUFile *cpr_transfer_output(MigrationChannel *channel, Error **errp);
QEMUFile *cpr_transfer_input(MigrationChannel *channel, Error **errp);

void cpr_exec_init(void);
QEMUFile *cpr_exec_output(Error **errp);
QEMUFile *cpr_exec_input(Error **errp);
bool cpr_exec_persist_state(QEMUFile *f, Error **errp);
bool cpr_exec_has_state(void);
void cpr_exec_unpersist_state(void);
void cpr_exec_unpreserve_fds(void);
#endif
