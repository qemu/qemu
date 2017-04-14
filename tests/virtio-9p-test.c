/*
 * QTest testcase for VirtIO 9P
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu-common.h"
#include "libqos/libqos-pc.h"
#include "libqos/libqos-spapr.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_pci.h"
#include "hw/9pfs/9p.h"

static const char mount_tag[] = "qtest";

typedef struct {
    QVirtioDevice *dev;
    QOSState *qs;
    QVirtQueue *vq;
    char *test_share;
    uint16_t p9_req_tag;
} QVirtIO9P;

static QVirtIO9P *qvirtio_9p_start(const char *driver)
{
    const char *arch = qtest_get_arch();
    const char *cmd = "-fsdev local,id=fsdev0,security_model=none,path=%s "
                      "-device %s,fsdev=fsdev0,mount_tag=%s";
    QVirtIO9P *v9p = g_new0(QVirtIO9P, 1);

    v9p->test_share = g_strdup("/tmp/qtest.XXXXXX");
    g_assert_nonnull(mkdtemp(v9p->test_share));

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        v9p->qs = qtest_pc_boot(cmd, v9p->test_share, driver, mount_tag);
    } else if (strcmp(arch, "ppc64") == 0) {
        v9p->qs = qtest_spapr_boot(cmd, v9p->test_share, driver, mount_tag);
    } else {
        g_printerr("virtio-9p tests are only available on x86 or ppc64\n");
        exit(EXIT_FAILURE);
    }

    return v9p;
}

static void qvirtio_9p_stop(QVirtIO9P *v9p)
{
    qtest_shutdown(v9p->qs);
    rmdir(v9p->test_share);
    g_free(v9p->test_share);
    g_free(v9p);
}

static QVirtIO9P *qvirtio_9p_pci_start(void)
{
    QVirtIO9P *v9p = qvirtio_9p_start("virtio-9p-pci");
    QVirtioPCIDevice *dev = qvirtio_pci_device_find(v9p->qs->pcibus,
                                                    VIRTIO_ID_9P);
    g_assert_nonnull(dev);
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_9P);
    v9p->dev = (QVirtioDevice *) dev;

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(v9p->dev);
    qvirtio_set_acknowledge(v9p->dev);
    qvirtio_set_driver(v9p->dev);

    v9p->vq = qvirtqueue_setup(v9p->dev, v9p->qs->alloc, 0);
    return v9p;
}

static void qvirtio_9p_pci_stop(QVirtIO9P *v9p)
{
    qvirtqueue_cleanup(v9p->dev->bus, v9p->vq, v9p->qs->alloc);
    qvirtio_pci_device_disable(container_of(v9p->dev, QVirtioPCIDevice, vdev));
    qvirtio_pci_device_free((QVirtioPCIDevice *)v9p->dev);
    qvirtio_9p_stop(v9p);
}

static void pci_config(QVirtIO9P *v9p)
{
    size_t tag_len = qvirtio_config_readw(v9p->dev, 0);
    char *tag;
    int i;

    g_assert_cmpint(tag_len, ==, strlen(mount_tag));

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(v9p->dev, i + 2);
    }
    g_assert_cmpmem(tag, tag_len, mount_tag, tag_len);
    g_free(tag);
}

#define P9_MAX_SIZE 4096 /* Max size of a T-message or R-message */

typedef struct {
    QVirtIO9P *v9p;
    uint16_t tag;
    uint64_t t_msg;
    uint32_t t_size;
    uint64_t r_msg;
    /* No r_size, it is hardcoded to P9_MAX_SIZE */
    size_t t_off;
    size_t r_off;
} P9Req;

static void v9fs_memwrite(P9Req *req, const void *addr, size_t len)
{
    memwrite(req->t_msg + req->t_off, addr, len);
    req->t_off += len;
}

static void v9fs_memskip(P9Req *req, size_t len)
{
    req->r_off += len;
}

static void v9fs_memrewind(P9Req *req, size_t len)
{
    req->r_off -= len;
}

static void v9fs_memread(P9Req *req, void *addr, size_t len)
{
    memread(req->r_msg + req->r_off, addr, len);
    req->r_off += len;
}

