/*
 * 9P network client for VirtIO 9P test cases (based on QTest)
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include "virtio-9p-client.h"

#define QVIRTIO_9P_TIMEOUT_US (10 * 1000 * 1000)
static QGuestAllocator *alloc;

void v9fs_set_allocator(QGuestAllocator *t_alloc)
{
    alloc = t_alloc;
}

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
    if (!*out) {
        return;
    }
    for (i = 0; (*out)[i]; ++i) {
        g_free((*out)[i]);
    }
    g_free(*out);
    *out = NULL;
}

void v9fs_memwrite(P9Req *req, const void *addr, size_t len)
{
    qtest_memwrite(req->qts, req->t_msg + req->t_off, addr, len);
    req->t_off += len;
}

void v9fs_memskip(P9Req *req, size_t len)
{
    req->r_off += len;
}

void v9fs_memread(P9Req *req, void *addr, size_t len)
{
    qtest_memread(req->qts, req->r_msg + req->r_off, addr, len);
    req->r_off += len;
}

void v9fs_uint8_read(P9Req *req, uint8_t *val)
{
    v9fs_memread(req, val, 1);
}

void v9fs_uint16_write(P9Req *req, uint16_t val)
{
    uint16_t le_val = cpu_to_le16(val);

    v9fs_memwrite(req, &le_val, 2);
}

void v9fs_uint16_read(P9Req *req, uint16_t *val)
{
    v9fs_memread(req, val, 2);
    le16_to_cpus(val);
}

void v9fs_uint32_write(P9Req *req, uint32_t val)
{
    uint32_t le_val = cpu_to_le32(val);

    v9fs_memwrite(req, &le_val, 4);
}

void v9fs_uint64_write(P9Req *req, uint64_t val)
{
    uint64_t le_val = cpu_to_le64(val);

    v9fs_memwrite(req, &le_val, 8);
}

void v9fs_uint32_read(P9Req *req, uint32_t *val)
{
    v9fs_memread(req, val, 4);
    le32_to_cpus(val);
}

void v9fs_uint64_read(P9Req *req, uint64_t *val)
{
    v9fs_memread(req, val, 8);
    le64_to_cpus(val);
}

/* len[2] string[len] */
uint16_t v9fs_string_size(const char *string)
{
    size_t len = strlen(string);

    g_assert_cmpint(len, <=, UINT16_MAX - 2);

    return 2 + len;
}

void v9fs_string_write(P9Req *req, const char *string)
{
    int len = strlen(string);

    g_assert_cmpint(len, <=, UINT16_MAX);

    v9fs_uint16_write(req, (uint16_t) len);
    v9fs_memwrite(req, string, len);
}

void v9fs_string_read(P9Req *req, uint16_t *len, char **string)
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

P9Req *v9fs_req_init(QVirtio9P *v9p, uint32_t size, uint8_t id,
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

void v9fs_req_send(P9Req *req)
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
        id == P9_RGETATTR ? "RGETATTR" :
        id == P9_RLINK ? "RLINK" :
        id == P9_RUNLINKAT ? "RUNLINKAT" :
        id == P9_RFLUSH ? "RFLUSH" :
        id == P9_RREADDIR ? "RREADDIR" :
        "<unknown>";
}

void v9fs_req_wait_for_reply(P9Req *req, uint32_t *len)
{
    QVirtio9P *v9p = req->v9p;

    qvirtio_wait_used_elem(req->qts, v9p->vdev, v9p->vq, req->free_head, len,
                           QVIRTIO_9P_TIMEOUT_US);
}

void v9fs_req_recv(P9Req *req, uint8_t id)
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

void v9fs_req_free(P9Req *req)
{
    guest_free(alloc, req->t_msg);
    guest_free(alloc, req->r_msg);
    g_free(req);
}

