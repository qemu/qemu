/*
 * vhost-user-scsi sample application
 *
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 *
 * Author:
 *  Felipe Franciosi <felipe@nutanix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <iscsi/iscsi.h>
#include <iscsi/scsi-lowlevel.h>
#include "contrib/libvhost-user/libvhost-user-glib.h"
#include "standard-headers/linux/virtio_scsi.h"


#define VUS_ISCSI_INITIATOR "iqn.2016-11.com.nutanix:vhost-user-scsi"

typedef struct VusIscsiLun {
    struct iscsi_context *iscsi_ctx;
    int iscsi_lun;
} VusIscsiLun;

typedef struct VusDev {
    VugDev parent;

    VusIscsiLun lun;
    GMainLoop *loop;
} VusDev;

/** libiscsi integration **/

typedef struct virtio_scsi_cmd_req VirtIOSCSICmdReq;
typedef struct virtio_scsi_cmd_resp VirtIOSCSICmdResp;

static int vus_iscsi_add_lun(VusIscsiLun *lun, char *iscsi_uri)
{
    struct iscsi_url *iscsi_url;
    struct iscsi_context *iscsi_ctx;
    int ret = 0;

    assert(lun);
    assert(iscsi_uri);
    assert(!lun->iscsi_ctx);

    iscsi_ctx = iscsi_create_context(VUS_ISCSI_INITIATOR);
    if (!iscsi_ctx) {
        g_warning("Unable to create iSCSI context");
        return -1;
    }

    iscsi_url = iscsi_parse_full_url(iscsi_ctx, iscsi_uri);
    if (!iscsi_url) {
        g_warning("Unable to parse iSCSI URL: %s", iscsi_get_error(iscsi_ctx));
        goto fail;
    }

    iscsi_set_session_type(iscsi_ctx, ISCSI_SESSION_NORMAL);
    iscsi_set_header_digest(iscsi_ctx, ISCSI_HEADER_DIGEST_NONE_CRC32C);
    if (iscsi_full_connect_sync(iscsi_ctx, iscsi_url->portal, iscsi_url->lun)) {
        g_warning("Unable to login to iSCSI portal: %s",
                  iscsi_get_error(iscsi_ctx));
        goto fail;
    }

    lun->iscsi_ctx = iscsi_ctx;
    lun->iscsi_lun = iscsi_url->lun;

    g_debug("Context %p created for lun 0: %s", iscsi_ctx, iscsi_uri);

out:
    if (iscsi_url) {
        iscsi_destroy_url(iscsi_url);
    }
    return ret;

fail:
    (void)iscsi_destroy_context(iscsi_ctx);
    ret = -1;
    goto out;
}

static struct scsi_task *scsi_task_new(int cdb_len, uint8_t *cdb, int dir,
                                       int xfer_len)
{
    struct scsi_task *task;

    assert(cdb_len > 0);
    assert(cdb);

    task = g_new0(struct scsi_task, 1);
    memcpy(task->cdb, cdb, cdb_len);
    task->cdb_size = cdb_len;
    task->xfer_dir = dir;
    task->expxferlen = xfer_len;

    return task;
}

static int get_cdb_len(uint8_t *cdb)
{
    assert(cdb);

    switch (cdb[0] >> 5) {
    case 0: return 6;
    case 1: /* fall through */
    case 2: return 10;
    case 4: return 16;
    case 5: return 12;
    }
    g_warning("Unable to determine cdb len (0x%02hhX)", cdb[0] >> 5);
    return -1;
}

static int handle_cmd_sync(struct iscsi_context *ctx,
                           VirtIOSCSICmdReq *req,
                           struct iovec *out, unsigned int out_len,
                           VirtIOSCSICmdResp *rsp,
                           struct iovec *in, unsigned int in_len)
{
    struct scsi_task *task;
    uint32_t dir;
    uint32_t len;
    int cdb_len;
    int i;

    assert(ctx);
    assert(req);
    assert(rsp);

    if (!(!req->lun[1] && req->lun[2] == 0x40 && !req->lun[3])) {
        /* Ignore anything different than target=0, lun=0 */
        g_debug("Ignoring unconnected lun (0x%hhX, 0x%hhX)",
             req->lun[1], req->lun[3]);
        rsp->status = SCSI_STATUS_CHECK_CONDITION;
        memset(rsp->sense, 0, sizeof(rsp->sense));
        rsp->sense_len = 18;
        rsp->sense[0] = 0x70;
        rsp->sense[2] = SCSI_SENSE_ILLEGAL_REQUEST;
        rsp->sense[7] = 10;
        rsp->sense[12] = 0x24;

        return 0;
    }