static void v9fs_uint16_write(P9Req *req, uint16_t val)
{
    uint16_t le_val = cpu_to_le16(val);

    v9fs_memwrite(req, &le_val, 2);
}

static void v9fs_uint16_read(P9Req *req, uint16_t *val)
{
    v9fs_memread(req, val, 2);
    le16_to_cpus(val);
}

static void v9fs_uint32_write(P9Req *req, uint32_t val)
{
    uint32_t le_val = cpu_to_le32(val);

    v9fs_memwrite(req, &le_val, 4);
}

static void v9fs_uint32_read(P9Req *req, uint32_t *val)
{
    v9fs_memread(req, val, 4);
    le32_to_cpus(val);
}

/* len[2] string[len] */
static uint16_t v9fs_string_size(const char *string)
{
    size_t len = strlen(string);

    g_assert_cmpint(len, <=, UINT16_MAX);

    return 2 + len;
}

static void v9fs_string_write(P9Req *req, const char *string)
{
    int len = strlen(string);

    g_assert_cmpint(len, <=, UINT16_MAX);

    v9fs_uint16_write(req, (uint16_t) len);
    v9fs_memwrite(req, string, len);
}

static void v9fs_string_read(P9Req *req, uint16_t *len, char **string)
{
    uint16_t local_len;

    v9fs_uint16_read(req, &local_len);
    if (len) {
        *len = local_len;
    }
    if (string) {
        *string = g_malloc(local_len);
        v9fs_memread(req, *string, local_len);
    } else {
        v9fs_memskip(req, local_len);
    }
}

 typedef struct {
    uint32_t size;
    uint8_t id;
    uint16_t tag;
} QEMU_PACKED P9Hdr;

static P9Req *v9fs_req_init(QVirtIO9P *v9p, uint32_t size, uint8_t id,
                            uint16_t tag)
{
    P9Req *req = g_new0(P9Req, 1);
    uint32_t t_size = 7 + size; /* 9P header has well-known size of 7 bytes */
    P9Hdr hdr = {
        .size = cpu_to_le32(t_size),
        .id = id,
        .tag = cpu_to_le16(tag)
    };

    g_assert_cmpint(t_size, <=, P9_MAX_SIZE);

    req->v9p = v9p;
    req->t_size = t_size;
    req->t_msg = guest_alloc(v9p->qs->alloc, req->t_size);
    v9fs_memwrite(req, &hdr, 7);
    req->tag = tag;
    return req;
}

static void v9fs_req_send(P9Req *req)
{
    QVirtIO9P *v9p = req->v9p;
    uint32_t free_head;

    req->r_msg = guest_alloc(v9p->qs->alloc, P9_MAX_SIZE);
    free_head = qvirtqueue_add(v9p->vq, req->t_msg, req->t_size, false, true);
    qvirtqueue_add(v9p->vq, req->r_msg, P9_MAX_SIZE, true, false);
    qvirtqueue_kick(v9p->dev, v9p->vq, free_head);
    req->t_off = 0;
}

static const char *rmessage_name(uint8_t id)
{
    return
        id == P9_RLERROR ? "RLERROR" :
        id == P9_RVERSION ? "RVERSION" :
        id == P9_RATTACH ? "RATTACH" :
        id == P9_RWALK ? "RWALK" :
        "<unknown>";
}

static void v9fs_req_recv(P9Req *req, uint8_t id)
{
    QVirtIO9P *v9p = req->v9p;
    P9Hdr hdr;
    int i;

    for (i = 0; i < 10; i++) {
        qvirtio_wait_queue_isr(v9p->dev, v9p->vq, 1000 * 1000);

        v9fs_memread(req, &hdr, 7);
        hdr.size = ldl_le_p(&hdr.size);
        hdr.tag = lduw_le_p(&hdr.tag);
        if (hdr.size >= 7) {
            break;
        }
        v9fs_memrewind(req, 7);
    }

    g_assert_cmpint(hdr.size, >=, 7);
    g_assert_cmpint(hdr.size, <=, P9_MAX_SIZE);
    g_assert_cmpint(hdr.tag, ==, req->tag);

    if (hdr.id != id) {
        g_printerr("Received response %d (%s) instead of %d (%s)\n",
                   hdr.id, rmessage_name(hdr.id), id, rmessage_name(id));

        if (hdr.id == P9_RLERROR) {
            uint32_t err;
            v9fs_uint32_read(req, &err);
            g_printerr("Rlerror has errno %d (%s)\n", err, strerror(err));
        }
    }
    g_assert_cmpint(hdr.id, ==, id);
}

