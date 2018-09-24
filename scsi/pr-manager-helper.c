/*
 * Persistent reservation manager that talks to qemu-pr-helper
 *
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This code is licensed under the LGPL v2.1 or later.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "scsi/constants.h"
#include "scsi/pr-manager.h"
#include "scsi/utils.h"
#include "io/channel.h"
#include "io/channel-socket.h"
#include "pr-helper.h"
#include "qapi/qapi-events-block.h"

#include <scsi/sg.h>

#define PR_MAX_RECONNECT_ATTEMPTS 5

#define TYPE_PR_MANAGER_HELPER "pr-manager-helper"

#define PR_MANAGER_HELPER(obj) \
     OBJECT_CHECK(PRManagerHelper, (obj), \
                  TYPE_PR_MANAGER_HELPER)

typedef struct PRManagerHelper {
    /* <private> */
    PRManager parent;

    char *path;

    QemuMutex lock;
    QIOChannel *ioc;
} PRManagerHelper;

static void pr_manager_send_status_changed_event(PRManagerHelper *pr_mgr)
{
    char *id = object_get_canonical_path_component(OBJECT(pr_mgr));

    if (id) {
        qapi_event_send_pr_manager_status_changed(id, !!pr_mgr->ioc);
        g_free(id);
    }
}

/* Called with lock held.  */
static int pr_manager_helper_read(PRManagerHelper *pr_mgr,
                                  void *buf, int sz, Error **errp)
{
    ssize_t r = qio_channel_read_all(pr_mgr->ioc, buf, sz, errp);

    if (r < 0) {
        object_unref(OBJECT(pr_mgr->ioc));
        pr_mgr->ioc = NULL;
        pr_manager_send_status_changed_event(pr_mgr);
        return -EINVAL;
    }

    return 0;
}

/* Called with lock held.  */
static int pr_manager_helper_write(PRManagerHelper *pr_mgr,
                                   int fd,
                                   const void *buf, int sz, Error **errp)
{
    size_t nfds = (fd != -1);
    while (sz > 0) {
        struct iovec iov;
        ssize_t n_written;

        iov.iov_base = (void *)buf;
        iov.iov_len = sz;
        n_written = qio_channel_writev_full(QIO_CHANNEL(pr_mgr->ioc), &iov, 1,
                                            nfds ? &fd : NULL, nfds, errp);

        if (n_written <= 0) {
            assert(n_written != QIO_CHANNEL_ERR_BLOCK);
            object_unref(OBJECT(pr_mgr->ioc));
            pr_mgr->ioc = NULL;
            pr_manager_send_status_changed_event(pr_mgr);
            return n_written < 0 ? -EINVAL : 0;
        }

        nfds = 0;
        buf += n_written;
        sz -= n_written;
    }

    return 0;
}

/* Called with lock held.  */
static int pr_manager_helper_initialize(PRManagerHelper *pr_mgr,
                                        Error **errp)
{
    char *path = g_strdup(pr_mgr->path);
    SocketAddress saddr = {
        .type = SOCKET_ADDRESS_TYPE_UNIX,
        .u.q_unix.path = path
    };
    QIOChannelSocket *sioc = qio_channel_socket_new();
    Error *local_err = NULL;

    uint32_t flags;
    int r;

    assert(!pr_mgr->ioc);
    qio_channel_set_name(QIO_CHANNEL(sioc), "pr-manager-helper");
    qio_channel_socket_connect_sync(sioc,
                                    &saddr,
                                    &local_err);
    g_free(path);
    if (local_err) {
        object_unref(OBJECT(sioc));
        error_propagate(errp, local_err);
        return -ENOTCONN;
    }

    qio_channel_set_delay(QIO_CHANNEL(sioc), false);
    pr_mgr->ioc = QIO_CHANNEL(sioc);

    /* A simple feature negotation protocol, even though there is
     * no optional feature right now.
     */
    r = pr_manager_helper_read(pr_mgr, &flags, sizeof(flags), errp);
    if (r < 0) {
        goto out_close;
    }

    flags = 0;
    r = pr_manager_helper_write(pr_mgr, -1, &flags, sizeof(flags), errp);
    if (r < 0) {
        goto out_close;
    }

    pr_manager_send_status_changed_event(pr_mgr);
    return 0;

out_close:
    object_unref(OBJECT(pr_mgr->ioc));
    pr_mgr->ioc = NULL;
    return r;
}

