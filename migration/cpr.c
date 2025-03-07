/*
 * Copyright (c) 2021-2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/cpr.h"
#include "migration/misc.h"
#include "migration/options.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/vmstate.h"
#include "system/runstate.h"
#include "trace.h"

/*************************************************************************/
/* cpr state container for all information to be saved. */

typedef QLIST_HEAD(CprFdList, CprFd) CprFdList;

typedef struct CprState {
    CprFdList fds;
} CprState;

static CprState cpr_state;

/****************************************************************************/

typedef struct CprFd {
    char *name;
    unsigned int namelen;
    int id;
    int fd;
    QLIST_ENTRY(CprFd) next;
} CprFd;

static const VMStateDescription vmstate_cpr_fd = {
    .name = "cpr fd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(namelen, CprFd),
        VMSTATE_VBUFFER_ALLOC_UINT32(name, CprFd, 0, NULL, namelen),
        VMSTATE_INT32(id, CprFd),
        VMSTATE_FD(fd, CprFd),
        VMSTATE_END_OF_LIST()
    }
};

void cpr_save_fd(const char *name, int id, int fd)
{
    CprFd *elem = g_new0(CprFd, 1);

    trace_cpr_save_fd(name, id, fd);
    elem->name = g_strdup(name);
    elem->namelen = strlen(name) + 1;
    elem->id = id;
    elem->fd = fd;
    QLIST_INSERT_HEAD(&cpr_state.fds, elem, next);
}

static CprFd *find_fd(CprFdList *head, const char *name, int id)
{
    CprFd *elem;

    QLIST_FOREACH(elem, head, next) {
        if (!strcmp(elem->name, name) && elem->id == id) {
            return elem;
        }
    }
    return NULL;
}

void cpr_delete_fd(const char *name, int id)
{
    CprFd *elem = find_fd(&cpr_state.fds, name, id);

    if (elem) {
        QLIST_REMOVE(elem, next);
        g_free(elem->name);
        g_free(elem);
    }

    trace_cpr_delete_fd(name, id);
}

int cpr_find_fd(const char *name, int id)
{
    CprFd *elem = find_fd(&cpr_state.fds, name, id);
    int fd = elem ? elem->fd : -1;

    trace_cpr_find_fd(name, id, fd);
    return fd;
}
/*************************************************************************/
#define CPR_STATE "CprState"

static const VMStateDescription vmstate_cpr_state = {
    .name = CPR_STATE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_QLIST_V(fds, CprState, 1, vmstate_cpr_fd, CprFd, next),
        VMSTATE_END_OF_LIST()
    }
};
/*************************************************************************/

static QEMUFile *cpr_state_file;

QIOChannel *cpr_state_ioc(void)
{
    return qemu_file_get_ioc(cpr_state_file);
}

static MigMode incoming_mode = MIG_MODE_NONE;

MigMode cpr_get_incoming_mode(void)
{
    return incoming_mode;
}

void cpr_set_incoming_mode(MigMode mode)
{
    incoming_mode = mode;
}

bool cpr_is_incoming(void)
{
    return incoming_mode != MIG_MODE_NONE;
}

int cpr_state_save(MigrationChannel *channel, Error **errp)
{
    int ret;
    QEMUFile *f;
    MigMode mode = migrate_mode();

    trace_cpr_state_save(MigMode_str(mode));

    if (mode == MIG_MODE_CPR_TRANSFER) {
        g_assert(channel);
        f = cpr_transfer_output(channel, errp);
    } else {
        return 0;
    }
    if (!f) {
        return -1;
    }

    qemu_put_be32(f, QEMU_CPR_FILE_MAGIC);
    qemu_put_be32(f, QEMU_CPR_FILE_VERSION);

    ret = vmstate_save_state(f, &vmstate_cpr_state, &cpr_state, 0);
    if (ret) {
        error_setg(errp, "vmstate_save_state error %d", ret);
        qemu_fclose(f);
        return ret;
    }

    /*
     * Close the socket only partially so we can later detect when the other
     * end closes by getting a HUP event.
     */
    qemu_fflush(f);
    qio_channel_shutdown(qemu_file_get_ioc(f), QIO_CHANNEL_SHUTDOWN_WRITE,
                         NULL);
    cpr_state_file = f;
    return 0;
}

int cpr_state_load(MigrationChannel *channel, Error **errp)
{
    int ret;
    uint32_t v;
    QEMUFile *f;
    MigMode mode = 0;

    if (channel) {
        mode = MIG_MODE_CPR_TRANSFER;
        cpr_set_incoming_mode(mode);
        f = cpr_transfer_input(channel, errp);
    } else {
        return 0;
    }
    if (!f) {
        return -1;
    }

    trace_cpr_state_load(MigMode_str(mode));

    v = qemu_get_be32(f);
    if (v != QEMU_CPR_FILE_MAGIC) {
        error_setg(errp, "Not a migration stream (bad magic %x)", v);
        qemu_fclose(f);
        return -EINVAL;
    }
    v = qemu_get_be32(f);
    if (v != QEMU_CPR_FILE_VERSION) {
        error_setg(errp, "Unsupported migration stream version %d", v);
        qemu_fclose(f);
        return -ENOTSUP;
    }

    ret = vmstate_load_state(f, &vmstate_cpr_state, &cpr_state, 1);
    if (ret) {
        error_setg(errp, "vmstate_load_state error %d", ret);
        qemu_fclose(f);
        return ret;
    }

    /*
     * Let the caller decide when to close the socket (and generate a HUP event
     * for the sending side).
     */
    cpr_state_file = f;

    return ret;
}

void cpr_state_close(void)
{
    if (cpr_state_file) {
        qemu_fclose(cpr_state_file);
        cpr_state_file = NULL;
    }
}