/* size[4] Rlerror tag[2] ecode[4] */
void v9fs_rlerror(P9Req *req, uint32_t *err)
{
    v9fs_req_recv(req, P9_RLERROR);
    v9fs_uint32_read(req, err);
    v9fs_req_free(req);
}

/* size[4] Tversion tag[2] msize[4] version[s] */
TVersionRes v9fs_tversion(TVersionOpt opt)
{
    P9Req *req;
    uint32_t err;
    uint32_t body_size = 4;
    uint16_t string_size;
    uint16_t server_len;
    g_autofree char *server_version = NULL;

    g_assert(opt.client);

    if (!opt.msize) {
        opt.msize = P9_MAX_SIZE;
    }

    if (!opt.tag) {
        opt.tag = P9_NOTAG;
    }

    if (!opt.version) {
        opt.version = "9P2000.L";
    }

    string_size = v9fs_string_size(opt.version);
    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;
    req = v9fs_req_init(opt.client, body_size, P9_TVERSION, opt.tag);

    v9fs_uint32_write(req, opt.msize);
    v9fs_string_write(req, opt.version);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rversion(req, &server_len, &server_version);
            g_assert_cmpmem(server_version, server_len,
                            opt.version, strlen(opt.version));
        }
        req = NULL; /* request was freed */
    }

    return (TVersionRes) {
        .req = req,
    };
}

/* size[4] Rversion tag[2] msize[4] version[s] */
void v9fs_rversion(P9Req *req, uint16_t *len, char **version)
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
TAttachRes v9fs_tattach(TAttachOpt opt)
{
    uint32_t err;
    const char *uname = ""; /* ignored by QEMU */
    const char *aname = ""; /* ignored by QEMU */

    g_assert(opt.client);
    /* expecting either Rattach or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !opt.rattach.qid);

    if (!opt.requestOnly) {
        v9fs_tversion((TVersionOpt) { .client = opt.client });
    }

    if (!opt.n_uname) {
        opt.n_uname = getuid();
    }

    P9Req *req = v9fs_req_init(opt.client, 4 + 4 + 2 + 2 + 4, P9_TATTACH,
                               opt.tag);

    v9fs_uint32_write(req, opt.fid);
    v9fs_uint32_write(req, P9_NOFID);
    v9fs_string_write(req, uname);
    v9fs_string_write(req, aname);
    v9fs_uint32_write(req, opt.n_uname);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rattach(req, opt.rattach.qid);
        }
        req = NULL; /* request was freed */
    }

    return (TAttachRes) {
        .req = req,
    };
}

/* size[4] Rattach tag[2] qid[13] */
void v9fs_rattach(P9Req *req, v9fs_qid *qid)
{
    v9fs_req_recv(req, P9_RATTACH);
    if (qid) {
        v9fs_memread(req, qid, 13);
    }
    v9fs_req_free(req);
}

/* size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s]) */
TWalkRes v9fs_twalk(TWalkOpt opt)
{
    P9Req *req;
    int i;
    uint32_t body_size = 4 + 4 + 2;
    uint32_t err;
    char **wnames = NULL;

    g_assert(opt.client);
    /* expecting either high- or low-level path, both not both */
    g_assert(!opt.path || !(opt.nwname || opt.wnames));
    /* expecting either Rwalk or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !(opt.rwalk.nwqid || opt.rwalk.wqid));

    if (!opt.newfid) {
        opt.newfid = genfid();
    }

    if (opt.path) {
        opt.nwname = split(opt.path, "/", &wnames);
        opt.wnames = wnames;
    }

    for (i = 0; i < opt.nwname; i++) {
        uint16_t wname_size = v9fs_string_size(opt.wnames[i]);

        g_assert_cmpint(body_size, <=, UINT32_MAX - wname_size);
        body_size += wname_size;
    }
    req = v9fs_req_init(opt.client, body_size, P9_TWALK, opt.tag);
    v9fs_uint32_write(req, opt.fid);
    v9fs_uint32_write(req, opt.newfid);
    v9fs_uint16_write(req, opt.nwname);
    for (i = 0; i < opt.nwname; i++) {
        v9fs_string_write(req, opt.wnames[i]);
    }
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rwalk(req, opt.rwalk.nwqid, opt.rwalk.wqid);
        }
        req = NULL; /* request was freed */
    }

    split_free(&wnames);

    return (TWalkRes) {
        .newfid = opt.newfid,
        .req = req,
    };
}

