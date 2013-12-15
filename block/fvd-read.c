/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this module implements bdrv_aio_readv() for FVD.
 *============================================================================*/

static void finish_read_backing_for_copy_on_read (void *opaque, int ret);
static void finish_read_fvd (void *opaque, int ret);
static inline void calc_read_region (BDRVFvdState * s, int64_t sector_num,
                                     int nb_sectors,
                                     int64_t * p_first_sec_in_fvd,
                                     int64_t * p_last_sec_in_fvd,
                                     int64_t * p_first_sec_in_backing,
                                     int64_t * p_last_sec_in_backing);

static BlockDriverAIOCB *fvd_aio_readv (BlockDriverState * bs,
                                        int64_t sector_num, QEMUIOVector * qiov,
                                        int nb_sectors,
                                        BlockDriverCompletionFunc * cb,
                                        void *opaque)
{
    BDRVFvdState *s = bs->opaque;

    TRACE_REQUEST (FALSE, sector_num, nb_sectors);

    if (!s->data_region_prepared) {
        init_data_region (s);
    }

    if (s->prefetch_state == PREFETCH_STATE_FINISHED
        || sector_num >= s->nb_sectors_in_base_img) {
        /* This is an  efficient case. See Section 3.3.5 of the FVD-cow paper.
         * This also covers the case of no base image. */
        return load_data (NULL, bs, sector_num, qiov, nb_sectors, cb, opaque);
    }

    /* Figure out data regions in the base image and in the FVD data file. */
    int64_t last_sec_in_backing, first_sec_in_backing;
    int64_t last_sec_in_fvd, first_sec_in_fvd;
    calc_read_region (s, sector_num, nb_sectors, &first_sec_in_fvd,
                      &last_sec_in_fvd, &first_sec_in_backing,
                      &last_sec_in_backing);

    if (first_sec_in_backing < 0) {
        /* A simple case: all requested data are in the FVD data file. */
        return load_data (NULL, bs, sector_num, qiov, nb_sectors, cb, opaque);
    }

    /* Do copy-on-read only if the context id is 0, i.e., it is not emulating
     * synchronous I/O.  Doing copy-on-read in emulated synchronous I/O may
     * leave the copy-on-read callbacks never being processed due to
     * mismatching contextid. */
    const int copy_on_read = s->copy_on_read;

    if (first_sec_in_fvd < 0 && !copy_on_read) {
        /* A simple case: all requested data are in the base image and no need
         * to do copy_on_read. */
        return bdrv_aio_readv (bs->backing_hd, sector_num, qiov, nb_sectors, cb,
                               opaque);
    }

    /* The remaining cases are more complicated, which can be: 1. Data are
     * only in the base image and copy-on-read is needed.  2. Data are in both
     * the base image and the FVD data file. Copy-on-read may be either TRUE
     * or FALSE. */
    FvdAIOCB *acb = my_qemu_aio_get (&fvd_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }

    QDEBUG ("READ: acb%llu-%p  start  sector_num=%" PRId64 " nb_sectors=%d\n",
            acb->uuid, acb, sector_num, nb_sectors);

    acb->type = OP_READ;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->read.qiov = qiov;
    acb->read.ret = 0;
    acb->read.read_backing.hd_acb = NULL;
    acb->read.read_backing.done = FALSE;
    acb->read.read_backing.iov.iov_base = NULL;
    acb->read.read_fvd.hd_acb = NULL;
    acb->read.read_fvd.iov.iov_base = NULL;
    acb->read.read_fvd.done = (first_sec_in_fvd < 0);

    /* Read from the base image. */
    if (copy_on_read) {
        /* Round the request to the block boundary. */
        acb->read.read_backing.sector_num =
            ROUND_DOWN (first_sec_in_backing, s->block_size);
        int64_t end = ROUND_UP (last_sec_in_backing + 1, s->block_size);
        if (end > s->nb_sectors_in_base_img) {
            end = s->nb_sectors_in_base_img;
        }
        acb->read.read_backing.nb_sectors =
            end - acb->read.read_backing.sector_num;
    } else {
        acb->read.read_backing.sector_num = first_sec_in_backing;
        acb->read.read_backing.nb_sectors =
            last_sec_in_backing - first_sec_in_backing + 1;
    }

    acb->read.read_backing.iov.iov_len =
        acb->read.read_backing.nb_sectors * 512;
    acb->read.read_backing.iov.iov_base =
        my_qemu_blockalign (bs->backing_hd, acb->read.read_backing.iov.iov_len);
    qemu_iovec_init_external (&acb->read.read_backing.qiov,
                              &acb->read.read_backing.iov, 1);
    acb->read.read_backing.hd_acb =
        bdrv_aio_readv (bs->backing_hd, acb->read.read_backing.sector_num,
                        &acb->read.read_backing.qiov,
                        acb->read.read_backing.nb_sectors,
                        finish_read_backing_for_copy_on_read, acb);
    QDEBUG ("READ: acb%llu-%p  read_backing  backing_sector_num=%" PRId64
            " backing_nb_sectors=%d\n", acb->uuid, acb,
            acb->read.read_backing.sector_num,
            acb->read.read_backing.nb_sectors);

    if (!acb->read.read_backing.hd_acb) {
        my_qemu_vfree (acb->read.read_backing.iov.iov_base);
        my_qemu_aio_release (acb);
        return NULL;
    }

    if (first_sec_in_fvd >= 0) {
        /* Read the FVD data file. */
        acb->read.read_fvd.sector_num = first_sec_in_fvd;
        acb->read.read_fvd.nb_sectors = last_sec_in_fvd - first_sec_in_fvd + 1;
        acb->read.read_fvd.iov.iov_len = acb->read.read_fvd.nb_sectors * 512;

        /* Make a copy of the current bitmap because it may change when the
         * read requests finish. */
        int64_t b = MIN (acb->read.read_backing.sector_num,
                         acb->read.read_fvd.sector_num);
        b = b / s->block_size / 8;        /* First byte of the bitmap we need. */
        int64_t e1 = acb->read.read_backing.sector_num +
                            acb->read.read_backing.nb_sectors;
        int64_t e2 = acb->read.read_fvd.sector_num +
                            acb->read.read_fvd.nb_sectors;
        int64_t e = MAX (e1, e2);
        if (e > s->nb_sectors_in_base_img) {
            e = s->nb_sectors_in_base_img;
        }
        e = (e - 1) / s->block_size / 8;/* Last byte of the bitmap we need. */
        int bitmap_bytes = e - b + 1;
        int buf_size = acb->read.read_fvd.iov.iov_len +
                                    ROUND_UP (bitmap_bytes, 512);
        acb->read.read_fvd.iov.iov_base =
            my_qemu_blockalign (s->fvd_data, buf_size);
        uint8_t *saved_bitmap = ((uint8_t *) acb->read.read_fvd.iov.iov_base) +
                                    acb->read.read_fvd.iov.iov_len;
        memcpy (saved_bitmap, s->fresh_bitmap + b, bitmap_bytes);

        qemu_iovec_init_external (&acb->read.read_fvd.qiov,
                                  &acb->read.read_fvd.iov, 1);
        QDEBUG ("READ: acb%llu-%p  read_fvd  fvd_sector_num=%" PRId64
                " fvd_nb_sectors=%d\n", acb->uuid, acb,
                acb->read.read_fvd.sector_num, acb->read.read_fvd.nb_sectors);
        acb->read.read_fvd.hd_acb = load_data (acb, bs, first_sec_in_fvd,
                                               &acb->read.read_fvd.qiov,
                                               acb->read.read_fvd.nb_sectors,
                                               finish_read_fvd, acb);
        if (!acb->read.read_fvd.hd_acb) {
            if (acb->read.read_backing.hd_acb) {
                bdrv_aio_cancel (acb->read.read_backing.hd_acb);
                my_qemu_vfree (acb->read.read_backing.iov.iov_base);
            }
            my_qemu_vfree (acb->read.read_fvd.iov.iov_base);
            my_qemu_aio_release (acb);
            return NULL;
        }
    }

    return &acb->common;
}

