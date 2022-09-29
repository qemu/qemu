/*
 * QTest testcase for VirtIO 9P
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
#include "qemu/module.h"
#include "libqos/virtio-9p-client.h"

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
    v9fs_set_allocator(t_alloc);
    size_t tag_len = qvirtio_config_readw(v9p->vdev, 0);
    g_autofree char *tag = NULL;
    int i;

    g_assert_cmpint(tag_len, ==, strlen(MOUNT_TAG));

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(v9p->vdev, i + 2);
    }
    g_assert_cmpmem(tag, tag_len, MOUNT_TAG, tag_len);
}

static inline bool is_same_qid(v9fs_qid a, v9fs_qid b)
{
    /* don't compare QID version for checking for file ID equalness */
    return a[0] == b[0] && memcmp(&a[5], &b[5], 8) == 0;
}

static void do_version(QVirtio9P *v9p)
{
    const char *version = "9P2000.L";
    uint16_t server_len;
    g_autofree char *server_version = NULL;
    P9Req *req;

    req = v9fs_tversion(v9p, P9_MAX_SIZE, version, P9_NOTAG);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rversion(req, &server_len, &server_version);

    g_assert_cmpmem(server_version, server_len, version, strlen(version));
}

/*
 * utility function: walk to requested dir and return fid for that dir and
 * the QIDs of server response
 */
static uint32_t do_walk_rqids(QVirtio9P *v9p, const char *path, uint16_t *nwqid,
                              v9fs_qid **wqid)
{
    char **wnames;
    P9Req *req;
    const uint32_t fid = genfid();

    int nwnames = split(path, "/", &wnames);

    req = v9fs_twalk(v9p, 0, fid, nwnames, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, nwqid, wqid);

    split_free(&wnames);
    return fid;
}

/* utility function: walk to requested dir and return fid for that dir */
static uint32_t do_walk(QVirtio9P *v9p, const char *path)
{
    return do_walk_rqids(v9p, path, NULL, NULL);
}