/* size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13]) */
void v9fs_rwalk(P9Req *req, uint16_t *nwqid, v9fs_qid **wqid)
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

/* size[4] Tgetattr tag[2] fid[4] request_mask[8] */
TGetAttrRes v9fs_tgetattr(TGetAttrOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either Rgetattr or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !opt.rgetattr.attr);

    if (!opt.request_mask) {
        opt.request_mask = P9_GETATTR_ALL;
    }

    req = v9fs_req_init(opt.client, 4 + 8, P9_TGETATTR, opt.tag);
    v9fs_uint32_write(req, opt.fid);
    v9fs_uint64_write(req, opt.request_mask);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rgetattr(req, opt.rgetattr.attr);
        }
        req = NULL; /* request was freed */
    }

    return (TGetAttrRes) { .req = req };
}

/*
 * size[4] Rgetattr tag[2] valid[8] qid[13] mode[4] uid[4] gid[4] nlink[8]
 *                  rdev[8] size[8] blksize[8] blocks[8]
 *                  atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
 *                  ctime_sec[8] ctime_nsec[8] btime_sec[8] btime_nsec[8]
 *                  gen[8] data_version[8]
 */
void v9fs_rgetattr(P9Req *req, v9fs_attr *attr)
{
    v9fs_req_recv(req, P9_RGETATTR);

    v9fs_uint64_read(req, &attr->valid);
    v9fs_memread(req, &attr->qid, 13);
    v9fs_uint32_read(req, &attr->mode);
    v9fs_uint32_read(req, &attr->uid);
    v9fs_uint32_read(req, &attr->gid);
    v9fs_uint64_read(req, &attr->nlink);
    v9fs_uint64_read(req, &attr->rdev);
    v9fs_uint64_read(req, &attr->size);
    v9fs_uint64_read(req, &attr->blksize);
    v9fs_uint64_read(req, &attr->blocks);
    v9fs_uint64_read(req, &attr->atime_sec);
    v9fs_uint64_read(req, &attr->atime_nsec);
    v9fs_uint64_read(req, &attr->mtime_sec);
    v9fs_uint64_read(req, &attr->mtime_nsec);
    v9fs_uint64_read(req, &attr->ctime_sec);
    v9fs_uint64_read(req, &attr->ctime_nsec);
    v9fs_uint64_read(req, &attr->btime_sec);
    v9fs_uint64_read(req, &attr->btime_nsec);
    v9fs_uint64_read(req, &attr->gen);
    v9fs_uint64_read(req, &attr->data_version);

    v9fs_req_free(req);
}

/*
 * size[4] Tsetattr tag[2] fid[4] valid[4] mode[4] uid[4] gid[4] size[8]
 *                  atime_sec[8] atime_nsec[8] mtime_sec[8] mtime_nsec[8]
 */
TSetAttrRes v9fs_tsetattr(TSetAttrOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);

    req = v9fs_req_init(
        opt.client, 4/*fid*/ + 4/*valid*/ + 4/*mode*/ + 4/*uid*/ + 4/*gid*/ +
        8/*size*/ + 8/*atime_sec*/ + 8/*atime_nsec*/ + 8/*mtime_sec*/ +
        8/*mtime_nsec*/, P9_TSETATTR, opt.tag
    );
    v9fs_uint32_write(req, opt.fid);
    v9fs_uint32_write(req, (uint32_t) opt.attr.valid);
    v9fs_uint32_write(req, opt.attr.mode);
    v9fs_uint32_write(req, opt.attr.uid);
    v9fs_uint32_write(req, opt.attr.gid);
    v9fs_uint64_write(req, opt.attr.size);
    v9fs_uint64_write(req, opt.attr.atime_sec);
    v9fs_uint64_write(req, opt.attr.atime_nsec);
    v9fs_uint64_write(req, opt.attr.mtime_sec);
    v9fs_uint64_write(req, opt.attr.mtime_nsec);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rsetattr(req);
        }
        req = NULL; /* request was freed */
    }

    return (TSetAttrRes) { .req = req };
}

