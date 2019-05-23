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
#include "qemu/module.h"
#include "hw/9pfs/9p.h"
#include "hw/9pfs/9p-synth.h"
#include "libqos/virtio-9p.h"
#include "libqos/qgraph.h"

#define QVIRTIO_9P_TIMEOUT_US (10 * 1000 * 1000)
static QGuestAllocator *alloc;

static void pci_config(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    size_t tag_len = qvirtio_config_readw(v9p->vdev, 0);
    char *tag;
    int i;

    g_assert_cmpint(tag_len, ==, strlen(MOUNT_TAG));

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(v9p->vdev, i + 2);
    }
    g_assert_cmpmem(tag, tag_len, MOUNT_TAG, tag_len);
    g_free(tag);
}

#define P9_MAX_SIZE 4096 /* Max size of a T-message or R-message */

typedef struct {
    QVirtio9P *v9p;
    uint16_t tag;
    uint64_t t_msg;
    uint32_t t_size;
    uint64_t r_msg;
    /* No r_size, it is hardcoded to P9_MAX_SIZE */
    size_t t_off;
    size_t r_off;
    uint32_t free_head;
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

static void v9fs_uint64_write(P9Req *req, uint64_t val)
{
    uint64_t le_val = cpu_to_le64(val);

    v9fs_memwrite(req, &le_val, 8);
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

    g_assert_cmpint(len, <=, UINT16_MAX - 2);

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

static P9Req *v9fs_req_init(QVirtio9P *v9p, uint32_t size, uint8_t id,
                            uint16_t tag)
{
    P9Req *req = g_new0(P9Req, 1);
    uint32_t total_size = 7; /* 9P header has well-known size of 7 bytes */
    P9Hdr hdr = {
        .id = id,
        .tag = cpu_to_le16(tag)
    };

    g_assert_cmpint(total_size, <=, UINT32_MAX - size);
    total_size += size;
    hdr.size = cpu_to_le32(total_size);

    g_assert_cmpint(total_size, <=, P9_MAX_SIZE);

    req->v9p = v9p;
    req->t_size = total_size;
    req->t_msg = guest_alloc(alloc, req->t_size);
    v9fs_memwrite(req, &hdr, 7);
    req->tag = tag;
    return req;
}

static void v9fs_req_send(P9Req *req)
{
    QVirtio9P *v9p = req->v9p;

    req->r_msg = guest_alloc(alloc, P9_MAX_SIZE);
    req->free_head = qvirtqueue_add(v9p->vq, req->t_msg, req->t_size, false,
                                    true);
    qvirtqueue_add(v9p->vq, req->r_msg, P9_MAX_SIZE, true, false);
    qvirtqueue_kick(v9p->vdev, v9p->vq, req->free_head);
    req->t_off = 0;
}

static const char *rmessage_name(uint8_t id)
{
    return
        id == P9_RLERROR ? "RLERROR" :
        id == P9_RVERSION ? "RVERSION" :
        id == P9_RATTACH ? "RATTACH" :
        id == P9_RWALK ? "RWALK" :
        id == P9_RLOPEN ? "RLOPEN" :
        id == P9_RWRITE ? "RWRITE" :
        id == P9_RFLUSH ? "RFLUSH" :
        "<unknown>";
}

static void v9fs_req_wait_for_reply(P9Req *req, uint32_t *len)
{
    QVirtio9P *v9p = req->v9p;

    qvirtio_wait_used_elem(v9p->vdev, v9p->vq, req->free_head, len,
                           QVIRTIO_9P_TIMEOUT_US);
}

static void v9fs_req_recv(P9Req *req, uint8_t id)
{
    P9Hdr hdr;

    v9fs_memread(req, &hdr, 7);
    hdr.size = ldl_le_p(&hdr.size);
    hdr.tag = lduw_le_p(&hdr.tag);

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
    guest_free(alloc, req->t_msg);
    guest_free(alloc, req->r_msg);
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
static P9Req *v9fs_tversion(QVirtio9P *v9p, uint32_t msize, const char *version,
                            uint16_t tag)
{
    P9Req *req;
    uint32_t body_size = 4;
    uint16_t string_size = v9fs_string_size(version);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;
    req = v9fs_req_init(v9p, body_size, P9_TVERSION, tag);

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
static P9Req *v9fs_tattach(QVirtio9P *v9p, uint32_t fid, uint32_t n_uname,
                           uint16_t tag)
{
    const char *uname = ""; /* ignored by QEMU */
    const char *aname = ""; /* ignored by QEMU */
    P9Req *req = v9fs_req_init(v9p, 4 + 4 + 2 + 2 + 4, P9_TATTACH, tag);

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
static P9Req *v9fs_twalk(QVirtio9P *v9p, uint32_t fid, uint32_t newfid,
                         uint16_t nwname, char *const wnames[], uint16_t tag)
{
    P9Req *req;
    int i;
    uint32_t body_size = 4 + 4 + 2;

    for (i = 0; i < nwname; i++) {
        uint16_t wname_size = v9fs_string_size(wnames[i]);

        g_assert_cmpint(body_size, <=, UINT32_MAX - wname_size);
        body_size += wname_size;
    }
    req = v9fs_req_init(v9p,  body_size, P9_TWALK, tag);
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

/* size[4] Tlopen tag[2] fid[4] flags[4] */
static P9Req *v9fs_tlopen(QVirtio9P *v9p, uint32_t fid, uint32_t flags,
                          uint16_t tag)
{
    P9Req *req;

    req = v9fs_req_init(v9p,  4 + 4, P9_TLOPEN, tag);
    v9fs_uint32_write(req, fid);
    v9fs_uint32_write(req, flags);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rlopen tag[2] qid[13] iounit[4] */
static void v9fs_rlopen(P9Req *req, v9fs_qid *qid, uint32_t *iounit)
{
    v9fs_req_recv(req, P9_RLOPEN);
    if (qid) {
        v9fs_memread(req, qid, 13);
    } else {
        v9fs_memskip(req, 13);
    }
    if (iounit) {
        v9fs_uint32_read(req, iounit);
    }
    v9fs_req_free(req);
}

/* size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count] */
static P9Req *v9fs_twrite(QVirtio9P *v9p, uint32_t fid, uint64_t offset,
                          uint32_t count, const void *data, uint16_t tag)
{
    P9Req *req;
    uint32_t body_size = 4 + 8 + 4;

    g_assert_cmpint(body_size, <=, UINT32_MAX - count);
    body_size += count;
    req = v9fs_req_init(v9p,  body_size, P9_TWRITE, tag);
    v9fs_uint32_write(req, fid);
    v9fs_uint64_write(req, offset);
    v9fs_uint32_write(req, count);
    v9fs_memwrite(req, data, count);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rwrite tag[2] count[4] */
static void v9fs_rwrite(P9Req *req, uint32_t *count)
{
    v9fs_req_recv(req, P9_RWRITE);
    if (count) {
        v9fs_uint32_read(req, count);
    }
    v9fs_req_free(req);
}

/* size[4] Tflush tag[2] oldtag[2] */
static P9Req *v9fs_tflush(QVirtio9P *v9p, uint16_t oldtag, uint16_t tag)
{
    P9Req *req;

    req = v9fs_req_init(v9p,  2, P9_TFLUSH, tag);
    v9fs_uint32_write(req, oldtag);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rflush tag[2] */
static void v9fs_rflush(P9Req *req)
{
    v9fs_req_recv(req, P9_RFLUSH);
    v9fs_req_free(req);
}

static void fs_version(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    const char *version = "9P2000.L";
    uint16_t server_len;
    char *server_version;
    P9Req *req;

    req = v9fs_tversion(v9p, P9_MAX_SIZE, version, P9_NOTAG);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rversion(req, &server_len, &server_version);

    g_assert_cmpmem(server_version, server_len, version, strlen(version));

    g_free(server_version);
}

static void fs_attach(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    P9Req *req;

    fs_version(v9p, NULL, t_alloc);
    req = v9fs_tattach(v9p, 0, getuid(), 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rattach(req, NULL);
}

static void fs_walk(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *wnames[P9_MAXWELEM];
    uint16_t nwqid;
    v9fs_qid *wqid;
    int i;
    P9Req *req;

    for (i = 0; i < P9_MAXWELEM; i++) {
        wnames[i] = g_strdup_printf(QTEST_V9FS_SYNTH_WALK_FILE, i);
    }

    fs_attach(v9p, NULL, t_alloc);
    req = v9fs_twalk(v9p, 0, 1, P9_MAXWELEM, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, &nwqid, &wqid);

    g_assert_cmpint(nwqid, ==, P9_MAXWELEM);

    for (i = 0; i < P9_MAXWELEM; i++) {
        g_free(wnames[i]);
    }

    g_free(wqid);
}

static void fs_walk_no_slash(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *const wnames[] = { g_strdup(" /") };
    P9Req *req;
    uint32_t err;

    fs_attach(v9p, NULL, t_alloc);
    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlerror(req, &err);

    g_assert_cmpint(err, ==, ENOENT);

    g_free(wnames[0]);
}

static void fs_walk_dotdot(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *const wnames[] = { g_strdup("..") };
    v9fs_qid root_qid, *wqid;
    P9Req *req;

    fs_version(v9p, NULL, t_alloc);
    req = v9fs_tattach(v9p, 0, getuid(), 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rattach(req, &root_qid);

    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, &wqid); /* We now we'll get one qid */

    g_assert_cmpmem(&root_qid, 13, wqid[0], 13);

    g_free(wqid);
    g_free(wnames[0]);
}

static void fs_lopen(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *const wnames[] = { g_strdup(QTEST_V9FS_SYNTH_LOPEN_FILE) };
    P9Req *req;

    fs_attach(v9p, NULL, t_alloc);
    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, NULL);

    req = v9fs_tlopen(v9p, 1, O_WRONLY, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlopen(req, NULL, NULL);

    g_free(wnames[0]);
}

static void fs_write(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    static const uint32_t write_count = P9_MAX_SIZE / 2;
    char *const wnames[] = { g_strdup(QTEST_V9FS_SYNTH_WRITE_FILE) };
    char *buf = g_malloc0(write_count);
    uint32_t count;
    P9Req *req;

    fs_attach(v9p, NULL, t_alloc);
    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, NULL);

    req = v9fs_tlopen(v9p, 1, O_WRONLY, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlopen(req, NULL, NULL);

    req = v9fs_twrite(v9p, 1, 0, write_count, buf, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwrite(req, &count);
    g_assert_cmpint(count, ==, write_count);

    g_free(buf);
    g_free(wnames[0]);
}

static void fs_flush_success(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *const wnames[] = { g_strdup(QTEST_V9FS_SYNTH_FLUSH_FILE) };
    P9Req *req, *flush_req;
    uint32_t reply_len;
    uint8_t should_block;

    fs_attach(v9p, NULL, t_alloc);
    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, NULL);

    req = v9fs_tlopen(v9p, 1, O_WRONLY, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlopen(req, NULL, NULL);

    /* This will cause the 9p server to try to write data to the backend,
     * until the write request gets cancelled.
     */
    should_block = 1;
    req = v9fs_twrite(v9p, 1, 0, sizeof(should_block), &should_block, 0);

    flush_req = v9fs_tflush(v9p, req->tag, 1);

    /* The write request is supposed to be flushed: the server should just
     * mark the write request as used and reply to the flush request.
     */
    v9fs_req_wait_for_reply(req, &reply_len);
    g_assert_cmpint(reply_len, ==, 0);
    v9fs_req_free(req);
    v9fs_rflush(flush_req);

    g_free(wnames[0]);
}

static void fs_flush_ignored(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *const wnames[] = { g_strdup(QTEST_V9FS_SYNTH_FLUSH_FILE) };
    P9Req *req, *flush_req;
    uint32_t count;
    uint8_t should_block;

    fs_attach(v9p, NULL, t_alloc);
    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, NULL);

    req = v9fs_tlopen(v9p, 1, O_WRONLY, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlopen(req, NULL, NULL);

    /* This will cause the write request to complete right away, before it
     * could be actually cancelled.
     */
    should_block = 0;
    req = v9fs_twrite(v9p, 1, 0, sizeof(should_block), &should_block, 0);

    flush_req = v9fs_tflush(v9p, req->tag, 1);

    /* The write request is supposed to complete. The server should
     * reply to the write request and the flush request.
     */
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwrite(req, &count);
    g_assert_cmpint(count, ==, sizeof(should_block));
    v9fs_rflush(flush_req);

    g_free(wnames[0]);
}

static void register_virtio_9p_test(void)
{
    qos_add_test("config", "virtio-9p", pci_config, NULL);
    qos_add_test("fs/version/basic", "virtio-9p", fs_version, NULL);
    qos_add_test("fs/attach/basic", "virtio-9p", fs_attach, NULL);
    qos_add_test("fs/walk/basic", "virtio-9p", fs_walk, NULL);
    qos_add_test("fs/walk/no_slash", "virtio-9p", fs_walk_no_slash,
                 NULL);
    qos_add_test("fs/walk/dotdot_from_root", "virtio-9p",
                 fs_walk_dotdot, NULL);
    qos_add_test("fs/lopen/basic", "virtio-9p", fs_lopen, NULL);
    qos_add_test("fs/write/basic", "virtio-9p", fs_write, NULL);
    qos_add_test("fs/flush/success", "virtio-9p", fs_flush_success,
                 NULL);
    qos_add_test("fs/flush/ignored", "virtio-9p", fs_flush_ignored,
                 NULL);
}

libqos_init(register_virtio_9p_test);
