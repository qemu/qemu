/*
 * QTest testcase for VirtIO 9P
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "hw/9pfs/9p.h"
#include "hw/9pfs/9p-synth.h"
#include "libqos/virtio-9p.h"
#include "libqos/qgraph.h"

#define QVIRTIO_9P_TIMEOUT_US (10 * 1000 * 1000)
static QGuestAllocator *alloc;

/*
 * Used to auto generate new fids. Start with arbitrary high value to avoid
 * collision with hard coded fids in basic test code.
 */
static uint32_t fid_generator = 1000;

static uint32_t genfid(void)
{
    return fid_generator++;
}

/**
 * Splits the @a in string by @a delim into individual (non empty) strings
 * and outputs them to @a out. The output array @a out is NULL terminated.
 *
 * Output array @a out must be freed by calling split_free().
 *
 * @returns number of individual elements in output array @a out (without the
 *          final NULL terminating element)
 */
static int split(const char *in, const char *delim, char ***out)
{
    int n = 0, i = 0;
    char *tmp, *p;

    tmp = g_strdup(in);
    for (p = strtok(tmp, delim); p != NULL; p = strtok(NULL, delim)) {
        if (strlen(p) > 0) {
            ++n;
        }
    }
    g_free(tmp);

    *out = g_new0(char *, n + 1); /* last element NULL delimiter */

    tmp = g_strdup(in);
    for (p = strtok(tmp, delim); p != NULL; p = strtok(NULL, delim)) {
        if (strlen(p) > 0) {
            (*out)[i++] = g_strdup(p);
        }
    }
    g_free(tmp);

    return n;
}

static void split_free(char ***out)
{
    int i;
    for (i = 0; (*out)[i]; ++i) {
        g_free((*out)[i]);
    }
    g_free(*out);
    *out = NULL;
}

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
    QTestState *qts;
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
    qtest_memwrite(req->qts, req->t_msg + req->t_off, addr, len);
    req->t_off += len;
}

static void v9fs_memskip(P9Req *req, size_t len)
{
    req->r_off += len;
}

static void v9fs_memread(P9Req *req, void *addr, size_t len)
{
    qtest_memread(req->qts, req->r_msg + req->r_off, addr, len);
    req->r_off += len;
}