static void finish_copy_on_read (void *opaque, int ret)
{
    FvdAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    if (ret == 0) {
        /* Update fresh_bitmap but do not update stale_bitmap or the on-disk
         * bitmap. See Section 3.3.4 of the FVD-cow paper. */
        update_fresh_bitmap (acb->sector_num, acb->nb_sectors, s);
    }

    s->outstanding_copy_on_read_data -= acb->nb_sectors * 512;

#ifdef FVD_DEBUG
    s->total_copy_on_read_data += acb->nb_sectors * 512;
#endif
    QDEBUG ("READ: acb%llu-%p  finish_copy_on_read  buffer_sector_num=%" PRId64
            " buffer_nb_sectors=%d write_sector_num=%" PRId64
            " write_nb_sectors=%d outstanding_copy_on_read=%" PRId64 "\n",
            acb->uuid, acb, acb->copy.buffered_sector_begin,
            (int) (acb->copy.buffered_sector_end -
                   acb->copy.buffered_sector_begin), acb->sector_num,
            acb->nb_sectors, s->outstanding_copy_on_read_data);

    QLIST_REMOVE (acb, copy_lock.next);
    restart_dependent_writes (acb);

    int64_t begin = acb->sector_num + acb->nb_sectors;
    int64_t end = acb->copy.buffered_sector_end;

    if (find_region_in_base_img (s, &begin, &end)) {
        acb->sector_num = begin;
        acb->nb_sectors = end - begin;
        acb->copy.iov.iov_base = acb->copy.buf +
                                (begin - acb->copy.buffered_sector_begin) * 512;
        acb->copy.iov.iov_len = acb->nb_sectors * 512;
        qemu_iovec_init_external (&acb->copy.qiov, &acb->copy.iov, 1);
        QDEBUG ("READ: acb%llu-%p  copy_on_read  buffer_sector_num=%" PRId64
                " buffer_nb_sectors=%d write_sector_num=%" PRId64
                " write_nb_sectors=%d outstanding_copy_on_read=%" PRId64 "\n",
                acb->uuid, acb, acb->copy.buffered_sector_begin,
                (int) (acb->copy.buffered_sector_end -
                       acb->copy.buffered_sector_begin), acb->sector_num,
                acb->nb_sectors, s->outstanding_copy_on_read_data);
        acb->copy.hd_acb = store_data (TRUE, acb, bs, acb->sector_num,
                                       &acb->copy.qiov, acb->nb_sectors,
                                       finish_copy_on_read, acb);
        if (acb->copy.hd_acb) {
            QLIST_INIT (&acb->copy_lock.dependent_writes);
            acb->copy_lock.begin = begin;
            acb->copy_lock.end = end;
            QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            s->outstanding_copy_on_read_data += acb->copy.iov.iov_len;
            return;
        }
    }

    QDEBUG ("READ: acb%llu-%p  no_more_copy_on_read\n", acb->uuid, acb);
    my_qemu_vfree (acb->copy.buf);
    my_qemu_aio_release (acb);
}