static int pr_manager_helper_run(PRManager *p,
                                 int fd, struct sg_io_hdr *io_hdr)
{
    PRManagerHelper *pr_mgr = PR_MANAGER_HELPER(p);

    uint32_t len;
    PRHelperResponse resp;
    int ret;
    int expected_dir;
    int attempts;
    uint8_t cdb[PR_HELPER_CDB_SIZE] = { 0 };

    if (!io_hdr->cmd_len || io_hdr->cmd_len > PR_HELPER_CDB_SIZE) {
        return -EINVAL;
    }

    memcpy(cdb, io_hdr->cmdp, io_hdr->cmd_len);
    assert(cdb[0] == PERSISTENT_RESERVE_OUT || cdb[0] == PERSISTENT_RESERVE_IN);
    expected_dir =
        (cdb[0] == PERSISTENT_RESERVE_OUT ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV);
    if (io_hdr->dxfer_direction != expected_dir) {
        return -EINVAL;
    }

    len = scsi_cdb_xfer(cdb);
    if (io_hdr->dxfer_len < len || len > PR_HELPER_DATA_SIZE) {
        return -EINVAL;
    }

    qemu_mutex_lock(&pr_mgr->lock);

    /* Try to reconnect while sending the CDB.  */
    for (attempts = 0; attempts < PR_MAX_RECONNECT_ATTEMPTS; attempts++) {
        if (!pr_mgr->ioc) {
            ret = pr_manager_helper_initialize(pr_mgr, NULL);
            if (ret < 0) {
                qemu_mutex_unlock(&pr_mgr->lock);
                g_usleep(G_USEC_PER_SEC);
                qemu_mutex_lock(&pr_mgr->lock);
                continue;
            }
        }

        ret = pr_manager_helper_write(pr_mgr, fd, cdb, ARRAY_SIZE(cdb), NULL);
        if (ret >= 0) {
            break;
        }
    }
    if (ret < 0) {
        goto out;
    }

    /* After sending the CDB, any communications failure causes the
     * command to fail.  The failure is transient, retrying the command
     * will invoke pr_manager_helper_initialize again.
     */
    if (expected_dir == SG_DXFER_TO_DEV) {
        io_hdr->resid = io_hdr->dxfer_len - len;
        ret = pr_manager_helper_write(pr_mgr, -1, io_hdr->dxferp, len, NULL);
        if (ret < 0) {
            goto out;
        }
    }
    ret = pr_manager_helper_read(pr_mgr, &resp, sizeof(resp), NULL);
    if (ret < 0) {
        goto out;
    }

    resp.result = be32_to_cpu(resp.result);
    resp.sz = be32_to_cpu(resp.sz);
    if (io_hdr->dxfer_direction == SG_DXFER_FROM_DEV) {
        assert(resp.sz <= io_hdr->dxfer_len);
        ret = pr_manager_helper_read(pr_mgr, io_hdr->dxferp, resp.sz, NULL);
        if (ret < 0) {
            goto out;
        }
        io_hdr->resid = io_hdr->dxfer_len - resp.sz;
    } else {
        assert(resp.sz == 0);
    }

    io_hdr->status = resp.result;
    if (resp.result == CHECK_CONDITION) {
        io_hdr->driver_status = SG_ERR_DRIVER_SENSE;
        io_hdr->sb_len_wr = MIN(io_hdr->mx_sb_len, PR_HELPER_SENSE_SIZE);
        memcpy(io_hdr->sbp, resp.sense, io_hdr->sb_len_wr);
    }

out:
    if (ret < 0) {
        int sense_len = scsi_build_sense(io_hdr->sbp,
                                         SENSE_CODE(LUN_COMM_FAILURE));
        io_hdr->driver_status = SG_ERR_DRIVER_SENSE;
        io_hdr->sb_len_wr = MIN(io_hdr->mx_sb_len, sense_len);
        io_hdr->status = CHECK_CONDITION;
    }
    qemu_mutex_unlock(&pr_mgr->lock);
    return ret;
}

static bool pr_manager_helper_is_connected(PRManager *p)
{
    PRManagerHelper *pr_mgr = PR_MANAGER_HELPER(p);
    bool result;

    qemu_mutex_lock(&pr_mgr->lock);
    result = (pr_mgr->ioc != NULL);
    qemu_mutex_unlock(&pr_mgr->lock);

    return result;
}

static void pr_manager_helper_complete(UserCreatable *uc, Error **errp)
{
    PRManagerHelper *pr_mgr = PR_MANAGER_HELPER(uc);

    qemu_mutex_lock(&pr_mgr->lock);
    pr_manager_helper_initialize(pr_mgr, errp);
    qemu_mutex_unlock(&pr_mgr->lock);
}

static char *get_path(Object *obj, Error **errp)
{
    PRManagerHelper *pr_mgr = PR_MANAGER_HELPER(obj);

    return g_strdup(pr_mgr->path);
}

static void set_path(Object *obj, const char *str, Error **errp)
{
    PRManagerHelper *pr_mgr = PR_MANAGER_HELPER(obj);

    g_free(pr_mgr->path);
    pr_mgr->path = g_strdup(str);
}

static void pr_manager_helper_instance_finalize(Object *obj)
{
    PRManagerHelper *pr_mgr = PR_MANAGER_HELPER(obj);

    object_unref(OBJECT(pr_mgr->ioc));
    qemu_mutex_destroy(&pr_mgr->lock);
}

static void pr_manager_helper_instance_init(Object *obj)
{
    PRManagerHelper *pr_mgr = PR_MANAGER_HELPER(obj);

    qemu_mutex_init(&pr_mgr->lock);
}

static void pr_manager_helper_class_init(ObjectClass *klass,
                                         void *class_data G_GNUC_UNUSED)
{
    PRManagerClass *prmgr_klass = PR_MANAGER_CLASS(klass);
    UserCreatableClass *uc_klass = USER_CREATABLE_CLASS(klass);

    object_class_property_add_str(klass, "path", get_path, set_path,
                                  &error_abort);
    uc_klass->complete = pr_manager_helper_complete;
    prmgr_klass->run = pr_manager_helper_run;
    prmgr_klass->is_connected = pr_manager_helper_is_connected;
}

static const TypeInfo pr_manager_helper_info = {
    .parent = TYPE_PR_MANAGER,
    .name = TYPE_PR_MANAGER_HELPER,
    .instance_size = sizeof(PRManagerHelper),
    .instance_init = pr_manager_helper_instance_init,
    .instance_finalize = pr_manager_helper_instance_finalize,
    .class_init = pr_manager_helper_class_init,
};

static void pr_manager_helper_register_types(void)
{
    type_register_static(&pr_manager_helper_info);
}

type_init(pr_manager_helper_register_types);
