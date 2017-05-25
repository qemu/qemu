/*
 * 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <glib/gprintf.h>
#include <utime.h>

#include "9p-iov-marshal.h"
#include "qemu/bswap.h"

static ssize_t v9fs_packunpack(void *addr, struct iovec *sg, int sg_count,
                               size_t offset, size_t size, int pack)
{
    int i = 0;
    size_t copied = 0;
    size_t req_size = size;


    for (i = 0; size && i < sg_count; i++) {
        size_t len;
        if (offset >= sg[i].iov_len) {
            /* skip this sg */
            offset -= sg[i].iov_len;
            continue;
        } else {
            len = MIN(sg[i].iov_len - offset, size);
            if (pack) {
                memcpy(sg[i].iov_base + offset, addr, len);
            } else {
                memcpy(addr, sg[i].iov_base + offset, len);
            }
            size -= len;
            copied += len;
            addr += len;
            if (size) {
                offset = 0;
                continue;
            }
        }
    }
    if (copied < req_size) {
        /*
         * We copied less that requested size. error out
         */
        return -ENOBUFS;
    }
    return copied;
}

static ssize_t v9fs_unpack(void *dst, struct iovec *out_sg, int out_num,
                           size_t offset, size_t size)
{
    return v9fs_packunpack(dst, out_sg, out_num, offset, size, 0);
}

ssize_t v9fs_pack(struct iovec *in_sg, int in_num, size_t offset,
                  const void *src, size_t size)
{
    return v9fs_packunpack((void *)src, in_sg, in_num, offset, size, 1);
}

ssize_t v9fs_iov_vunmarshal(struct iovec *out_sg, int out_num, size_t offset,
                            int bswap, const char *fmt, va_list ap)
{
    int i;
    ssize_t copied = 0;
    size_t old_offset = offset;

    for (i = 0; fmt[i]; i++) {
        switch (fmt[i]) {
        case 'b': {
            uint8_t *valp = va_arg(ap, uint8_t *);
            copied = v9fs_unpack(valp, out_sg, out_num, offset, sizeof(*valp));
            break;
        }
        case 'w': {
            uint16_t val, *valp;
            valp = va_arg(ap, uint16_t *);
            copied = v9fs_unpack(&val, out_sg, out_num, offset, sizeof(val));
            if (bswap) {
                *valp = le16_to_cpu(val);
            } else {
                *valp = val;
            }
            break;
        }
        case 'd': {
            uint32_t val, *valp;
            valp = va_arg(ap, uint32_t *);
            copied = v9fs_unpack(&val, out_sg, out_num, offset, sizeof(val));
            if (bswap) {
                *valp = le32_to_cpu(val);
            } else {
                *valp = val;
            }
            break;
        }
        case 'q': {
            uint64_t val, *valp;
            valp = va_arg(ap, uint64_t *);
            copied = v9fs_unpack(&val, out_sg, out_num, offset, sizeof(val));
            if (bswap) {
                *valp = le64_to_cpu(val);
            } else {
                *valp = val;
            }
            break;
        }
        case 's': {
            V9fsString *str = va_arg(ap, V9fsString *);
            copied = v9fs_iov_unmarshal(out_sg, out_num, offset, bswap,
                                        "w", &str->size);
            if (copied > 0) {
                offset += copied;
                str->data = g_malloc(str->size + 1);
                copied = v9fs_unpack(str->data, out_sg, out_num, offset,
                                     str->size);
                if (copied >= 0) {
                    str->data[str->size] = 0;
                } else {
                    v9fs_string_free(str);
                }
            }
            break;
        }
        case 'Q': {
            V9fsQID *qidp = va_arg(ap, V9fsQID *);
            copied = v9fs_iov_unmarshal(out_sg, out_num, offset, bswap,
                                        "bdq", &qidp->type, &qidp->version,
                                        &qidp->path);
            break;
        }
        case 'S': {
            V9fsStat *statp = va_arg(ap, V9fsStat *);
            copied = v9fs_iov_unmarshal(out_sg, out_num, offset, bswap,
                                        "wwdQdddqsssssddd",
                                        &statp->size, &statp->type,
                                        &statp->dev, &statp->qid,
                                        &statp->mode, &statp->atime,
                                        &statp->mtime, &statp->length,
                                        &statp->name, &statp->uid,
                                        &statp->gid, &statp->muid,
                                        &statp->extension,
                                        &statp->n_uid, &statp->n_gid,
                                        &statp->n_muid);
            break;
        }
        case 'I': {
            V9fsIattr *iattr = va_arg(ap, V9fsIattr *);
            copied = v9fs_iov_unmarshal(out_sg, out_num, offset, bswap,
                                        "ddddqqqqq",
                                        &iattr->valid, &iattr->mode,
                                        &iattr->uid, &iattr->gid,
                                        &iattr->size, &iattr->atime_sec,
                                        &iattr->atime_nsec,
                                        &iattr->mtime_sec,
                                        &iattr->mtime_nsec);
            break;
        }
        default:
            g_assert_not_reached();
        }
        if (copied < 0) {
            return copied;
        }
        offset += copied;
    }

    return offset - old_offset;
}