    cdb_len = get_cdb_len(req->cdb);
    if (cdb_len == -1) {
        return -1;
    }

    len = 0;
    if (!out_len && !in_len) {
        dir = SCSI_XFER_NONE;
    } else if (out_len) {
        dir = SCSI_XFER_WRITE;
        for (i = 0; i < out_len; i++) {
            len += out[i].iov_len;
        }
    } else {
        dir = SCSI_XFER_READ;
        for (i = 0; i < in_len; i++) {
            len += in[i].iov_len;
        }
    }

    task = scsi_task_new(cdb_len, req->cdb, dir, len);

    if (dir == SCSI_XFER_WRITE) {
        task->iovector_out.iov = (struct scsi_iovec *)out;
        task->iovector_out.niov = out_len;
    } else if (dir == SCSI_XFER_READ) {
        task->iovector_in.iov = (struct scsi_iovec *)in;
        task->iovector_in.niov = in_len;
    }

    g_debug("Sending iscsi cmd (cdb_len=%d, dir=%d, task=%p)",
         cdb_len, dir, task);
    if (!iscsi_scsi_command_sync(ctx, 0, task, NULL)) {
        g_warning("Error serving SCSI command");
        g_free(task);
        return -1;
    }

    memset(rsp, 0, sizeof(*rsp));

    rsp->status = task->status;
    rsp->resid  = task->residual;

    if (task->status == SCSI_STATUS_CHECK_CONDITION) {
        rsp->response = VIRTIO_SCSI_S_FAILURE;
        rsp->sense_len = task->datain.size - 2;
        memcpy(rsp->sense, &task->datain.data[2], rsp->sense_len);
    }

    g_free(task);

    g_debug("Filled in rsp: status=%hhX, resid=%u, response=%hhX, sense_len=%u",
         rsp->status, rsp->resid, rsp->response, rsp->sense_len);

    return 0;
}

/** libvhost-user callbacks **/

static void vus_panic_cb(VuDev *vu_dev, const char *buf)
{
    VugDev *gdev;
    VusDev *vdev_scsi;

    assert(vu_dev);

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_scsi = container_of(gdev, VusDev, parent);
    if (buf) {
        g_warning("vu_panic: %s", buf);
    }

    g_main_loop_quit(vdev_scsi->loop);
}

static void vus_proc_req(VuDev *vu_dev, int idx)
{
    VugDev *gdev;
    VusDev *vdev_scsi;
    VuVirtq *vq;

    assert(vu_dev);

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_scsi = container_of(gdev, VusDev, parent);
    if (idx < 0 || idx >= VHOST_MAX_NR_VIRTQUEUE) {
        g_warning("VQ Index out of range: %d", idx);
        vus_panic_cb(vu_dev, NULL);
        return;
    }

    vq = vu_get_queue(vu_dev, idx);
    if (!vq) {
        g_warning("Error fetching VQ (dev=%p, idx=%d)", vu_dev, idx);
        vus_panic_cb(vu_dev, NULL);
        return;
    }

    g_debug("Got kicked on vq[%d]@%p", idx, vq);

    while (1) {
        VuVirtqElement *elem;
        VirtIOSCSICmdReq *req;
        VirtIOSCSICmdResp *rsp;

        elem = vu_queue_pop(vu_dev, vq, sizeof(VuVirtqElement));
        if (!elem) {
            g_debug("No more elements pending on vq[%d]@%p", idx, vq);
            break;
        }
        g_debug("Popped elem@%p", elem);

        assert(!(elem->out_num > 1 && elem->in_num > 1));
        assert(elem->out_num > 0 && elem->in_num > 0);

        if (elem->out_sg[0].iov_len < sizeof(VirtIOSCSICmdReq)) {
            g_warning("Invalid virtio-scsi req header");
            vus_panic_cb(vu_dev, NULL);
            break;
        }
        req = (VirtIOSCSICmdReq *)elem->out_sg[0].iov_base;

        if (elem->in_sg[0].iov_len < sizeof(VirtIOSCSICmdResp)) {
            g_warning("Invalid virtio-scsi rsp header");
            vus_panic_cb(vu_dev, NULL);
            break;
        }
        rsp = (VirtIOSCSICmdResp *)elem->in_sg[0].iov_base;

        if (handle_cmd_sync(vdev_scsi->lun.iscsi_ctx,
                            req, &elem->out_sg[1], elem->out_num - 1,
                            rsp, &elem->in_sg[1], elem->in_num - 1) != 0) {
            vus_panic_cb(vu_dev, NULL);
            break;
        }

        vu_queue_push(vu_dev, vq, elem, 0);
        vu_queue_notify(vu_dev, vq);

        free(elem);
    }
}