static void finish_read (FvdAIOCB * acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    if (acb->read.ret != 0) {
        QDEBUG ("READ: acb%llu-%p  finish_read error ret=%d sector_num=%" PRId64
                " nb_sectors=%d\n", acb->uuid, acb, acb->read.ret,
                acb->sector_num, acb->nb_sectors);
        acb->common.cb (acb->common.opaque, acb->read.ret);
        if (acb->read.read_backing.iov.iov_base) {
            my_qemu_vfree (acb->read.read_backing.iov.iov_base);
        }
        if (acb->read.read_fvd.iov.iov_base) {
            my_qemu_vfree (acb->read.read_fvd.iov.iov_base);
        }
        my_qemu_aio_release (acb);

        return;
    }

    if (!acb->read.read_fvd.iov.iov_base) {
        /* Only read data from the base image. */
        uint8_t *data = ((uint8_t *) acb->read.read_backing.iov.iov_base) +
                    (acb->sector_num - acb->read.read_backing.sector_num) * 512;
        qemu_iovec_from_buf(acb->read.qiov, 0, data, acb->nb_sectors * 512);
    } else {
        /* Under the guidance of the saved bitmap, merge data from the FVD
         * data file and the base image. */
        uint8_t *saved_bitmap = ((uint8_t *) acb->read.read_fvd.iov.iov_base) +
                                            acb->read.read_fvd.iov.iov_len;
        int64_t bitmap_offset = MIN (acb->read.read_backing.sector_num,
                                     acb->read.read_fvd.sector_num);
        bitmap_offset = bitmap_offset / s->block_size / 8;
        int iov_index = 0;
        uint8_t *iov_buf = acb->read.qiov->iov[0].iov_base;
        int iov_left = acb->read.qiov->iov[0].iov_len;
        int64_t sec = acb->sector_num;
        const int64_t end = acb->sector_num + acb->nb_sectors;
        int64_t first_sec;
        uint8_t *source;

        if (bitmap_show_sector_in_base_img
            (sec, s, bitmap_offset, saved_bitmap)) {
            goto in_backing;
        }

        while (1) {
            /* For a section of data in the FVD data file. */
            if (sec >= end) {
                break;
            }

            first_sec = sec;
            do {
                sec++;
            } while (sec < end && !bitmap_show_sector_in_base_img (sec, s,
                                        bitmap_offset, saved_bitmap));

            source = ((uint8_t *) acb->read.read_fvd.iov.iov_base) +
                            (first_sec - acb->read.read_fvd.sector_num) * 512;
            copy_to_iov (acb->read.qiov->iov, &iov_index, &iov_buf, &iov_left,
                         source, (sec - first_sec) * 512);

          in_backing:
            /* For a section of data in the base image. */
            if (sec >= end) {
                break;
            }

            first_sec = sec;
            do {
                sec++;
            } while (sec < end && bitmap_show_sector_in_base_img (sec, s,
                                                bitmap_offset, saved_bitmap));

            source = ((uint8_t *) acb->read.read_backing.iov.iov_base) +
                        (first_sec - acb->read.read_backing.sector_num) * 512;
            copy_to_iov (acb->read.qiov->iov, &iov_index, &iov_buf, &iov_left,
                         source, (sec - first_sec) * 512);
        }

        ASSERT (iov_index == acb->read.qiov->niov - 1 && iov_left == 0);
        my_qemu_vfree (acb->read.read_fvd.iov.iov_base);
    }

    QDEBUG ("READ: acb%llu-%p  finish_read  ret=%d\n", acb->uuid, acb,
            acb->read.ret);
    acb->common.cb (acb->common.opaque, acb->read.ret);

    if (!s->copy_on_read) {
        /* Do copy-on-read only if the context id is 0, i.e., it is not
         * emulating synchronous I/O.  Doing copy-on-read in emulated
         * synchronous I/O may leave the copy-on-read callbacks never being
         * processed due to mismatching context id. */
        my_qemu_vfree (acb->read.read_backing.iov.iov_base);
        my_qemu_aio_release (acb);
        return;
    }

    /* Convert AIOReadCB into a AIOCopyCB for copy-on-read. */
    uint8_t *buf = acb->read.read_backing.iov.iov_base;
    int64_t begin = acb->read.read_backing.sector_num;
    int64_t end = begin + acb->read.read_backing.nb_sectors;

    acb->type = OP_COPY;
    acb->copy.buf = buf;
    acb->copy.buffered_sector_begin = begin;
    acb->copy.buffered_sector_end = end;

    if (s->outstanding_copy_on_read_data < s->max_outstanding_copy_on_read_data
        && find_region_in_base_img (s, &begin, &end)) {
        /* Write to the FVD data file. */
        acb->sector_num = begin;
        acb->nb_sectors = end - begin;
        acb->copy.iov.iov_base =
            buf + (begin - acb->copy.buffered_sector_begin) * 512;
        acb->copy.iov.iov_len = acb->nb_sectors * 512;
        qemu_iovec_init_external (&acb->copy.qiov, &acb->copy.iov, 1);
        QDEBUG ("READ: acb%llu-%p  copy_on_read  buffer_sector_num=%" PRId64
                " buffer_nb_sectors=%d write_sector_num=%" PRId64
                " write_nb_sectors=%d outstanding_copy_on_read=%" PRId64 "\n",
                acb->uuid, acb, acb->copy.buffered_sector_begin,
                (int) (acb->copy.buffered_sector_end -
                       acb->copy.buffered_sector_begin), acb->sector_num,
                acb->nb_sectors, s->outstanding_copy_on_read_data);
        acb->copy.hd_acb = store_data (TRUE, acb, bs, acb->sector_num,
                                       &acb->copy.qiov, acb->nb_sectors,
                                       finish_copy_on_read, acb);
        if (acb->copy.hd_acb) {
            QLIST_INIT (&acb->copy_lock.dependent_writes);
            acb->copy_lock.begin = begin;
            acb->copy_lock.end = end;
            QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            s->outstanding_copy_on_read_data += acb->copy.iov.iov_len;
            return;
        }
    }

    /* No more copy-on-read to do. */
    my_qemu_vfree (acb->copy.buf);
    my_qemu_aio_release (acb);
}

