#ifndef QEMU_9P_MARSHAL_H
#define QEMU_9P_MARSHAL_H

typedef struct V9fsString {
    uint16_t size;
    char *data;
} V9fsString;

typedef struct V9fsQID {
    uint8_t type;
    uint32_t version;
    uint64_t path;
} V9fsQID;

typedef struct V9fsStat {
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

typedef struct V9fsIattr {
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
void v9fs_string_free(V9fsString *str);
void v9fs_string_sprintf(V9fsString *str, const char *fmt, ...);
void v9fs_string_copy(V9fsString *lhs, V9fsString *rhs);

#endif
