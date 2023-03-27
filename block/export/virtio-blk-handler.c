/*
 * Handler for virtio-blk I/O
 *
 * Copyright (c) 2020 Red Hat, Inc.
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Coiby Xu <coiby.xu@gmail.com>
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "virtio-blk-handler.h"

#include "standard-headers/linux/virtio_blk.h"

struct virtio_blk_inhdr {
    unsigned char status;
};

static bool coroutine_fn
virtio_blk_sect_range_ok(BlockBackend *blk, uint32_t block_size,
                         uint64_t sector, size_t size)
{
    uint64_t nb_sectors;
    uint64_t total_sectors;

    if (size % VIRTIO_BLK_SECTOR_SIZE) {
        return false;
    }

    nb_sectors = size >> VIRTIO_BLK_SECTOR_BITS;

    QEMU_BUILD_BUG_ON(BDRV_SECTOR_SIZE != VIRTIO_BLK_SECTOR_SIZE);
    if (nb_sectors > BDRV_REQUEST_MAX_SECTORS) {
        return false;
    }
    if ((sector << VIRTIO_BLK_SECTOR_BITS) % block_size) {
        return false;
    }
    blk_co_get_geometry(blk, &total_sectors);
    if (sector > total_sectors || nb_sectors > total_sectors - sector) {
        return false;
    }
    return true;
}

static int coroutine_fn
virtio_blk_discard_write_zeroes(VirtioBlkHandler *handler, struct iovec *iov,
                                uint32_t iovcnt, uint32_t type)
{
    BlockBackend *blk = handler->blk;
    struct virtio_blk_discard_write_zeroes desc;
    ssize_t size;
    uint64_t sector;
    uint32_t num_sectors;
    uint32_t max_sectors;
    uint32_t flags;
    int bytes;

    /* Only one desc is currently supported */
    if (unlikely(iov_size(iov, iovcnt) > sizeof(desc))) {
        return VIRTIO_BLK_S_UNSUPP;
    }

    size = iov_to_buf(iov, iovcnt, 0, &desc, sizeof(desc));
    if (unlikely(size != sizeof(desc))) {
        error_report("Invalid size %zd, expected %zu", size, sizeof(desc));
        return VIRTIO_BLK_S_IOERR;
    }

    sector = le64_to_cpu(desc.sector);
    num_sectors = le32_to_cpu(desc.num_sectors);
    flags = le32_to_cpu(desc.flags);
    max_sectors = (type == VIRTIO_BLK_T_WRITE_ZEROES) ?
                  VIRTIO_BLK_MAX_WRITE_ZEROES_SECTORS :
                  VIRTIO_BLK_MAX_DISCARD_SECTORS;

    /* This check ensures that 'bytes' fits in an int */
    if (unlikely(num_sectors > max_sectors)) {
        return VIRTIO_BLK_S_IOERR;
    }

    bytes = num_sectors << VIRTIO_BLK_SECTOR_BITS;

    if (unlikely(!virtio_blk_sect_range_ok(blk, handler->logical_block_size,
                                           sector, bytes))) {
        return VIRTIO_BLK_S_IOERR;
    }

    /*
     * The device MUST set the status byte to VIRTIO_BLK_S_UNSUPP for discard
     * and write zeroes commands if any unknown flag is set.
     */
    if (unlikely(flags & ~VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP)) {
        return VIRTIO_BLK_S_UNSUPP;
    }

    if (type == VIRTIO_BLK_T_WRITE_ZEROES) {
        int blk_flags = 0;

        if (flags & VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP) {
            blk_flags |= BDRV_REQ_MAY_UNMAP;
        }

        if (blk_co_pwrite_zeroes(blk, sector << VIRTIO_BLK_SECTOR_BITS,
                                 bytes, blk_flags) == 0) {
            return VIRTIO_BLK_S_OK;
        }
    } else if (type == VIRTIO_BLK_T_DISCARD) {
        /*
         * The device MUST set the status byte to VIRTIO_BLK_S_UNSUPP for
         * discard commands if the unmap flag is set.
         */
        if (unlikely(flags & VIRTIO_BLK_WRITE_ZEROES_FLAG_UNMAP)) {
            return VIRTIO_BLK_S_UNSUPP;
        }

        if (blk_co_pdiscard(blk, sector << VIRTIO_BLK_SECTOR_BITS,
                            bytes) == 0) {
            return VIRTIO_BLK_S_OK;
        }
    }

    return VIRTIO_BLK_S_IOERR;
}