static void v9fs_req_free(P9Req *req)
{
    QVirtIO9P *v9p = req->v9p;

    guest_free(v9p->qs->alloc, req->t_msg);
    guest_free(v9p->qs->alloc, req->r_msg);
    g_free(req);
}

/* size[4] Rlerror tag[2] ecode[4] */
static void v9fs_rlerror(P9Req *req, uint32_t *err)
{
    v9fs_req_recv(req, P9_RLERROR);
    v9fs_uint32_read(req, err);
    v9fs_req_free(req);
}

/* size[4] Tversion tag[2] msize[4] version[s] */
static P9Req *v9fs_tversion(QVirtIO9P *v9p, uint32_t msize, const char *version)
{
    P9Req *req = v9fs_req_init(v9p, 4 + v9fs_string_size(version), P9_TVERSION,
                               P9_NOTAG);

    v9fs_uint32_write(req, msize);
    v9fs_string_write(req, version);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rversion tag[2] msize[4] version[s] */
static void v9fs_rversion(P9Req *req, uint16_t *len, char **version)
{
    uint32_t msize;

    v9fs_req_recv(req, P9_RVERSION);
    v9fs_uint32_read(req, &msize);

    g_assert_cmpint(msize, ==, P9_MAX_SIZE);

    if (len || version) {
        v9fs_string_read(req, len, version);
    }

    v9fs_req_free(req);
}

/* size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s] n_uname[4] */
static P9Req *v9fs_tattach(QVirtIO9P *v9p, uint32_t fid, uint32_t n_uname)
{
    const char *uname = ""; /* ignored by QEMU */
    const char *aname = ""; /* ignored by QEMU */
    P9Req *req = v9fs_req_init(v9p, 4 + 4 + 2 + 2 + 4, P9_TATTACH,
                               ++(v9p->p9_req_tag));

    v9fs_uint32_write(req, fid);
    v9fs_uint32_write(req, P9_NOFID);
    v9fs_string_write(req, uname);
    v9fs_string_write(req, aname);
    v9fs_uint32_write(req, n_uname);
    v9fs_req_send(req);
    return req;
}

typedef char v9fs_qid[13];

/* size[4] Rattach tag[2] qid[13] */
static void v9fs_rattach(P9Req *req, v9fs_qid *qid)
{
    v9fs_req_recv(req, P9_RATTACH);
    if (qid) {
        v9fs_memread(req, qid, 13);
    }
    v9fs_req_free(req);
}

/* size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s]) */
static P9Req *v9fs_twalk(QVirtIO9P *v9p, uint32_t fid, uint32_t newfid,
                         uint16_t nwname, char *const wnames[])
{
    P9Req *req;
    int i;
    uint32_t size = 4 + 4 + 2;

    for (i = 0; i < nwname; i++) {
        size += v9fs_string_size(wnames[i]);
    }
    req = v9fs_req_init(v9p,  size, P9_TWALK, ++(v9p->p9_req_tag));
    v9fs_uint32_write(req, fid);
    v9fs_uint32_write(req, newfid);
    v9fs_uint16_write(req, nwname);
    for (i = 0; i < nwname; i++) {
        v9fs_string_write(req, wnames[i]);
    }
    v9fs_req_send(req);
    return req;
}

/* size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13]) */
static void v9fs_rwalk(P9Req *req, uint16_t *nwqid, v9fs_qid **wqid)
{
    uint16_t local_nwqid;

    v9fs_req_recv(req, P9_RWALK);
    v9fs_uint16_read(req, &local_nwqid);
    if (nwqid) {
        *nwqid = local_nwqid;
    }
    if (wqid) {
        *wqid = g_malloc(local_nwqid * 13);
        v9fs_memread(req, *wqid, local_nwqid * 13);
    }
    v9fs_req_free(req);
}

static void fs_version(QVirtIO9P *v9p)
{
    const char *version = "9P2000.L";
    uint16_t server_len;
    char *server_version;
    P9Req *req;

    req = v9fs_tversion(v9p, P9_MAX_SIZE, version);
    v9fs_rversion(req, &server_len, &server_version);

    g_assert_cmpmem(server_version, server_len, version, strlen(version));

    g_free(server_version);
}

static void fs_attach(QVirtIO9P *v9p)
{
    P9Req *req;

    fs_version(v9p);
    req = v9fs_tattach(v9p, 0, getuid());
    v9fs_rattach(req, NULL);
}

static void fs_walk(QVirtIO9P *v9p)
{
    char *wnames[P9_MAXWELEM], *paths[P9_MAXWELEM];
    char *last_path = v9p->test_share;
    uint16_t nwqid;
    v9fs_qid *wqid;
    int i;
    P9Req *req;

    for (i = 0; i < P9_MAXWELEM; i++) {
        wnames[i] = g_strdup_printf("%s%d", __func__, i);
        last_path = paths[i] = g_strdup_printf("%s/%s", last_path, wnames[i]);
        g_assert(!mkdir(paths[i], 0700));
    }

    fs_attach(v9p);
    req = v9fs_twalk(v9p, 0, 1, P9_MAXWELEM, wnames);
    v9fs_rwalk(req, &nwqid, &wqid);

    g_assert_cmpint(nwqid, ==, P9_MAXWELEM);

    for (i = 0; i < P9_MAXWELEM; i++) {
        rmdir(paths[P9_MAXWELEM - i - 1]);
        g_free(paths[P9_MAXWELEM - i - 1]);
        g_free(wnames[i]);
    }

    g_free(wqid);
}

static void fs_walk_no_slash(QVirtIO9P *v9p)
{
    char *const wnames[] = { g_strdup(" /") };
    P9Req *req;
    uint32_t err;

    fs_attach(v9p);
    req = v9fs_twalk(v9p, 0, 1, 1, wnames);
    v9fs_rlerror(req, &err);

    g_assert_cmpint(err, ==, ENOENT);

    g_free(wnames[0]);
}

static void fs_walk_dotdot(QVirtIO9P *v9p)
{
    char *const wnames[] = { g_strdup("..") };
    v9fs_qid root_qid, *wqid;
    P9Req *req;

    fs_version(v9p);
    req = v9fs_tattach(v9p, 0, getuid());
    v9fs_rattach(req, &root_qid);

    req = v9fs_twalk(v9p, 0, 1, 1, wnames);
    v9fs_rwalk(req, NULL, &wqid); /* We now we'll get one qid */

    g_assert_cmpmem(&root_qid, 13, wqid[0], 13);

    g_free(wqid);
    g_free(wnames[0]);
}

typedef void (*v9fs_test_fn)(QVirtIO9P *v9p);

static void v9fs_run_pci_test(gconstpointer data)
{
    v9fs_test_fn fn = data;
    QVirtIO9P *v9p = qvirtio_9p_pci_start();

    if (fn) {
        fn(v9p);
    }
    qvirtio_9p_pci_stop(v9p);
}

static void v9fs_qtest_pci_add(const char *path, v9fs_test_fn fn)
{
    qtest_add_data_func(path, fn, v9fs_run_pci_test);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    v9fs_qtest_pci_add("/virtio/9p/pci/nop", NULL);
    v9fs_qtest_pci_add("/virtio/9p/pci/config", pci_config);
    v9fs_qtest_pci_add("/virtio/9p/pci/fs/version/basic", fs_version);
    v9fs_qtest_pci_add("/virtio/9p/pci/fs/attach/basic", fs_attach);
    v9fs_qtest_pci_add("/virtio/9p/pci/fs/walk/basic", fs_walk);
    v9fs_qtest_pci_add("/virtio/9p/pci/fs/walk/no_slash", fs_walk_no_slash);
    v9fs_qtest_pci_add("/virtio/9p/pci/fs/walk/dotdot_from_root",
                       fs_walk_dotdot);

    return g_test_run();
}