/* size[4] Rsetattr tag[2] */
void v9fs_rsetattr(P9Req *req)
{
    v9fs_req_recv(req, P9_RSETATTR);
    v9fs_req_free(req);
}

/* size[4] Treaddir tag[2] fid[4] offset[8] count[4] */
TReadDirRes v9fs_treaddir(TReadDirOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either Rreaddir or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !(opt.rreaddir.count ||
             opt.rreaddir.nentries || opt.rreaddir.entries));

    req = v9fs_req_init(opt.client, 4 + 8 + 4, P9_TREADDIR, opt.tag);
    v9fs_uint32_write(req, opt.fid);
    v9fs_uint64_write(req, opt.offset);
    v9fs_uint32_write(req, opt.count);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rreaddir(req, opt.rreaddir.count, opt.rreaddir.nentries,
                          opt.rreaddir.entries);
        }
        req = NULL; /* request was freed */
    }

    return (TReadDirRes) { .req = req };
}

/* size[4] Rreaddir tag[2] count[4] data[count] */
void v9fs_rreaddir(P9Req *req, uint32_t *count, uint32_t *nentries,
                   struct V9fsDirent **entries)
{
    uint32_t local_count;
    struct V9fsDirent *e = NULL;
    /* only used to avoid a leak if entries was NULL */
    struct V9fsDirent *unused_entries = NULL;
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
            e = g_new(struct V9fsDirent, 1);
            if (entries) {
                *entries = e;
            } else {
                unused_entries = e;
            }
        } else {
            e = e->next = g_new(struct V9fsDirent, 1);
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

    v9fs_free_dirents(unused_entries);
    v9fs_req_free(req);
}

void v9fs_free_dirents(struct V9fsDirent *e)
{
    struct V9fsDirent *next = NULL;

    for (; e; e = next) {
        next = e->next;
        g_free(e->name);
        g_free(e);
    }
}

/* size[4] Tlopen tag[2] fid[4] flags[4] */
TLOpenRes v9fs_tlopen(TLOpenOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either Rlopen or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !(opt.rlopen.qid || opt.rlopen.iounit));

    req = v9fs_req_init(opt.client,  4 + 4, P9_TLOPEN, opt.tag);
    v9fs_uint32_write(req, opt.fid);
    v9fs_uint32_write(req, opt.flags);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rlopen(req, opt.rlopen.qid, opt.rlopen.iounit);
        }
        req = NULL; /* request was freed */
    }

    return (TLOpenRes) { .req = req };
}

/* size[4] Rlopen tag[2] qid[13] iounit[4] */
void v9fs_rlopen(P9Req *req, v9fs_qid *qid, uint32_t *iounit)
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
TWriteRes v9fs_twrite(TWriteOpt opt)
{
    P9Req *req;
    uint32_t err;
    uint32_t body_size = 4 + 8 + 4;
    uint32_t written = 0;

    g_assert(opt.client);

    g_assert_cmpint(body_size, <=, UINT32_MAX - opt.count);
    body_size += opt.count;
    req = v9fs_req_init(opt.client, body_size, P9_TWRITE, opt.tag);
    v9fs_uint32_write(req, opt.fid);
    v9fs_uint64_write(req, opt.offset);
    v9fs_uint32_write(req, opt.count);
    v9fs_memwrite(req, opt.data, opt.count);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rwrite(req, &written);
        }
        req = NULL; /* request was freed */
    }

    return (TWriteRes) {
        .req = req,
        .count = written
    };
}

