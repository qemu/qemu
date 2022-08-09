/*
 * vhost-user-blk sample application
 *
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is based on the "vhost-user-scsi" sample and "virtio-blk" driver
 * implementation by:
 *  Felipe Franciosi <felipe@nutanix.com>
 *  Anthony Liguori <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_blk.h"
#include "libvhost-user-glib.h"

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

enum {
    VHOST_USER_BLK_MAX_QUEUES = 8,
};

struct virtio_blk_inhdr {
    unsigned char status;
};

/* vhost user block device */
typedef struct VubDev {
    VugDev parent;
    int blk_fd;
    struct virtio_blk_config blkcfg;
    bool enable_ro;
    char *blk_name;
    GMainLoop *loop;
} VubDev;

typedef struct VubReq {
    VuVirtqElement *elem;
    int64_t sector_num;
    size_t size;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr *out;
    VubDev *vdev_blk;
    struct VuVirtq *vq;
} VubReq;

/* refer util/iov.c */
static size_t vub_iov_size(const struct iovec *iov,
                              const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}

static size_t vub_iov_to_buf(const struct iovec *iov,
                             const unsigned int iov_cnt, void *buf)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        memcpy(buf + len,  iov[i].iov_base, iov[i].iov_len);
        len += iov[i].iov_len;
    }
    return len;
}

static void vub_panic_cb(VuDev *vu_dev, const char *buf)
{
    VugDev *gdev;
    VubDev *vdev_blk;

    assert(vu_dev);

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);
    if (buf) {
        g_warning("vu_panic: %s", buf);
    }

    g_main_loop_quit(vdev_blk->loop);
}

static void vub_req_complete(VubReq *req)
{
    VugDev *gdev = &req->vdev_blk->parent;
    VuDev *vu_dev = &gdev->parent;

    /* IO size with 1 extra status byte */
    vu_queue_push(vu_dev, req->vq, req->elem,
                  req->size + 1);
    vu_queue_notify(vu_dev, req->vq);

    g_free(req->elem);
    g_free(req);
}

static int vub_open(const char *file_name, bool wce)
{
    int fd;
    int flags = O_RDWR;

    if (!wce) {
        flags |= O_DIRECT;
    }

    fd = open(file_name, flags);
    if (fd < 0) {
        fprintf(stderr, "Cannot open file %s, %s\n", file_name,
                strerror(errno));
        return -1;
    }

    return fd;
}

static ssize_t
vub_readv(VubReq *req, struct iovec *iov, uint32_t iovcnt)
{
    VubDev *vdev_blk = req->vdev_blk;
    ssize_t rc;

    if (!iovcnt) {
        fprintf(stderr, "Invalid Read IOV count\n");
        return -1;
    }

    req->size = vub_iov_size(iov, iovcnt);
    rc = preadv(vdev_blk->blk_fd, iov, iovcnt, req->sector_num * 512);
    if (rc < 0) {
        fprintf(stderr, "%s, Sector %"PRIu64", Size %zu failed with %s\n",
                vdev_blk->blk_name, req->sector_num, req->size,
                strerror(errno));
        return -1;
    }

    return rc;
}

static ssize_t
vub_writev(VubReq *req, struct iovec *iov, uint32_t iovcnt)
{
    VubDev *vdev_blk = req->vdev_blk;
    ssize_t rc;

    if (!iovcnt) {
        fprintf(stderr, "Invalid Write IOV count\n");
        return -1;
    }

    req->size = vub_iov_size(iov, iovcnt);
    rc = pwritev(vdev_blk->blk_fd, iov, iovcnt, req->sector_num * 512);
    if (rc < 0) {
        fprintf(stderr, "%s, Sector %"PRIu64", Size %zu failed with %s\n",
                vdev_blk->blk_name, req->sector_num, req->size,
                strerror(errno));
        return -1;
    }

    return rc;
}

