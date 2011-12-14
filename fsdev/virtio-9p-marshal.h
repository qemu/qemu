#ifndef _QEMU_VIRTIO_9P_MARSHAL_H
#define _QEMU_VIRTIO_9P_MARSHAL_H

typedef struct V9fsString
{
    uint16_t size;
    char *data;
} V9fsString;

typedef struct V9fsQID
{
    int8_t type;
    int32_t version;
    int64_t path;
} V9fsQID;

typedef struct V9fsStat
{
    int16_t size;
    int16_t type;
    int32_t dev;
    V9fsQID qid;
    int32_t mode;
    int32_t atime;
    int32_t mtime;
    int64_t length;
    V9fsString name;
    V9fsString uid;
    V9fsString gid;
    V9fsString muid;
    /* 9p2000.u */
    V9fsString extension;
   int32_t n_uid;
    int32_t n_gid;
    int32_t n_muid;
} V9fsStat;

typedef struct V9fsIattr
{
    int32_t valid;
    int32_t mode;
    int32_t uid;
    int32_t gid;
    int64_t size;
    int64_t atime_sec;
    int64_t atime_nsec;
    int64_t mtime_sec;
    int64_t mtime_nsec;
} V9fsIattr;

typedef struct V9fsStatDotl {
    uint64_t st_result_mask;
    V9fsQID qid;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_nlink;
    uint64_t st_rdev;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
    uint64_t st_atime_sec;
    uint64_t st_atime_nsec;
    uint64_t st_mtime_sec;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime_sec;
    uint64_t st_ctime_nsec;
    uint64_t st_btime_sec;
    uint64_t st_btime_nsec;
    uint64_t st_gen;
    uint64_t st_data_version;
} V9fsStatDotl;

static inline void v9fs_string_init(V9fsString *str)
{
    str->data = NULL;
    str->size = 0;
}
extern void v9fs_string_free(V9fsString *str);
extern void v9fs_string_null(V9fsString *str);
extern void v9fs_string_sprintf(V9fsString *str, const char *fmt, ...);
extern void v9fs_string_copy(V9fsString *lhs, V9fsString *rhs);

ssize_t v9fs_pack(struct iovec *in_sg, int in_num, size_t offset,
                  const void *src, size_t size);
ssize_t v9fs_unmarshal(struct iovec *out_sg, int out_num, size_t offset,
                       int bswap, const char *fmt, ...);
ssize_t v9fs_marshal(struct iovec *in_sg, int in_num, size_t offset,
                     int bswap, const char *fmt, ...);
#endif