static void v9fs_uint8_read(P9Req *req, uint8_t *val)
{
    v9fs_memread(req, val, 1);
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

static void v9fs_uint64_read(P9Req *req, uint64_t *val)
{
    v9fs_memread(req, val, 8);
    le64_to_cpus(val);
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
        *string = g_malloc(local_len + 1);
        v9fs_memread(req, *string, local_len);
        (*string)[local_len] = 0;
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

    req->qts = global_qtest;
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
    req->free_head = qvirtqueue_add(req->qts, v9p->vq, req->t_msg, req->t_size,
                                    false, true);
    qvirtqueue_add(req->qts, v9p->vq, req->r_msg, P9_MAX_SIZE, true, false);
    qvirtqueue_kick(req->qts, v9p->vdev, v9p->vq, req->free_head);
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
        id == P9_RMKDIR ? "RMKDIR" :
        id == P9_RLCREATE ? "RLCREATE" :
        id == P9_RSYMLINK ? "RSYMLINK" :
        id == P9_RLINK ? "RLINK" :
        id == P9_RUNLINKAT ? "RUNLINKAT" :
        id == P9_RFLUSH ? "RFLUSH" :
        id == P9_RREADDIR ? "READDIR" :
        "<unknown>";
}

static void v9fs_req_wait_for_reply(P9Req *req, uint32_t *len)
{
    QVirtio9P *v9p = req->v9p;

    qvirtio_wait_used_elem(req->qts, v9p->vdev, v9p->vq, req->free_head, len,
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

/* size[4] Treaddir tag[2] fid[4] offset[8] count[4] */
static P9Req *v9fs_treaddir(QVirtio9P *v9p, uint32_t fid, uint64_t offset,
                            uint32_t count, uint16_t tag)
{
    P9Req *req;

    req = v9fs_req_init(v9p, 4 + 8 + 4, P9_TREADDIR, tag);
    v9fs_uint32_write(req, fid);
    v9fs_uint64_write(req, offset);
    v9fs_uint32_write(req, count);
    v9fs_req_send(req);
    return req;
}

struct V9fsDirent {
    v9fs_qid qid;
    uint64_t offset;
    uint8_t type;
    char *name;
    struct V9fsDirent *next;
};

/* size[4] Rreaddir tag[2] count[4] data[count] */
static void v9fs_rreaddir(P9Req *req, uint32_t *count, uint32_t *nentries,
                          struct V9fsDirent **entries)
{
    uint32_t local_count;
    struct V9fsDirent *e = NULL;
    uint16_t slen;
    uint32_t n = 0;

    v9fs_req_recv(req, P9_RREADDIR);
    v9fs_uint32_read(req, &local_count);

    if (count) {
        *count = local_count;
    }

    for (int32_t togo = (int32_t)local_count;
         togo >= 13 + 8 + 1 + 2;
         togo -= 13 + 8 + 1 + 2 + slen, ++n)
    {
        if (!e) {
            e = g_malloc(sizeof(struct V9fsDirent));
            if (entries) {
                *entries = e;
            }
        } else {
            e = e->next = g_malloc(sizeof(struct V9fsDirent));
        }
        e->next = NULL;
        /* qid[13] offset[8] type[1] name[s] */
        v9fs_memread(req, &e->qid, 13);
        v9fs_uint64_read(req, &e->offset);
        v9fs_uint8_read(req, &e->type);
        v9fs_string_read(req, &slen, &e->name);
    }

    if (nentries) {
        *nentries = n;
    }

    v9fs_req_free(req);
}

static void v9fs_free_dirents(struct V9fsDirent *e)
{
    struct V9fsDirent *next = NULL;

    for (; e; e = next) {
        next = e->next;
        g_free(e->name);
        g_free(e);
    }
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

static void do_version(QVirtio9P *v9p)
{
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

/* utility function: walk to requested dir and return fid for that dir */
static uint32_t do_walk(QVirtio9P *v9p, const char *path)
{
    char **wnames;
    P9Req *req;
    const uint32_t fid = genfid();

    int nwnames = split(path, "/", &wnames);

    req = v9fs_twalk(v9p, 0, fid, nwnames, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, NULL);

    split_free(&wnames);
    return fid;
}

static void fs_version(void *obj, void *data, QGuestAllocator *t_alloc)
{
    alloc = t_alloc;
    do_version(obj);
}

static void do_attach(QVirtio9P *v9p)
{
    P9Req *req;

    do_version(v9p);
    req = v9fs_tattach(v9p, 0, getuid(), 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rattach(req, NULL);
}

static void fs_attach(void *obj, void *data, QGuestAllocator *t_alloc)
{
    alloc = t_alloc;
    do_attach(obj);
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

    do_attach(v9p);
    req = v9fs_twalk(v9p, 0, 1, P9_MAXWELEM, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, &nwqid, &wqid);

    g_assert_cmpint(nwqid, ==, P9_MAXWELEM);

    for (i = 0; i < P9_MAXWELEM; i++) {
        g_free(wnames[i]);
    }

    g_free(wqid);
}

static bool fs_dirents_contain_name(struct V9fsDirent *e, const char* name)
{
    for (; e; e = e->next) {
        if (!strcmp(e->name, name)) {
            return true;
        }
    }
    return false;
}

/* size[4] Tmkdir tag[2] dfid[4] name[s] mode[4] gid[4] */
static P9Req *v9fs_tmkdir(QVirtio9P *v9p, uint32_t dfid, const char *name,
                          uint32_t mode, uint32_t gid, uint16_t tag)
{
    P9Req *req;

    uint32_t body_size = 4 + 4 + 4;
    uint16_t string_size = v9fs_string_size(name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(v9p, body_size, P9_TMKDIR, tag);
    v9fs_uint32_write(req, dfid);
    v9fs_string_write(req, name);
    v9fs_uint32_write(req, mode);
    v9fs_uint32_write(req, gid);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rmkdir tag[2] qid[13] */
static void v9fs_rmkdir(P9Req *req, v9fs_qid *qid)
{
    v9fs_req_recv(req, P9_RMKDIR);
    if (qid) {
        v9fs_memread(req, qid, 13);
    } else {
        v9fs_memskip(req, 13);
    }
    v9fs_req_free(req);
}

/* size[4] Tlcreate tag[2] fid[4] name[s] flags[4] mode[4] gid[4] */
static P9Req *v9fs_tlcreate(QVirtio9P *v9p, uint32_t fid, const char *name,
                            uint32_t flags, uint32_t mode, uint32_t gid,
                            uint16_t tag)
{
    P9Req *req;

    uint32_t body_size = 4 + 4 + 4 + 4;
    uint16_t string_size = v9fs_string_size(name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(v9p, body_size, P9_TLCREATE, tag);
    v9fs_uint32_write(req, fid);
    v9fs_string_write(req, name);
    v9fs_uint32_write(req, flags);
    v9fs_uint32_write(req, mode);
    v9fs_uint32_write(req, gid);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rlcreate tag[2] qid[13] iounit[4] */
static void v9fs_rlcreate(P9Req *req, v9fs_qid *qid, uint32_t *iounit)
{
    v9fs_req_recv(req, P9_RLCREATE);
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

/* size[4] Tsymlink tag[2] fid[4] name[s] symtgt[s] gid[4] */
static P9Req *v9fs_tsymlink(QVirtio9P *v9p, uint32_t fid, const char *name,
                            const char *symtgt, uint32_t gid, uint16_t tag)
{
    P9Req *req;

    uint32_t body_size = 4 + 4;
    uint16_t string_size = v9fs_string_size(name) + v9fs_string_size(symtgt);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(v9p, body_size, P9_TSYMLINK, tag);
    v9fs_uint32_write(req, fid);
    v9fs_string_write(req, name);
    v9fs_string_write(req, symtgt);
    v9fs_uint32_write(req, gid);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rsymlink tag[2] qid[13] */
static void v9fs_rsymlink(P9Req *req, v9fs_qid *qid)
{
    v9fs_req_recv(req, P9_RSYMLINK);
    if (qid) {
        v9fs_memread(req, qid, 13);
    } else {
        v9fs_memskip(req, 13);
    }
    v9fs_req_free(req);
}

/* size[4] Tlink tag[2] dfid[4] fid[4] name[s] */
static P9Req *v9fs_tlink(QVirtio9P *v9p, uint32_t dfid, uint32_t fid,
                         const char *name, uint16_t tag)
{
    P9Req *req;

    uint32_t body_size = 4 + 4;
    uint16_t string_size = v9fs_string_size(name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(v9p, body_size, P9_TLINK, tag);
    v9fs_uint32_write(req, dfid);
    v9fs_uint32_write(req, fid);
    v9fs_string_write(req, name);
    v9fs_req_send(req);
    return req;
}

/* size[4] Rlink tag[2] */
static void v9fs_rlink(P9Req *req)
{
    v9fs_req_recv(req, P9_RLINK);
    v9fs_req_free(req);
}

/* size[4] Tunlinkat tag[2] dirfd[4] name[s] flags[4] */
static P9Req *v9fs_tunlinkat(QVirtio9P *v9p, uint32_t dirfd, const char *name,
                             uint32_t flags, uint16_t tag)
{
    P9Req *req;

    uint32_t body_size = 4 + 4;
    uint16_t string_size = v9fs_string_size(name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(v9p, body_size, P9_TUNLINKAT, tag);
    v9fs_uint32_write(req, dirfd);
    v9fs_string_write(req, name);
    v9fs_uint32_write(req, flags);
    v9fs_req_send(req);
    return req;
}

/* size[4] Runlinkat tag[2] */
static void v9fs_runlinkat(P9Req *req)
{
    v9fs_req_recv(req, P9_RUNLINKAT);
    v9fs_req_free(req);
}

/* basic readdir test where reply fits into a single response message */
static void fs_readdir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *const wnames[] = { g_strdup(QTEST_V9FS_SYNTH_READDIR_DIR) };
    uint16_t nqid;
    v9fs_qid qid;
    uint32_t count, nentries;
    struct V9fsDirent *entries = NULL;
    P9Req *req;

    do_attach(v9p);
    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, &nqid, NULL);
    g_assert_cmpint(nqid, ==, 1);

    req = v9fs_tlopen(v9p, 1, O_DIRECTORY, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlopen(req, &qid, NULL);

    /*
     * submit count = msize - 11, because 11 is the header size of Rreaddir
     */
    req = v9fs_treaddir(v9p, 1, 0, P9_MAX_SIZE - 11, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rreaddir(req, &count, &nentries, &entries);

    /*
     * Assuming msize (P9_MAX_SIZE) is large enough so we can retrieve all
     * dir entries with only one readdir request.
     */
    g_assert_cmpint(
        nentries, ==,
        QTEST_V9FS_SYNTH_READDIR_NFILES + 2 /* "." and ".." */
    );

    /*
     * Check all file names exist in returned entries, ignore their order
     * though.
     */
    g_assert_cmpint(fs_dirents_contain_name(entries, "."), ==, true);
    g_assert_cmpint(fs_dirents_contain_name(entries, ".."), ==, true);
    for (int i = 0; i < QTEST_V9FS_SYNTH_READDIR_NFILES; ++i) {
        char *name = g_strdup_printf(QTEST_V9FS_SYNTH_READDIR_FILE, i);
        g_assert_cmpint(fs_dirents_contain_name(entries, name), ==, true);
        g_free(name);
    }

    v9fs_free_dirents(entries);
    g_free(wnames[0]);
}

/* readdir test where overall request is split over several messages */
static void do_readdir_split(QVirtio9P *v9p, uint32_t count)
{
    char *const wnames[] = { g_strdup(QTEST_V9FS_SYNTH_READDIR_DIR) };
    uint16_t nqid;
    v9fs_qid qid;
    uint32_t nentries, npartialentries;
    struct V9fsDirent *entries, *tail, *partialentries;
    P9Req *req;
    int fid;
    uint64_t offset;

    do_attach(v9p);

    fid = 1;
    offset = 0;
    entries = NULL;
    nentries = 0;
    tail = NULL;

    req = v9fs_twalk(v9p, 0, fid, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, &nqid, NULL);
    g_assert_cmpint(nqid, ==, 1);

    req = v9fs_tlopen(v9p, fid, O_DIRECTORY, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlopen(req, &qid, NULL);

    /*
     * send as many Treaddir requests as required to get all directory
     * entries
     */
    while (true) {
        npartialentries = 0;
        partialentries = NULL;

        req = v9fs_treaddir(v9p, fid, offset, count, 0);
        v9fs_req_wait_for_reply(req, NULL);
        v9fs_rreaddir(req, &count, &npartialentries, &partialentries);
        if (npartialentries > 0 && partialentries) {
            if (!entries) {
                entries = partialentries;
                nentries = npartialentries;
                tail = partialentries;
            } else {
                tail->next = partialentries;
                nentries += npartialentries;
            }
            while (tail->next) {
                tail = tail->next;
            }
            offset = tail->offset;
        } else {
            break;
        }
    }

    g_assert_cmpint(
        nentries, ==,
        QTEST_V9FS_SYNTH_READDIR_NFILES + 2 /* "." and ".." */
    );

    /*
     * Check all file names exist in returned entries, ignore their order
     * though.
     */
    g_assert_cmpint(fs_dirents_contain_name(entries, "."), ==, true);
    g_assert_cmpint(fs_dirents_contain_name(entries, ".."), ==, true);
    for (int i = 0; i < QTEST_V9FS_SYNTH_READDIR_NFILES; ++i) {
        char *name = g_strdup_printf(QTEST_V9FS_SYNTH_READDIR_FILE, i);
        g_assert_cmpint(fs_dirents_contain_name(entries, name), ==, true);
        g_free(name);
    }

    v9fs_free_dirents(entries);

    g_free(wnames[0]);
}

static void fs_walk_no_slash(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    char *const wnames[] = { g_strdup(" /") };
    P9Req *req;
    uint32_t err;

    do_attach(v9p);
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

    do_version(v9p);
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

    do_attach(v9p);
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

    do_attach(v9p);
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

    do_attach(v9p);
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

    do_attach(v9p);
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

static void do_mkdir(QVirtio9P *v9p, const char *path, const char *cname)
{
    char *const name = g_strdup(cname);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, path);

    req = v9fs_tmkdir(v9p, fid, name, 0750, 0, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rmkdir(req, NULL);

    g_free(name);
}

/* create a regular file with Tlcreate and return file's fid */
static uint32_t do_lcreate(QVirtio9P *v9p, const char *path,
                           const char *cname)
{
    char *const name = g_strdup(cname);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, path);

    req = v9fs_tlcreate(v9p, fid, name, 0, 0750, 0, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlcreate(req, NULL, NULL);

    g_free(name);
    return fid;
}

/* create symlink named @a clink in directory @a path pointing to @a to */
static void do_symlink(QVirtio9P *v9p, const char *path, const char *clink,
                       const char *to)
{
    char *const name = g_strdup(clink);
    char *const dst = g_strdup(to);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, path);

    req = v9fs_tsymlink(v9p, fid, name, dst, 0, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rsymlink(req, NULL);

    g_free(dst);
    g_free(name);
}

/* create a hard link named @a clink in directory @a path pointing to @a to */
static void do_hardlink(QVirtio9P *v9p, const char *path, const char *clink,
                        const char *to)
{
    uint32_t dfid, fid;
    P9Req *req;

    dfid = do_walk(v9p, path);
    fid = do_walk(v9p, to);

    req = v9fs_tlink(v9p, dfid, fid, clink, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlink(req);
}

static void do_unlinkat(QVirtio9P *v9p, const char *atpath, const char *rpath,
                        uint32_t flags)
{
    char *const name = g_strdup(rpath);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, atpath);

    req = v9fs_tunlinkat(v9p, fid, name, flags, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_runlinkat(req);

    g_free(name);
}

static void fs_readdir_split_128(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    alloc = t_alloc;
    do_readdir_split(obj, 128);
}

static void fs_readdir_split_256(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    alloc = t_alloc;
    do_readdir_split(obj, 256);
}

static void fs_readdir_split_512(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    alloc = t_alloc;
    do_readdir_split(obj, 512);
}


/* tests using the 9pfs 'local' fs driver */

static void fs_create_dir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st;
    char *root_path = virtio_9p_test_path("");
    char *new_dir = virtio_9p_test_path("01");

    g_assert(root_path != NULL);

    do_attach(v9p);
    do_mkdir(v9p, "/", "01");

    /* check if created directory really exists now ... */
    g_assert(stat(new_dir, &st) == 0);
    /* ... and is actually a directory */
    g_assert((st.st_mode & S_IFMT) == S_IFDIR);

    g_free(new_dir);
    g_free(root_path);
}

static void fs_unlinkat_dir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st;
    char *root_path = virtio_9p_test_path("");
    char *new_dir = virtio_9p_test_path("02");

    g_assert(root_path != NULL);

    do_attach(v9p);
    do_mkdir(v9p, "/", "02");

    /* check if created directory really exists now ... */
    g_assert(stat(new_dir, &st) == 0);
    /* ... and is actually a directory */
    g_assert((st.st_mode & S_IFMT) == S_IFDIR);

    do_unlinkat(v9p, "/", "02", AT_REMOVEDIR);
    /* directory should be gone now */
    g_assert(stat(new_dir, &st) != 0);

    g_free(new_dir);
    g_free(root_path);
}

static void fs_create_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st;
    char *new_file = virtio_9p_test_path("03/1st_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "03");
    do_lcreate(v9p, "03", "1st_file");

    /* check if created file exists now ... */
    g_assert(stat(new_file, &st) == 0);
    /* ... and is a regular file */
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    g_free(new_file);
}

static void fs_unlinkat_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st;
    char *new_file = virtio_9p_test_path("04/doa_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "04");
    do_lcreate(v9p, "04", "doa_file");

    /* check if created file exists now ... */
    g_assert(stat(new_file, &st) == 0);
    /* ... and is a regular file */
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    do_unlinkat(v9p, "04", "doa_file", 0);
    /* file should be gone now */
    g_assert(stat(new_file, &st) != 0);

    g_free(new_file);
}

static void fs_symlink_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st;
    char *real_file = virtio_9p_test_path("05/real_file");
    char *symlink_file = virtio_9p_test_path("05/symlink_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "05");
    do_lcreate(v9p, "05", "real_file");
    g_assert(stat(real_file, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    do_symlink(v9p, "05", "symlink_file", "real_file");

    /* check if created link exists now */
    g_assert(stat(symlink_file, &st) == 0);

    g_free(symlink_file);
    g_free(real_file);
}

static void fs_unlinkat_symlink(void *obj, void *data,
                                QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st;
    char *real_file = virtio_9p_test_path("06/real_file");
    char *symlink_file = virtio_9p_test_path("06/symlink_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "06");
    do_lcreate(v9p, "06", "real_file");
    g_assert(stat(real_file, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    do_symlink(v9p, "06", "symlink_file", "real_file");
    g_assert(stat(symlink_file, &st) == 0);

    do_unlinkat(v9p, "06", "symlink_file", 0);
    /* symlink should be gone now */
    g_assert(stat(symlink_file, &st) != 0);

    g_free(symlink_file);
    g_free(real_file);
}

static void fs_hardlink_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st_real, st_link;
    char *real_file = virtio_9p_test_path("07/real_file");
    char *hardlink_file = virtio_9p_test_path("07/hardlink_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "07");
    do_lcreate(v9p, "07", "real_file");
    g_assert(stat(real_file, &st_real) == 0);
    g_assert((st_real.st_mode & S_IFMT) == S_IFREG);

    do_hardlink(v9p, "07", "hardlink_file", "07/real_file");

    /* check if link exists now ... */
    g_assert(stat(hardlink_file, &st_link) == 0);
    /* ... and it's a hard link, right? */
    g_assert((st_link.st_mode & S_IFMT) == S_IFREG);
    g_assert(st_link.st_dev == st_real.st_dev);
    g_assert(st_link.st_ino == st_real.st_ino);

    g_free(hardlink_file);
    g_free(real_file);
}

static void fs_unlinkat_hardlink(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    alloc = t_alloc;
    struct stat st_real, st_link;
    char *real_file = virtio_9p_test_path("08/real_file");
    char *hardlink_file = virtio_9p_test_path("08/hardlink_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "08");
    do_lcreate(v9p, "08", "real_file");
    g_assert(stat(real_file, &st_real) == 0);
    g_assert((st_real.st_mode & S_IFMT) == S_IFREG);

    do_hardlink(v9p, "08", "hardlink_file", "08/real_file");
    g_assert(stat(hardlink_file, &st_link) == 0);

    do_unlinkat(v9p, "08", "hardlink_file", 0);
    /* symlink should be gone now */
    g_assert(stat(hardlink_file, &st_link) != 0);
    /* and old file should still exist */
    g_assert(stat(real_file, &st_real) == 0);

    g_free(hardlink_file);
    g_free(real_file);
}

static void *assign_9p_local_driver(GString *cmd_line, void *arg)
{
    virtio_9p_assign_local_driver(cmd_line, "security_model=mapped-xattr");
    return arg;
}

static void register_virtio_9p_test(void)
{

    QOSGraphTestOptions opts = {
    };

    /* 9pfs test cases using the 'synth' filesystem driver */
    qos_add_test("synth/config", "virtio-9p", pci_config, &opts);
    qos_add_test("synth/version/basic", "virtio-9p", fs_version,  &opts);
    qos_add_test("synth/attach/basic", "virtio-9p", fs_attach,  &opts);
    qos_add_test("synth/walk/basic", "virtio-9p", fs_walk,  &opts);
    qos_add_test("synth/walk/no_slash", "virtio-9p", fs_walk_no_slash,
                  &opts);
    qos_add_test("synth/walk/dotdot_from_root", "virtio-9p",
                 fs_walk_dotdot,  &opts);
    qos_add_test("synth/lopen/basic", "virtio-9p", fs_lopen,  &opts);
    qos_add_test("synth/write/basic", "virtio-9p", fs_write,  &opts);
    qos_add_test("synth/flush/success", "virtio-9p", fs_flush_success,
                  &opts);
    qos_add_test("synth/flush/ignored", "virtio-9p", fs_flush_ignored,
                  &opts);
    qos_add_test("synth/readdir/basic", "virtio-9p", fs_readdir,  &opts);
    qos_add_test("synth/readdir/split_512", "virtio-9p",
                 fs_readdir_split_512,  &opts);
    qos_add_test("synth/readdir/split_256", "virtio-9p",
                 fs_readdir_split_256,  &opts);
    qos_add_test("synth/readdir/split_128", "virtio-9p",
                 fs_readdir_split_128,  &opts);


    /* 9pfs test cases using the 'local' filesystem driver */

    /*
     * XXX: Until we are sure that these tests can run everywhere,
     * keep them as "slow" so that they aren't run with "make check".
     */
    if (!g_test_slow()) {
        return;
    }

    opts.before = assign_9p_local_driver;
    qos_add_test("local/config", "virtio-9p", pci_config,  &opts);
    qos_add_test("local/create_dir", "virtio-9p", fs_create_dir, &opts);
    qos_add_test("local/unlinkat_dir", "virtio-9p", fs_unlinkat_dir, &opts);
    qos_add_test("local/create_file", "virtio-9p", fs_create_file, &opts);
    qos_add_test("local/unlinkat_file", "virtio-9p", fs_unlinkat_file, &opts);
    qos_add_test("local/symlink_file", "virtio-9p", fs_symlink_file, &opts);
    qos_add_test("local/unlinkat_symlink", "virtio-9p", fs_unlinkat_symlink,
                 &opts);
    qos_add_test("local/hardlink_file", "virtio-9p", fs_hardlink_file, &opts);
    qos_add_test("local/unlinkat_hardlink", "virtio-9p", fs_unlinkat_hardlink,
                 &opts);
}

libqos_init(register_virtio_9p_test);

static void __attribute__((constructor)) construct_9p_test(void)
{
    /* make sure test dir for the 'local' tests exists */
    virtio_9p_create_local_test_dir();
}

static void __attribute__((destructor)) destruct_9p_test(void)
{
    /* remove previously created test dir when test suite completed */
    virtio_9p_remove_local_test_dir();
}