static int
vub_discard_write_zeroes(VubReq *req, struct iovec *iov, uint32_t iovcnt,
                         uint32_t type)
{
    struct virtio_blk_discard_write_zeroes *desc;
    ssize_t size;
    void *buf;

    size = vub_iov_size(iov, iovcnt);
    if (size != sizeof(*desc)) {
        fprintf(stderr, "Invalid size %zd, expect %zd\n", size, sizeof(*desc));
        return -1;
    }
    buf = g_new0(char, size);
    vub_iov_to_buf(iov, iovcnt, buf);

    #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
    VubDev *vdev_blk = req->vdev_blk;
    desc = (struct virtio_blk_discard_write_zeroes *)buf;
    uint64_t range[2] = { le64toh(desc->sector) << 9,
                          le32toh(desc->num_sectors) << 9 };
    if (type == VIRTIO_BLK_T_DISCARD) {
        if (ioctl(vdev_blk->blk_fd, BLKDISCARD, range) == 0) {
            g_free(buf);
            return 0;
        }
    } else if (type == VIRTIO_BLK_T_WRITE_ZEROES) {
        if (ioctl(vdev_blk->blk_fd, BLKZEROOUT, range) == 0) {
            g_free(buf);
            return 0;
        }
    }
    #endif

    g_free(buf);
    return -1;
}

static void
vub_flush(VubReq *req)
{
    VubDev *vdev_blk = req->vdev_blk;

    fdatasync(vdev_blk->blk_fd);
}

static int vub_virtio_process_req(VubDev *vdev_blk,
                                     VuVirtq *vq)
{
    VugDev *gdev = &vdev_blk->parent;
    VuDev *vu_dev = &gdev->parent;
    VuVirtqElement *elem;
    uint32_t type;
    unsigned in_num;
    unsigned out_num;
    VubReq *req;

    elem = vu_queue_pop(vu_dev, vq, sizeof(VuVirtqElement) + sizeof(VubReq));
    if (!elem) {
        return -1;
    }

    /* refer to hw/block/virtio_blk.c */
    if (elem->out_num < 1 || elem->in_num < 1) {
        fprintf(stderr, "virtio-blk request missing headers\n");
        g_free(elem);
        return -1;
    }

    req = g_new0(VubReq, 1);
    req->vdev_blk = vdev_blk;
    req->vq = vq;
    req->elem = elem;

    in_num = elem->in_num;
    out_num = elem->out_num;

    /* don't support VIRTIO_F_ANY_LAYOUT and virtio 1.0 only */
    if (elem->out_sg[0].iov_len < sizeof(struct virtio_blk_outhdr)) {
        fprintf(stderr, "Invalid outhdr size\n");
        goto err;
    }
    req->out = (struct virtio_blk_outhdr *)elem->out_sg[0].iov_base;
    out_num--;

    if (elem->in_sg[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        fprintf(stderr, "Invalid inhdr size\n");
        goto err;
    }
    req->in = (struct virtio_blk_inhdr *)elem->in_sg[in_num - 1].iov_base;
    in_num--;

    type = le32toh(req->out->type);
    switch (type & ~VIRTIO_BLK_T_BARRIER) {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT: {
        ssize_t ret = 0;
        bool is_write = type & VIRTIO_BLK_T_OUT;
        req->sector_num = le64toh(req->out->sector);
        if (is_write) {
            ret  = vub_writev(req, &elem->out_sg[1], out_num);
        } else {
            ret = vub_readv(req, &elem->in_sg[0], in_num);
        }
        if (ret >= 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        vub_req_complete(req);
        break;
    }
    case VIRTIO_BLK_T_FLUSH:
        vub_flush(req);
        req->in->status = VIRTIO_BLK_S_OK;
        vub_req_complete(req);
        break;
    case VIRTIO_BLK_T_GET_ID: {
        size_t size = MIN(vub_iov_size(&elem->in_sg[0], in_num),
                          VIRTIO_BLK_ID_BYTES);
        snprintf(elem->in_sg[0].iov_base, size, "%s", "vhost_user_blk");
        req->in->status = VIRTIO_BLK_S_OK;
        req->size = elem->in_sg[0].iov_len;
        vub_req_complete(req);
        break;
    }
    case VIRTIO_BLK_T_DISCARD:
    case VIRTIO_BLK_T_WRITE_ZEROES: {
        int rc;
        rc = vub_discard_write_zeroes(req, &elem->out_sg[1], out_num, type);
        if (rc == 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        vub_req_complete(req);
        break;
    }
    default:
        req->in->status = VIRTIO_BLK_S_UNSUPP;
        vub_req_complete(req);
        break;
    }

    return 0;

err:
    g_free(elem);
    g_free(req);
    return -1;
}

static void vub_process_vq(VuDev *vu_dev, int idx)
{
    VugDev *gdev;
    VubDev *vdev_blk;
    VuVirtq *vq;
    int ret;

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);
    assert(vdev_blk);

    vq = vu_get_queue(vu_dev, idx);
    assert(vq);

    while (1) {
        ret = vub_virtio_process_req(vdev_blk, vq);
        if (ret) {
            break;
        }
    }
}

static void vub_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VuVirtq *vq;

    assert(vu_dev);

    vq = vu_get_queue(vu_dev, idx);
    vu_set_queue_handler(vu_dev, vq, started ? vub_process_vq : NULL);
}

static uint64_t
vub_get_features(VuDev *dev)
{
    uint64_t features;
    VugDev *gdev;
    VubDev *vdev_blk;

    gdev = container_of(dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);

    features = 1ull << VIRTIO_BLK_F_SIZE_MAX |
               1ull << VIRTIO_BLK_F_SEG_MAX |
               1ull << VIRTIO_BLK_F_TOPOLOGY |
               1ull << VIRTIO_BLK_F_BLK_SIZE |
               1ull << VIRTIO_BLK_F_FLUSH |
               #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
               1ull << VIRTIO_BLK_F_DISCARD |
               1ull << VIRTIO_BLK_F_WRITE_ZEROES |
               #endif
               1ull << VIRTIO_BLK_F_CONFIG_WCE;

    if (vdev_blk->enable_ro) {
        features |= 1ull << VIRTIO_BLK_F_RO;
    }

    return features;
}

static uint64_t
vub_get_protocol_features(VuDev *dev)
{
    return 1ull << VHOST_USER_PROTOCOL_F_CONFIG |
           1ull << VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD;
}

static int
vub_get_config(VuDev *vu_dev, uint8_t *config, uint32_t len)
{
    VugDev *gdev;
    VubDev *vdev_blk;

    if (len > sizeof(struct virtio_blk_config)) {
        return -1;
    }

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);
    memcpy(config, &vdev_blk->blkcfg, len);

    return 0;
}