/* size[4] Rwrite tag[2] count[4] */
void v9fs_rwrite(P9Req *req, uint32_t *count)
{
    v9fs_req_recv(req, P9_RWRITE);
    if (count) {
        v9fs_uint32_read(req, count);
    }
    v9fs_req_free(req);
}

/* size[4] Tflush tag[2] oldtag[2] */
TFlushRes v9fs_tflush(TFlushOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);

    req = v9fs_req_init(opt.client, 2, P9_TFLUSH, opt.tag);
    v9fs_uint32_write(req, opt.oldtag);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rflush(req);
        }
        req = NULL; /* request was freed */
    }

    return (TFlushRes) { .req = req };
}

/* size[4] Rflush tag[2] */
void v9fs_rflush(P9Req *req)
{
    v9fs_req_recv(req, P9_RFLUSH);
    v9fs_req_free(req);
}

/* size[4] Tmkdir tag[2] dfid[4] name[s] mode[4] gid[4] */
TMkdirRes v9fs_tmkdir(TMkdirOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either hi-level atPath or low-level dfid, but not both */
    g_assert(!opt.atPath || !opt.dfid);
    /* expecting either Rmkdir or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !opt.rmkdir.qid);

    if (opt.atPath) {
        opt.dfid = v9fs_twalk((TWalkOpt) { .client = opt.client,
                                           .path = opt.atPath }).newfid;
    }

    if (!opt.mode) {
        opt.mode = 0750;
    }

    uint32_t body_size = 4 + 4 + 4;
    uint16_t string_size = v9fs_string_size(opt.name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(opt.client, body_size, P9_TMKDIR, opt.tag);
    v9fs_uint32_write(req, opt.dfid);
    v9fs_string_write(req, opt.name);
    v9fs_uint32_write(req, opt.mode);
    v9fs_uint32_write(req, opt.gid);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rmkdir(req, opt.rmkdir.qid);
        }
        req = NULL; /* request was freed */
    }

    return (TMkdirRes) { .req = req };
}

/* size[4] Rmkdir tag[2] qid[13] */
void v9fs_rmkdir(P9Req *req, v9fs_qid *qid)
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
TlcreateRes v9fs_tlcreate(TlcreateOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either hi-level atPath or low-level fid, but not both */
    g_assert(!opt.atPath || !opt.fid);
    /* expecting either Rlcreate or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !(opt.rlcreate.qid || opt.rlcreate.iounit));

    if (opt.atPath) {
        opt.fid = v9fs_twalk((TWalkOpt) { .client = opt.client,
                                          .path = opt.atPath }).newfid;
    }

    if (!opt.mode) {
        opt.mode = 0750;
    }

    uint32_t body_size = 4 + 4 + 4 + 4;
    uint16_t string_size = v9fs_string_size(opt.name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(opt.client, body_size, P9_TLCREATE, opt.tag);
    v9fs_uint32_write(req, opt.fid);
    v9fs_string_write(req, opt.name);
    v9fs_uint32_write(req, opt.flags);
    v9fs_uint32_write(req, opt.mode);
    v9fs_uint32_write(req, opt.gid);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rlcreate(req, opt.rlcreate.qid, opt.rlcreate.iounit);
        }
        req = NULL; /* request was freed */
    }

    return (TlcreateRes) { .req = req };
}