static void vus_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VuVirtq *vq;

    assert(vu_dev);

    if (idx < 0 || idx >= VHOST_MAX_NR_VIRTQUEUE) {
        g_warning("VQ Index out of range: %d", idx);
        vus_panic_cb(vu_dev, NULL);
        return;
    }

    vq = vu_get_queue(vu_dev, idx);

    if (idx == 0 || idx == 1) {
        g_debug("queue %d unimplemented", idx);
    } else {
        vu_set_queue_handler(vu_dev, vq, started ? vus_proc_req : NULL);
    }
}

static const VuDevIface vus_iface = {
    .queue_set_started = vus_queue_set_started,
};

/** misc helpers **/

static int unix_sock_new(char *unix_fn)
{
    int sock;
    struct sockaddr_un un;
    size_t len;

    assert(unix_fn);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock <= 0) {
        perror("socket");
        return -1;
    }

    un.sun_family = AF_UNIX;
    (void)snprintf(un.sun_path, sizeof(un.sun_path), "%s", unix_fn);
    len = sizeof(un.sun_family) + strlen(un.sun_path);

    (void)unlink(unix_fn);
    if (bind(sock, (struct sockaddr *)&un, len) < 0) {
        perror("bind");
        goto fail;
    }

    if (listen(sock, 1) < 0) {
        perror("listen");
        goto fail;
    }

    return sock;

fail:
    (void)close(sock);

    return -1;
}

/** vhost-user-scsi **/

int main(int argc, char **argv)
{
    VusDev *vdev_scsi = NULL;
    char *unix_fn = NULL;
    char *iscsi_uri = NULL;
    int lsock = -1, csock = -1, opt, err = EXIT_SUCCESS;

    while ((opt = getopt(argc, argv, "u:i:")) != -1) {
        switch (opt) {
        case 'h':
            goto help;
        case 'u':
            unix_fn = g_strdup(optarg);
            break;
        case 'i':
            iscsi_uri = g_strdup(optarg);
            break;
        default:
            goto help;
        }
    }
    if (!unix_fn || !iscsi_uri) {
        goto help;
    }

    lsock = unix_sock_new(unix_fn);
    if (lsock < 0) {
        goto err;
    }

    csock = accept(lsock, NULL, NULL);
    if (csock < 0) {
        perror("accept");
        goto err;
    }

    vdev_scsi = g_new0(VusDev, 1);
    vdev_scsi->loop = g_main_loop_new(NULL, FALSE);

    if (vus_iscsi_add_lun(&vdev_scsi->lun, iscsi_uri) != 0) {
        goto err;
    }

    vug_init(&vdev_scsi->parent, csock, vus_panic_cb, &vus_iface);

    g_main_loop_run(vdev_scsi->loop);

    vug_deinit(&vdev_scsi->parent);

out:
    if (vdev_scsi) {
        g_main_loop_unref(vdev_scsi->loop);
        g_free(vdev_scsi);
        unlink(unix_fn);
    }
    if (csock >= 0) {
        close(csock);
    }
    if (lsock >= 0) {
        close(lsock);
    }
    g_free(unix_fn);
    g_free(iscsi_uri);

    return err;

err:
    err = EXIT_FAILURE;
    goto out;

help:
    fprintf(stderr, "Usage: %s [ -u unix_sock_path -i iscsi_uri ] | [ -h ]\n",
            argv[0]);
    fprintf(stderr, "          -u path to unix socket\n");
    fprintf(stderr, "          -i iscsi uri for lun 0\n");
    fprintf(stderr, "          -h print help and quit\n");

    goto err;
}