static int
vub_set_config(VuDev *vu_dev, const uint8_t *data,
               uint32_t offset, uint32_t size, uint32_t flags)
{
    VugDev *gdev;
    VubDev *vdev_blk;
    uint8_t wce;
    int fd;

    /* don't support live migration */
    if (flags != VHOST_SET_CONFIG_TYPE_MASTER) {
        return -1;
    }

    gdev = container_of(vu_dev, VugDev, parent);
    vdev_blk = container_of(gdev, VubDev, parent);

    if (offset != offsetof(struct virtio_blk_config, wce) ||
        size != 1) {
        return -1;
    }

    wce = *data;
    if (wce == vdev_blk->blkcfg.wce) {
        /* Do nothing as same with old configuration */
        return 0;
    }

    vdev_blk->blkcfg.wce = wce;
    fprintf(stdout, "Write Cache Policy Changed\n");
    if (vdev_blk->blk_fd >= 0) {
        close(vdev_blk->blk_fd);
        vdev_blk->blk_fd = -1;
    }

    fd = vub_open(vdev_blk->blk_name, wce);
    if (fd < 0) {
        fprintf(stderr, "Error to open block device %s\n", vdev_blk->blk_name);
        vdev_blk->blk_fd = -1;
        return -1;
    }
    vdev_blk->blk_fd = fd;

    return 0;
}

static const VuDevIface vub_iface = {
    .get_features = vub_get_features,
    .queue_set_started = vub_queue_set_started,
    .get_protocol_features = vub_get_protocol_features,
    .get_config = vub_get_config,
    .set_config = vub_set_config,
};