/* size[4] Rlcreate tag[2] qid[13] iounit[4] */
void v9fs_rlcreate(P9Req *req, v9fs_qid *qid, uint32_t *iounit)
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
TsymlinkRes v9fs_tsymlink(TsymlinkOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either hi-level atPath or low-level fid, but not both */
    g_assert(!opt.atPath || !opt.fid);
    /* expecting either Rsymlink or Rlerror, but obviously not both */
    g_assert(!opt.expectErr || !opt.rsymlink.qid);

    if (opt.atPath) {
        opt.fid = v9fs_twalk((TWalkOpt) { .client = opt.client,
                                          .path = opt.atPath }).newfid;
    }

    uint32_t body_size = 4 + 4;
    uint16_t string_size = v9fs_string_size(opt.name) +
                           v9fs_string_size(opt.symtgt);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(opt.client, body_size, P9_TSYMLINK, opt.tag);
    v9fs_uint32_write(req, opt.fid);
    v9fs_string_write(req, opt.name);
    v9fs_string_write(req, opt.symtgt);
    v9fs_uint32_write(req, opt.gid);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rsymlink(req, opt.rsymlink.qid);
        }
        req = NULL; /* request was freed */
    }

    return (TsymlinkRes) { .req = req };
}

/* size[4] Rsymlink tag[2] qid[13] */
void v9fs_rsymlink(P9Req *req, v9fs_qid *qid)
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
TlinkRes v9fs_tlink(TlinkOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either hi-level atPath or low-level dfid, but not both */
    g_assert(!opt.atPath || !opt.dfid);
    /* expecting either hi-level toPath or low-level fid, but not both */
    g_assert(!opt.toPath || !opt.fid);

    if (opt.atPath) {
        opt.dfid = v9fs_twalk((TWalkOpt) { .client = opt.client,
                                           .path = opt.atPath }).newfid;
    }
    if (opt.toPath) {
        opt.fid = v9fs_twalk((TWalkOpt) { .client = opt.client,
                                          .path = opt.toPath }).newfid;
    }

    uint32_t body_size = 4 + 4;
    uint16_t string_size = v9fs_string_size(opt.name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(opt.client, body_size, P9_TLINK, opt.tag);
    v9fs_uint32_write(req, opt.dfid);
    v9fs_uint32_write(req, opt.fid);
    v9fs_string_write(req, opt.name);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_rlink(req);
        }
        req = NULL; /* request was freed */
    }

    return (TlinkRes) { .req = req };
}

/* size[4] Rlink tag[2] */
void v9fs_rlink(P9Req *req)
{
    v9fs_req_recv(req, P9_RLINK);
    v9fs_req_free(req);
}

/* size[4] Tunlinkat tag[2] dirfd[4] name[s] flags[4] */
TunlinkatRes v9fs_tunlinkat(TunlinkatOpt opt)
{
    P9Req *req;
    uint32_t err;

    g_assert(opt.client);
    /* expecting either hi-level atPath or low-level dirfd, but not both */
    g_assert(!opt.atPath || !opt.dirfd);

    if (opt.atPath) {
        opt.dirfd = v9fs_twalk((TWalkOpt) { .client = opt.client,
                                            .path = opt.atPath }).newfid;
    }

    uint32_t body_size = 4 + 4;
    uint16_t string_size = v9fs_string_size(opt.name);

    g_assert_cmpint(body_size, <=, UINT32_MAX - string_size);
    body_size += string_size;

    req = v9fs_req_init(opt.client, body_size, P9_TUNLINKAT, opt.tag);
    v9fs_uint32_write(req, opt.dirfd);
    v9fs_string_write(req, opt.name);
    v9fs_uint32_write(req, opt.flags);
    v9fs_req_send(req);

    if (!opt.requestOnly) {
        v9fs_req_wait_for_reply(req, NULL);
        if (opt.expectErr) {
            v9fs_rlerror(req, &err);
            g_assert_cmpint(err, ==, opt.expectErr);
        } else {
            v9fs_runlinkat(req);
        }
        req = NULL; /* request was freed */
    }

    return (TunlinkatRes) { .req = req };
}

/* size[4] Runlinkat tag[2] */
void v9fs_runlinkat(P9Req *req)
{
    v9fs_req_recv(req, P9_RUNLINKAT);
    v9fs_req_free(req);
}