static void finish_read_fvd (void *opaque, int ret)
{
    FvdAIOCB *acb = opaque;

    QDEBUG ("READ: acb%llu-%p  finish_read_fvd ret=%d\n", acb->uuid, acb, ret);
    acb->read.read_fvd.hd_acb = NULL;
    acb->read.read_fvd.done = TRUE;
    if (acb->read.ret == 0) {
        acb->read.ret = ret;
    }

    if (acb->read.read_backing.done) {
        finish_read (acb);        /* The other request also finished. */
    }
}

static void finish_read_backing_for_copy_on_read (void *opaque, int ret)
{
    FvdAIOCB *acb = opaque;

    QDEBUG ("READ: acb%llu-%p  finish_read_backing ret=%d\n", acb->uuid, acb,
            ret);
    acb->read.read_backing.hd_acb = NULL;
    acb->read.read_backing.done = TRUE;
    if (acb->read.ret == 0) {
        acb->read.ret = ret;
    }

    if (acb->read.read_fvd.done) {
        finish_read (acb);        /* The other request also finished. */
    }
}

static inline void calc_read_region (BDRVFvdState * s, int64_t sector_num,
                                     int nb_sectors,
                                     int64_t * p_first_sec_in_fvd,
                                     int64_t * p_last_sec_in_fvd,
                                     int64_t * p_first_sec_in_backing,
                                     int64_t * p_last_sec_in_backing)
{
    int64_t last_sec_in_backing = -1, first_sec_in_backing = -1;
    int64_t last_sec_in_fvd = -1, first_sec_in_fvd = -1;
    int prev_block_in_backing;

    if (fresh_bitmap_show_sector_in_base_img (sector_num, s)) {
        first_sec_in_backing = last_sec_in_backing = sector_num;
        prev_block_in_backing = TRUE;
    } else {
        first_sec_in_fvd = last_sec_in_fvd = sector_num;
        prev_block_in_backing = FALSE;
    }

    /* Begin of next block. */
    int64_t sec = ROUND_UP (sector_num + 1, s->block_size);

    const int64_t sec_end = sector_num + nb_sectors;
    int64_t last_sec = MIN (sec_end, s->nb_sectors_in_base_img) - 1;

    while (1) {
        if (sec > last_sec) {
            sec = last_sec;
        }

        if (fresh_bitmap_show_sector_in_base_img (sec, s)) {
            if (first_sec_in_backing < 0) {
                first_sec_in_backing = sec;
            }
            if (!prev_block_in_backing) {
                last_sec_in_fvd = sec - 1;
                prev_block_in_backing = TRUE;
            }
            last_sec_in_backing = sec;
        } else {
            if (first_sec_in_fvd < 0) {
                first_sec_in_fvd = sec;
            }
            if (prev_block_in_backing) {
                last_sec_in_backing = sec - 1;
                prev_block_in_backing = FALSE;
            }
            last_sec_in_fvd = sec;
        }

        if (sec == last_sec) {
            break;
        }
        sec += s->block_size;
    }

    if (sec_end > s->nb_sectors_in_base_img) {
        if (first_sec_in_fvd < 0) {
            first_sec_in_fvd = s->nb_sectors_in_base_img;
        }
        last_sec_in_fvd = sec_end - 1;
    }

    *p_first_sec_in_fvd = first_sec_in_fvd;
    *p_last_sec_in_fvd = last_sec_in_fvd;
    *p_first_sec_in_backing = first_sec_in_backing;
    *p_last_sec_in_backing = last_sec_in_backing;
}