static int unix_sock_new(char *unix_fn)
{
    int sock;
    struct sockaddr_un un;
    size_t len;

    assert(unix_fn);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
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

static void vub_free(struct VubDev *vdev_blk)
{
    if (!vdev_blk) {
        return;
    }

    g_main_loop_unref(vdev_blk->loop);
    if (vdev_blk->blk_fd >= 0) {
        close(vdev_blk->blk_fd);
    }
    g_free(vdev_blk);
}

static uint32_t
vub_get_blocksize(int fd)
{
    uint32_t blocksize = 512;

#if defined(__linux__) && defined(BLKSSZGET)
    if (ioctl(fd, BLKSSZGET, &blocksize) == 0) {
        return blocksize;
    }
#endif

    return blocksize;
}

static void
vub_initialize_config(int fd, struct virtio_blk_config *config)
{
    off64_t capacity;

    capacity = lseek64(fd, 0, SEEK_END);
    config->capacity = capacity >> 9;
    config->blk_size = vub_get_blocksize(fd);
    config->size_max = 65536;
    config->seg_max = 128 - 2;
    config->min_io_size = 1;
    config->opt_io_size = 1;
    config->num_queues = 1;
    #if defined(__linux__) && defined(BLKDISCARD) && defined(BLKZEROOUT)
    config->max_discard_sectors = 32768;
    config->max_discard_seg = 1;
    config->discard_sector_alignment = config->blk_size >> 9;
    config->max_write_zeroes_sectors = 32768;
    config->max_write_zeroes_seg = 1;
    #endif
}

static VubDev *
vub_new(char *blk_file)
{
    VubDev *vdev_blk;

    vdev_blk = g_new0(VubDev, 1);
    vdev_blk->loop = g_main_loop_new(NULL, FALSE);
    vdev_blk->blk_fd = vub_open(blk_file, 0);
    if (vdev_blk->blk_fd  < 0) {
        fprintf(stderr, "Error to open block device %s\n", blk_file);
        vub_free(vdev_blk);
        return NULL;
    }
    vdev_blk->enable_ro = false;
    vdev_blk->blkcfg.wce = 0;
    vdev_blk->blk_name = blk_file;

    /* fill virtio_blk_config with block parameters */
    vub_initialize_config(vdev_blk->blk_fd, &vdev_blk->blkcfg);

    return vdev_blk;
}

static int opt_fdnum = -1;
static char *opt_socket_path;
static char *opt_blk_file;
static gboolean opt_print_caps;
static gboolean opt_read_only;

static GOptionEntry entries[] = {
    { "print-capabilities", 'c', 0, G_OPTION_ARG_NONE, &opt_print_caps,
      "Print capabilities", NULL },
    { "fd", 'f', 0, G_OPTION_ARG_INT, &opt_fdnum,
      "Use inherited fd socket", "FDNUM" },
    { "socket-path", 's', 0, G_OPTION_ARG_FILENAME, &opt_socket_path,
      "Use UNIX socket path", "PATH" },
    {"blk-file", 'b', 0, G_OPTION_ARG_FILENAME, &opt_blk_file,
     "block device or file path", "PATH"},
    { "read-only", 'r', 0, G_OPTION_ARG_NONE, &opt_read_only,
      "Enable read-only", NULL },
    { NULL, },
};

int main(int argc, char **argv)
{
    int lsock = -1, csock = -1;
    VubDev *vdev_blk = NULL;
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("Option parsing failed: %s\n", error->message);
        exit(EXIT_FAILURE);
    }
    if (opt_print_caps) {
        g_print("{\n");
        g_print("  \"type\": \"block\",\n");
        g_print("  \"features\": [\n");
        g_print("    \"read-only\",\n");
        g_print("    \"blk-file\"\n");
        g_print("  ]\n");
        g_print("}\n");
        exit(EXIT_SUCCESS);
    }

    if (!opt_blk_file) {
        g_print("%s\n", g_option_context_get_help(context, true, NULL));
        exit(EXIT_FAILURE);
    }

    if (opt_socket_path) {
        lsock = unix_sock_new(opt_socket_path);
        if (lsock < 0) {
            exit(EXIT_FAILURE);
        }
    } else if (opt_fdnum < 0) {
        g_print("%s\n", g_option_context_get_help(context, true, NULL));
        exit(EXIT_FAILURE);
    } else {
        lsock = opt_fdnum;
    }

    csock = accept(lsock, NULL, NULL);
    if (csock < 0) {
        g_printerr("Accept error %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    vdev_blk = vub_new(opt_blk_file);
    if (!vdev_blk) {
        exit(EXIT_FAILURE);
    }
    if (opt_read_only) {
        vdev_blk->enable_ro = true;
    }

    if (!vug_init(&vdev_blk->parent, VHOST_USER_BLK_MAX_QUEUES, csock,
                  vub_panic_cb, &vub_iface)) {
        g_printerr("Failed to initialize libvhost-user-glib\n");
        exit(EXIT_FAILURE);
    }

    g_main_loop_run(vdev_blk->loop);
    g_main_loop_unref(vdev_blk->loop);
    g_option_context_free(context);
    vug_deinit(&vdev_blk->parent);
    vub_free(vdev_blk);
    if (csock >= 0) {
        close(csock);
    }
    if (lsock >= 0) {
        close(lsock);
    }
    g_free(opt_socket_path);
    g_free(opt_blk_file);

    return 0;
}