int coroutine_fn virtio_blk_process_req(VirtioBlkHandler *handler,
                                        struct iovec *in_iov,
                                        struct iovec *out_iov,
                                        unsigned int in_num,
                                        unsigned int out_num)
{
    BlockBackend *blk = handler->blk;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr out;
    uint32_t type;
    int in_len;

    if (out_num < 1 || in_num < 1) {
        error_report("virtio-blk request missing headers");
        return -EINVAL;
    }

    if (unlikely(iov_to_buf(out_iov, out_num, 0, &out,
                            sizeof(out)) != sizeof(out))) {
        error_report("virtio-blk request outhdr too short");
        return -EINVAL;
    }

    iov_discard_front(&out_iov, &out_num, sizeof(out));

    if (in_iov[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        error_report("virtio-blk request inhdr too short");
        return -EINVAL;
    }

    /* We always touch the last byte, so just see how big in_iov is. */
    in_len = iov_size(in_iov, in_num);
    in = (void *)in_iov[in_num - 1].iov_base
                 + in_iov[in_num - 1].iov_len
                 - sizeof(struct virtio_blk_inhdr);
    iov_discard_back(in_iov, &in_num, sizeof(struct virtio_blk_inhdr));

    type = le32_to_cpu(out.type);
    switch (type & ~VIRTIO_BLK_T_BARRIER) {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT: {
        QEMUIOVector qiov;
        int64_t offset;
        ssize_t ret = 0;
        bool is_write = type & VIRTIO_BLK_T_OUT;
        int64_t sector_num = le64_to_cpu(out.sector);

        if (is_write && !handler->writable) {
            in->status = VIRTIO_BLK_S_IOERR;
            break;
        }

        if (is_write) {
            qemu_iovec_init_external(&qiov, out_iov, out_num);
        } else {
            qemu_iovec_init_external(&qiov, in_iov, in_num);
        }

        if (unlikely(!virtio_blk_sect_range_ok(blk,
                                               handler->logical_block_size,
                                               sector_num, qiov.size))) {
            in->status = VIRTIO_BLK_S_IOERR;
            break;
        }

        offset = sector_num << VIRTIO_BLK_SECTOR_BITS;

        if (is_write) {
            ret = blk_co_pwritev(blk, offset, qiov.size, &qiov, 0);
        } else {
            ret = blk_co_preadv(blk, offset, qiov.size, &qiov, 0);
        }
        if (ret >= 0) {
            in->status = VIRTIO_BLK_S_OK;
        } else {
            in->status = VIRTIO_BLK_S_IOERR;
        }
        break;
    }
    case VIRTIO_BLK_T_FLUSH:
        if (blk_co_flush(blk) == 0) {
            in->status = VIRTIO_BLK_S_OK;
        } else {
            in->status = VIRTIO_BLK_S_IOERR;
        }
        break;
    case VIRTIO_BLK_T_GET_ID: {
        size_t size = MIN(strlen(handler->serial) + 1,
                          MIN(iov_size(in_iov, in_num),
                              VIRTIO_BLK_ID_BYTES));
        iov_from_buf(in_iov, in_num, 0, handler->serial, size);
        in->status = VIRTIO_BLK_S_OK;
        break;
    }
    case VIRTIO_BLK_T_DISCARD:
    case VIRTIO_BLK_T_WRITE_ZEROES:
        if (!handler->writable) {
            in->status = VIRTIO_BLK_S_IOERR;
            break;
        }
        in->status = virtio_blk_discard_write_zeroes(handler, out_iov,
                                                     out_num, type);
        break;
    default:
        in->status = VIRTIO_BLK_S_UNSUPP;
        break;
    }

    return in_len;
}