static void fvd_read_cancel (FvdAIOCB * acb)
{
    if (acb->read.read_backing.hd_acb) {
        bdrv_aio_cancel (acb->read.read_backing.hd_acb);
    }
    if (acb->read.read_fvd.hd_acb) {
        bdrv_aio_cancel (acb->read.read_fvd.hd_acb);
    }
    if (acb->read.read_backing.iov.iov_base) {
        my_qemu_vfree (acb->read.read_backing.iov.iov_base);
    }
    if (acb->read.read_fvd.iov.iov_base) {
        my_qemu_vfree (acb->read.read_fvd.iov.iov_base);
    }
    my_qemu_aio_release (acb);
}

static void fvd_copy_cancel (FvdAIOCB * acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    if (acb->copy.hd_acb) {
        bdrv_aio_cancel (acb->copy.hd_acb);
    }
    if (acb->copy_lock.next.le_prev != NULL) {
        QLIST_REMOVE (acb, copy_lock.next);
        restart_dependent_writes (acb);
    }
    my_qemu_vfree (acb->copy.buf);
    if (acb->common.cb != null_prefetch_cb) {
        /* This is a copy-on-read operation. */
        s->outstanding_copy_on_read_data -= acb->nb_sectors * 512;
    }
    my_qemu_aio_release (acb);
}

static void restart_dependent_writes (FvdAIOCB * acb)
{
    acb->copy_lock.next.le_prev = NULL;
    FvdAIOCB *req = acb->copy_lock.dependent_writes.lh_first;

    while (req) {
        /* Keep a copy of 'next' as it may be changed in do_aiO_write(). */
        FvdAIOCB *next = req->write.next_dependent_write.le_next;

        /* Indicate that this write is no longer on any depedent list. This
         * helps fvd_read_cancel() work properly. */
        req->write.next_dependent_write.le_prev = NULL;

        if (acb->type == OP_WRITE) {
            QDEBUG ("WRITE: acb%llu-%p  finished_and_restart_conflict_write "
                    "acb%llu-%p\n", acb->uuid, acb, req->uuid, req);
        } else {
            QDEBUG ("READ: copy_on_read acb%llu-%p  "
                    "finished_and_restart_conflict_write acb%llu-%p\n",
                    acb->uuid, acb, req->uuid, req);
        }

        if (do_aio_write (req) < 0) {
            QDEBUG ("WRITE: acb%llu-%p  finished with error ret=%d\n",
                    req->uuid, req, -1);
            req->common.cb (req->common.opaque, -1);
            my_qemu_aio_release (req);
        }

        req = next;
    }
}