ssize_t v9fs_iov_unmarshal(struct iovec *out_sg, int out_num, size_t offset,
                           int bswap, const char *fmt, ...)
{
    ssize_t ret;
    va_list ap;

    va_start(ap, fmt);
    ret = v9fs_iov_vunmarshal(out_sg, out_num, offset, bswap, fmt, ap);
    va_end(ap);

    return ret;
}

ssize_t v9fs_iov_vmarshal(struct iovec *in_sg, int in_num, size_t offset,
                          int bswap, const char *fmt, va_list ap)
{
    int i;
    ssize_t copied = 0;
    size_t old_offset = offset;

    for (i = 0; fmt[i]; i++) {
        switch (fmt[i]) {
        case 'b': {
            uint8_t val = va_arg(ap, int);
            copied = v9fs_pack(in_sg, in_num, offset, &val, sizeof(val));
            break;
        }
        case 'w': {
            uint16_t val = va_arg(ap, int);
            if (bswap) {
                val = cpu_to_le16(val);
            }
            copied = v9fs_pack(in_sg, in_num, offset, &val, sizeof(val));
            break;
        }
        case 'd': {
            uint32_t val = va_arg(ap, uint32_t);
            if (bswap) {
                val = cpu_to_le32(val);
            }
            copied = v9fs_pack(in_sg, in_num, offset, &val, sizeof(val));
            break;
        }
        case 'q': {
            uint64_t val = va_arg(ap, uint64_t);
            if (bswap) {
                val = cpu_to_le64(val);
            }
            copied = v9fs_pack(in_sg, in_num, offset, &val, sizeof(val));
            break;
        }
        case 's': {
            V9fsString *str = va_arg(ap, V9fsString *);
            copied = v9fs_iov_marshal(in_sg, in_num, offset, bswap,
                                      "w", str->size);
            if (copied > 0) {
                offset += copied;
                copied = v9fs_pack(in_sg, in_num, offset, str->data, str->size);
            }
            break;
        }
        case 'Q': {
            V9fsQID *qidp = va_arg(ap, V9fsQID *);
            copied = v9fs_iov_marshal(in_sg, in_num, offset, bswap, "bdq",
                                      qidp->type, qidp->version,
                                      qidp->path);
            break;
        }
        case 'S': {
            V9fsStat *statp = va_arg(ap, V9fsStat *);
            copied = v9fs_iov_marshal(in_sg, in_num, offset, bswap,
                                      "wwdQdddqsssssddd",
                                      statp->size, statp->type, statp->dev,
                                      &statp->qid, statp->mode, statp->atime,
                                      statp->mtime, statp->length,
                                      &statp->name,
                                      &statp->uid, &statp->gid, &statp->muid,
                                      &statp->extension, statp->n_uid,
                                      statp->n_gid, statp->n_muid);
            break;
        }
        case 'A': {
            V9fsStatDotl *statp = va_arg(ap, V9fsStatDotl *);
            copied = v9fs_iov_marshal(in_sg, in_num, offset, bswap,
                                      "qQdddqqqqqqqqqqqqqqq",
                                      statp->st_result_mask,
                                      &statp->qid, statp->st_mode,
                                      statp->st_uid, statp->st_gid,
                                      statp->st_nlink, statp->st_rdev,
                                      statp->st_size, statp->st_blksize,
                                      statp->st_blocks, statp->st_atime_sec,
                                      statp->st_atime_nsec,
                                      statp->st_mtime_sec,
                                      statp->st_mtime_nsec,
                                      statp->st_ctime_sec,
                                      statp->st_ctime_nsec,
                                      statp->st_btime_sec,
                                      statp->st_btime_nsec, statp->st_gen,
                                      statp->st_data_version);
            break;
        }
        default:
            g_assert_not_reached();
        }
        if (copied < 0) {
            return copied;
        }
        offset += copied;
    }

    return offset - old_offset;
}

ssize_t v9fs_iov_marshal(struct iovec *in_sg, int in_num, size_t offset,
                         int bswap, const char *fmt, ...)
{
    ssize_t ret;
    va_list ap;

    va_start(ap, fmt);
    ret = v9fs_iov_vmarshal(in_sg, in_num, offset, bswap, fmt, ap);
    va_end(ap);

    return ret;
}