/* utility function: walk to requested dir and expect passed error response */
static void do_walk_expect_error(QVirtio9P *v9p, const char *path, uint32_t err)
{
    char **wnames;
    P9Req *req;
    uint32_t _err;
    const uint32_t fid = genfid();

    int nwnames = split(path, "/", &wnames);

    req = v9fs_twalk(v9p, 0, fid, nwnames, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlerror(req, &_err);

    g_assert_cmpint(_err, ==, err);

    split_free(&wnames);
}

static void fs_version(void *obj, void *data, QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_version(obj);
}

static void do_attach_rqid(QVirtio9P *v9p, v9fs_qid *qid)
{
    P9Req *req;

    do_version(v9p);
    req = v9fs_tattach(v9p, 0, getuid(), 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rattach(req, qid);
}

static void do_attach(QVirtio9P *v9p)
{
    do_attach_rqid(v9p, NULL);
}

static void fs_attach(void *obj, void *data, QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_attach(obj);
}

static void fs_walk(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *wnames[P9_MAXWELEM];
    uint16_t nwqid;
    g_autofree v9fs_qid *wqid = NULL;
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

/* basic readdir test where reply fits into a single response message */
static void fs_readdir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
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
        g_autofree char *name =
            g_strdup_printf(QTEST_V9FS_SYNTH_READDIR_FILE, i);
        g_assert_cmpint(fs_dirents_contain_name(entries, name), ==, true);
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
    v9fs_set_allocator(t_alloc);
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

static void fs_walk_nonexistent(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);

    do_attach(v9p);
    /*
     * The 9p2000 protocol spec says: "If the first element cannot be walked
     * for any reason, Rerror is returned."
     */
    do_walk_expect_error(v9p, "non-existent", ENOENT);
}

static void fs_walk_2nd_nonexistent(void *obj, void *data,
                                    QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    v9fs_qid root_qid;
    uint16_t nwqid;
    uint32_t fid, err;
    P9Req *req;
    g_autofree v9fs_qid *wqid = NULL;
    g_autofree char *path = g_strdup_printf(
        QTEST_V9FS_SYNTH_WALK_FILE "/non-existent", 0
    );

    do_attach_rqid(v9p, &root_qid);
    fid = do_walk_rqids(v9p, path, &nwqid, &wqid);
    /*
     * The 9p2000 protocol spec says: "nwqid is therefore either nwname or the
     * index of the first elementwise walk that failed."
     */
    assert(nwqid == 1);

    /* returned QID wqid[0] is file ID of 1st subdir */
    g_assert(wqid && wqid[0] && !is_same_qid(root_qid, wqid[0]));

    /* expect fid being unaffected by walk above */
    req = v9fs_tgetattr(v9p, fid, P9_GETATTR_BASIC, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlerror(req, &err);

    g_assert_cmpint(err, ==, ENOENT);
}

static void fs_walk_none(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    v9fs_qid root_qid;
    g_autofree v9fs_qid *wqid = NULL;
    P9Req *req;
    struct v9fs_attr attr;

    do_version(v9p);
    req = v9fs_tattach(v9p, 0, getuid(), 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rattach(req, &root_qid);

    req = v9fs_twalk(v9p, 0, 1, 0, NULL, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, &wqid);

    /* special case: no QID is returned if nwname=0 was sent */
    g_assert(wqid == NULL);

    req = v9fs_tgetattr(v9p, 1, P9_GETATTR_BASIC, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rgetattr(req, &attr);

    g_assert(is_same_qid(root_qid, attr.qid));
}

static void fs_walk_dotdot(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    char *const wnames[] = { g_strdup("..") };
    v9fs_qid root_qid;
    g_autofree v9fs_qid *wqid = NULL;
    P9Req *req;

    do_version(v9p);
    req = v9fs_tattach(v9p, 0, getuid(), 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rattach(req, &root_qid);

    req = v9fs_twalk(v9p, 0, 1, 1, wnames, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rwalk(req, NULL, &wqid); /* We now we'll get one qid */

    g_assert_cmpmem(&root_qid, 13, wqid[0], 13);

    g_free(wnames[0]);
}

static void fs_lopen(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
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
    v9fs_set_allocator(t_alloc);
    static const uint32_t write_count = P9_MAX_SIZE / 2;
    char *const wnames[] = { g_strdup(QTEST_V9FS_SYNTH_WRITE_FILE) };
    g_autofree char *buf = g_malloc0(write_count);
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

    g_free(wnames[0]);
}

static void fs_flush_success(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
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
    v9fs_set_allocator(t_alloc);
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
    g_autofree char *name = g_strdup(cname);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, path);

    req = v9fs_tmkdir(v9p, fid, name, 0750, 0, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rmkdir(req, NULL);
}

/* create a regular file with Tlcreate and return file's fid */
static uint32_t do_lcreate(QVirtio9P *v9p, const char *path,
                           const char *cname)
{
    g_autofree char *name = g_strdup(cname);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, path);

    req = v9fs_tlcreate(v9p, fid, name, 0, 0750, 0, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rlcreate(req, NULL, NULL);

    return fid;
}

/* create symlink named @a clink in directory @a path pointing to @a to */
static void do_symlink(QVirtio9P *v9p, const char *path, const char *clink,
                       const char *to)
{
    g_autofree char *name = g_strdup(clink);
    g_autofree char *dst = g_strdup(to);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, path);

    req = v9fs_tsymlink(v9p, fid, name, dst, 0, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_rsymlink(req, NULL);
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
    g_autofree char *name = g_strdup(rpath);
    uint32_t fid;
    P9Req *req;

    fid = do_walk(v9p, atpath);

    req = v9fs_tunlinkat(v9p, fid, name, flags, 0);
    v9fs_req_wait_for_reply(req, NULL);
    v9fs_runlinkat(req);
}

static void fs_readdir_split_128(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_readdir_split(obj, 128);
}

static void fs_readdir_split_256(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_readdir_split(obj, 256);
}

static void fs_readdir_split_512(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    v9fs_set_allocator(t_alloc);
    do_readdir_split(obj, 512);
}


/* tests using the 9pfs 'local' fs driver */

static void fs_create_dir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *root_path = virtio_9p_test_path("");
    g_autofree char *new_dir = virtio_9p_test_path("01");

    g_assert(root_path != NULL);

    do_attach(v9p);
    do_mkdir(v9p, "/", "01");

    /* check if created directory really exists now ... */
    g_assert(stat(new_dir, &st) == 0);
    /* ... and is actually a directory */
    g_assert((st.st_mode & S_IFMT) == S_IFDIR);
}

static void fs_unlinkat_dir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *root_path = virtio_9p_test_path("");
    g_autofree char *new_dir = virtio_9p_test_path("02");

    g_assert(root_path != NULL);

    do_attach(v9p);
    do_mkdir(v9p, "/", "02");

    /* check if created directory really exists now ... */
    g_assert(stat(new_dir, &st) == 0);
    /* ... and is actually a directory */
    g_assert((st.st_mode & S_IFMT) == S_IFDIR);

    do_unlinkat(v9p, "/", "02", P9_DOTL_AT_REMOVEDIR);
    /* directory should be gone now */
    g_assert(stat(new_dir, &st) != 0);
}

static void fs_create_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *new_file = virtio_9p_test_path("03/1st_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "03");
    do_lcreate(v9p, "03", "1st_file");

    /* check if created file exists now ... */
    g_assert(stat(new_file, &st) == 0);
    /* ... and is a regular file */
    g_assert((st.st_mode & S_IFMT) == S_IFREG);
}

static void fs_unlinkat_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *new_file = virtio_9p_test_path("04/doa_file");

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
}

static void fs_symlink_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *real_file = virtio_9p_test_path("05/real_file");
    g_autofree char *symlink_file = virtio_9p_test_path("05/symlink_file");

    do_attach(v9p);
    do_mkdir(v9p, "/", "05");
    do_lcreate(v9p, "05", "real_file");
    g_assert(stat(real_file, &st) == 0);
    g_assert((st.st_mode & S_IFMT) == S_IFREG);

    do_symlink(v9p, "05", "symlink_file", "real_file");

    /* check if created link exists now */
    g_assert(stat(symlink_file, &st) == 0);
}

static void fs_unlinkat_symlink(void *obj, void *data,
                                QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st;
    g_autofree char *real_file = virtio_9p_test_path("06/real_file");
    g_autofree char *symlink_file = virtio_9p_test_path("06/symlink_file");

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
}

static void fs_hardlink_file(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st_real, st_link;
    g_autofree char *real_file = virtio_9p_test_path("07/real_file");
    g_autofree char *hardlink_file = virtio_9p_test_path("07/hardlink_file");

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
}

static void fs_unlinkat_hardlink(void *obj, void *data,
                                 QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    v9fs_set_allocator(t_alloc);
    struct stat st_real, st_link;
    g_autofree char *real_file = virtio_9p_test_path("08/real_file");
    g_autofree char *hardlink_file = virtio_9p_test_path("08/hardlink_file");

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
    qos_add_test("synth/walk/none", "virtio-9p", fs_walk_none, &opts);
    qos_add_test("synth/walk/dotdot_from_root", "virtio-9p",
                 fs_walk_dotdot,  &opts);
    qos_add_test("synth/walk/non_existent", "virtio-9p", fs_walk_nonexistent,
                  &opts);
    qos_add_test("synth/walk/2nd_non_existent", "virtio-9p",
                 fs_walk_2nd_nonexistent, &opts);
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
