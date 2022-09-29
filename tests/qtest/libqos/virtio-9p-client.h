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

#ifndef TESTS_LIBQOS_VIRTIO_9P_CLIENT_H
#define TESTS_LIBQOS_VIRTIO_9P_CLIENT_H

#include "hw/9pfs/9p.h"
#include "hw/9pfs/9p-synth.h"
#include "virtio-9p.h"
#include "qgraph.h"
#include "tests/qtest/libqtest-single.h"

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

/* type[1] version[4] path[8] */
typedef char v9fs_qid[13];

typedef struct v9fs_attr {
    uint64_t valid;
    v9fs_qid qid;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t nlink;
    uint64_t rdev;
    uint64_t size;
    uint64_t blksize;
    uint64_t blocks;
    uint64_t atime_sec;
    uint64_t atime_nsec;
    uint64_t mtime_sec;
    uint64_t mtime_nsec;
    uint64_t ctime_sec;
    uint64_t ctime_nsec;
    uint64_t btime_sec;
    uint64_t btime_nsec;
    uint64_t gen;
    uint64_t data_version;
} v9fs_attr;

#define P9_GETATTR_BASIC    0x000007ffULL /* Mask for fields up to BLOCKS */

struct V9fsDirent {
    v9fs_qid qid;
    uint64_t offset;
    uint8_t type;
    char *name;
    struct V9fsDirent *next;
};

void v9fs_set_allocator(QGuestAllocator *t_alloc);
void v9fs_memwrite(P9Req *req, const void *addr, size_t len);
void v9fs_memskip(P9Req *req, size_t len);
void v9fs_memread(P9Req *req, void *addr, size_t len);
void v9fs_uint8_read(P9Req *req, uint8_t *val);
void v9fs_uint16_write(P9Req *req, uint16_t val);
void v9fs_uint16_read(P9Req *req, uint16_t *val);
void v9fs_uint32_write(P9Req *req, uint32_t val);
void v9fs_uint64_write(P9Req *req, uint64_t val);
void v9fs_uint32_read(P9Req *req, uint32_t *val);
void v9fs_uint64_read(P9Req *req, uint64_t *val);
uint16_t v9fs_string_size(const char *string);
void v9fs_string_write(P9Req *req, const char *string);
void v9fs_string_read(P9Req *req, uint16_t *len, char **string);
P9Req *v9fs_req_init(QVirtio9P *v9p, uint32_t size, uint8_t id,
                     uint16_t tag);
void v9fs_req_send(P9Req *req);
void v9fs_req_wait_for_reply(P9Req *req, uint32_t *len);
void v9fs_req_recv(P9Req *req, uint8_t id);
void v9fs_req_free(P9Req *req);
void v9fs_rlerror(P9Req *req, uint32_t *err);
P9Req *v9fs_tversion(QVirtio9P *v9p, uint32_t msize, const char *version,
                     uint16_t tag);
void v9fs_rversion(P9Req *req, uint16_t *len, char **version);
P9Req *v9fs_tattach(QVirtio9P *v9p, uint32_t fid, uint32_t n_uname,
                    uint16_t tag);
void v9fs_rattach(P9Req *req, v9fs_qid *qid);
P9Req *v9fs_twalk(QVirtio9P *v9p, uint32_t fid, uint32_t newfid,
                  uint16_t nwname, char *const wnames[], uint16_t tag);
void v9fs_rwalk(P9Req *req, uint16_t *nwqid, v9fs_qid **wqid);
P9Req *v9fs_tgetattr(QVirtio9P *v9p, uint32_t fid, uint64_t request_mask,
                     uint16_t tag);
void v9fs_rgetattr(P9Req *req, v9fs_attr *attr);
P9Req *v9fs_treaddir(QVirtio9P *v9p, uint32_t fid, uint64_t offset,
                     uint32_t count, uint16_t tag);
void v9fs_rreaddir(P9Req *req, uint32_t *count, uint32_t *nentries,
                   struct V9fsDirent **entries);
void v9fs_free_dirents(struct V9fsDirent *e);
P9Req *v9fs_tlopen(QVirtio9P *v9p, uint32_t fid, uint32_t flags,
                   uint16_t tag);
void v9fs_rlopen(P9Req *req, v9fs_qid *qid, uint32_t *iounit);
P9Req *v9fs_twrite(QVirtio9P *v9p, uint32_t fid, uint64_t offset,
                   uint32_t count, const void *data, uint16_t tag);
void v9fs_rwrite(P9Req *req, uint32_t *count);
P9Req *v9fs_tflush(QVirtio9P *v9p, uint16_t oldtag, uint16_t tag);
void v9fs_rflush(P9Req *req);
P9Req *v9fs_tmkdir(QVirtio9P *v9p, uint32_t dfid, const char *name,
                   uint32_t mode, uint32_t gid, uint16_t tag);
void v9fs_rmkdir(P9Req *req, v9fs_qid *qid);
P9Req *v9fs_tlcreate(QVirtio9P *v9p, uint32_t fid, const char *name,
                     uint32_t flags, uint32_t mode, uint32_t gid,
                     uint16_t tag);
void v9fs_rlcreate(P9Req *req, v9fs_qid *qid, uint32_t *iounit);
P9Req *v9fs_tsymlink(QVirtio9P *v9p, uint32_t fid, const char *name,
                     const char *symtgt, uint32_t gid, uint16_t tag);
void v9fs_rsymlink(P9Req *req, v9fs_qid *qid);
P9Req *v9fs_tlink(QVirtio9P *v9p, uint32_t dfid, uint32_t fid,
                  const char *name, uint16_t tag);
void v9fs_rlink(P9Req *req);
P9Req *v9fs_tunlinkat(QVirtio9P *v9p, uint32_t dirfd, const char *name,
                      uint32_t flags, uint16_t tag);
void v9fs_runlinkat(P9Req *req);

#endif
