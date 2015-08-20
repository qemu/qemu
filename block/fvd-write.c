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
 *  A short description: this module implements bdrv_aio_writev() for FVD.
 *===========================================================================*/

static BlockDriverAIOCB *fvd_aio_writev (BlockDriverState * bs,
                                         int64_t sector_num,
                                         QEMUIOVector * qiov, int nb_sectors,
                                         BlockDriverCompletionFunc * cb,
                                         void *opaque)
{
    BDRVFvdState *s = bs->opaque;
    FvdAIOCB *acb;

    TRACE_REQUEST (TRUE, sector_num, nb_sectors);

    if (!s->data_region_prepared) {
        init_data_region (s);
    }

    if (s->prefetch_state == PREFETCH_STATE_FINISHED
        || sector_num >= s->nb_sectors_in_base_img) {
        /* This is an  efficient case. See Section 3.3.5 of the FVD-cow paper.
         * This also covers the case of no base image. */
        return store_data (FALSE, NULL, bs, sector_num, qiov,
                           nb_sectors, cb, opaque);
    }

    /* Check if all requested sectors are in the FVD data file. */
    int64_t sec = ROUND_DOWN (sector_num, s->block_size);
    int64_t sec_in_last_block = ROUND_DOWN (sector_num + nb_sectors - 1,
                                            s->block_size);
    do {
        if (stale_bitmap_show_sector_in_base_img (sec, s)) {
            goto slow_path;
        }
        sec += s->block_size;
    } while (sec <= sec_in_last_block);

    /* This is the fast path, as all requested data are in the FVD data file
     * and no need to update the bitmap. */
    return store_data (FALSE, NULL, bs, sector_num, qiov,
                       nb_sectors, cb, opaque);

  slow_path:
    acb = my_qemu_aio_get (&fvd_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }

    acb->type = OP_WRITE;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->write.ret = 0;
    acb->write.update_table = FALSE;
    acb->write.qiov = qiov;
    acb->write.hd_acb = NULL;
    acb->write.cow_buf = NULL;
    acb->copy_lock.next.le_prev = NULL;
    acb->write.next_write_lock.le_prev = NULL;
    acb->write.next_dependent_write.le_prev = NULL;
    acb->jcb.iov.iov_base = NULL;
    acb->jcb.hd_acb = NULL;
    acb->jcb.next_wait_for_journal.le_prev = NULL;
    QLIST_INIT (&acb->copy_lock.dependent_writes);

    QDEBUG ("WRITE: acb%llu-%p  start  sector_num=%" PRId64 " nb_sectors=%d\n",
            acb->uuid, acb, acb->sector_num, acb->nb_sectors);

    if (do_aio_write (acb) < 0) {
        my_qemu_aio_release (acb);
        return NULL;
    }
#ifdef FVD_DEBUG
    pending_local_writes++;
#endif
    return &acb->common;
}

static void fvd_write_cancel (FvdAIOCB * acb)
{
    if (acb->write.hd_acb) {
        bdrv_aio_cancel (acb->write.hd_acb);
    }
    if (acb->jcb.hd_acb) {
        bdrv_aio_cancel (acb->jcb.hd_acb);
        free_journal_sectors (acb->common.bs->opaque);
    }
    if (acb->jcb.next_wait_for_journal.le_prev) {
        QLIST_REMOVE (acb, jcb.next_wait_for_journal);
    }
    if (acb->write.next_dependent_write.le_prev) {
        QLIST_REMOVE (acb, write.next_dependent_write);
    }
    free_write_resource (acb);
}

static void free_write_resource (FvdAIOCB * acb)
{
    if (acb->write.next_write_lock.le_prev) {
        QLIST_REMOVE (acb, write.next_write_lock);
    }
    if (acb->copy_lock.next.le_prev) {
        QLIST_REMOVE (acb, copy_lock.next);
        restart_dependent_writes (acb);
    }
    if (acb->write.cow_buf) {
        my_qemu_vfree (acb->write.cow_buf);
    }
    if (acb->jcb.iov.iov_base != NULL) {
        my_qemu_vfree (acb->jcb.iov.iov_base);
    }

    my_qemu_aio_release (acb);

#ifdef FVD_DEBUG
    pending_local_writes--;
#endif
}

static inline void finish_write (FvdAIOCB * acb, int ret)
{
    QDEBUG ("WRITE: acb%llu-%p  completely_finished ret=%d\n", acb->uuid, acb,
            ret);
    acb->common.cb (acb->common.opaque, ret);
    free_write_resource (acb);
}

static void finish_write_data (void *opaque, int ret)
{
    FvdAIOCB *acb = opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    acb->write.ret = ret;
    acb->write.hd_acb = NULL;

    if (ret != 0) {
        QDEBUG ("WRITE: acb%llu-%p  finish_write_data error ret=%d\n",
                acb->uuid, acb, ret);
        finish_write (acb, ret);
        return;
    }

    QDEBUG ("WRITE: acb%llu-%p  finish_write_data\n", acb->uuid, acb);

    /* Figure out whether to update metadata or not. */
    if (s->fresh_bitmap == s->stale_bitmap) {
        /* This is the case if neither copy_on_read nor prefetching is
         * enabled. Cannot update fresh_bitmap until the on-disk metadata is
         * updated. */
        if (acb->write.update_table || stale_bitmap_need_update (acb)) {
            /* Cannot release lock on data now since fresh_bitmap has not been
             * updated. Otherwise, a copy-on-write or copy-on-read operation
             * may use data from the backing image to overwrite the data just
             * been written. */
            write_metadata_to_journal (acb);
        } else {
            finish_write (acb, ret);        /* No need to update metadata. */
        }
        return;
    }

    /* stale_bitmap and fresh_bitmap are different. Now we can update
     * fresh_bitmap. stale_bitmap will be updated after the on-disk metadata
     * are updated. */
    int update_stale_bitmap = update_fresh_bitmap_and_check_stale_bitmap (acb);

    if (acb->write.update_table || update_stale_bitmap) {
        /* Release lock on data now since fresh_bitmap has been updated. */
        QLIST_REMOVE (acb, write.next_write_lock);
        acb->write.next_write_lock.le_prev = NULL;
        if (acb->copy_lock.next.le_prev) {
            QLIST_REMOVE (acb, copy_lock.next);
            restart_dependent_writes (acb);
        }

        write_metadata_to_journal (acb);
    } else {
        finish_write (acb, ret);
    }
}

static void finish_read_backing_for_copy_on_write (void *opaque, int ret)
{
    FvdAIOCB *acb = (FvdAIOCB *) opaque;
    BlockDriverState *bs = acb->common.bs;

    if (ret != 0) {
        QDEBUG ("WRITE: acb%llu-%p  finish_read_from_backing with error "
                "ret=%d\n", acb->uuid, acb, ret);
        finish_write (acb, ret);
    } else {
        QDEBUG ("WRITE: acb%llu-%p  "
                "finish_read_from_backing_and_start_write_data\n",
                acb->uuid, acb);
        acb->write.hd_acb = store_data (FALSE, acb, bs,
                                        acb->write.cow_start_sector,
                                        acb->write.cow_qiov,
                                        acb->write.cow_qiov->size / 512,
                                        finish_write_data, acb);
        if (!acb->write.hd_acb) {
            finish_write (acb, -1);
        }
    }
}

static int do_aio_write (FvdAIOCB * acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    /* Calculate the data region need be locked. */
    const int64_t sector_end = acb->sector_num + acb->nb_sectors;
    const int64_t block_begin = ROUND_DOWN (acb->sector_num, s->block_size);
    int64_t block_end = ROUND_UP (sector_end, s->block_size);

    /* Check for conflicting copy-on-reads. */
    FvdAIOCB *old;
    QLIST_FOREACH (old, &s->copy_locks, copy_lock.next) {
        if (old->copy_lock.end > acb->sector_num &&
            sector_end > old->copy_lock.begin) {
            QLIST_INSERT_HEAD (&old->copy_lock.dependent_writes, acb,
                               write.next_dependent_write);
            QDEBUG ("WRITE: acb%llu-%p  put_on_hold_due_to_data_conflict "
                    "with %s acb%llu-%p\n", acb->uuid, acb,
                    old->type == OP_WRITE ? "write" : "copy_on_read",
                    old->uuid, old);
            return 0;
        }
    }

    /* No conflict. Now check if this write updates partial blocks and hence
     * need to read those blocks from the base image and merge with this
     * write. */
    int read_first_block, read_last_block;
    if (acb->sector_num % s->block_size == 0) {
        read_first_block = FALSE;
    } else
        if (fresh_bitmap_show_sector_in_base_img (acb->sector_num, s)) {
        read_first_block = TRUE;
    } else {
        read_first_block = FALSE;
    }

    if (sector_end % s->block_size == 0) {
        read_last_block = FALSE;
    } else if (fresh_bitmap_show_sector_in_base_img (sector_end - 1, s)) {
        read_last_block = TRUE;
    } else {
        read_last_block = FALSE;
    }

    if (read_first_block) {
        if (read_last_block) {
            /* Case 1: Read all the blocks involved from the base image. */
            const QEMUIOVector *old_qiov = acb->write.qiov;
            if (block_end > s->nb_sectors_in_base_img) {
                block_end = s->nb_sectors_in_base_img;
            }

            int buf_size = (block_end - block_begin) * 512
                    + 2 * sizeof (QEMUIOVector)
                    + sizeof (struct iovec) * (old_qiov->niov + 3);
            buf_size = ROUND_UP (buf_size, 512);
            acb->write.cow_buf = my_qemu_blockalign (bs->backing_hd, buf_size);

            /* For reading from the base image. */
            QEMUIOVector *read_qiov = (QEMUIOVector *) (acb->write.cow_buf +
                                  (block_end - block_begin) * 512);
            read_qiov->iov = (struct iovec *) (read_qiov + 1);
            read_qiov->nalloc = -1;
            read_qiov->niov = 1;
            read_qiov->iov[0].iov_base = acb->write.cow_buf;
            read_qiov->iov[0].iov_len = read_qiov->size =
                (block_end - block_begin) * 512;

            /* For writing to the FVD data file. */
            QEMUIOVector *write_qiov = (QEMUIOVector *) (read_qiov->iov + 1);
            write_qiov->iov = (struct iovec *) (write_qiov + 1);
            write_qiov->nalloc = -1;
            write_qiov->niov = old_qiov->niov + 2;
            write_qiov->size = read_qiov->size;

            /* The first entry is for data read from the base image. */
            write_qiov->iov[0].iov_base = acb->write.cow_buf;
            write_qiov->iov[0].iov_len = (acb->sector_num - block_begin) * 512;
            memcpy (&write_qiov->iov[1], old_qiov->iov,
                    sizeof (struct iovec) * old_qiov->niov);

            /* The last entry is for data read from the base image. */
            write_qiov->iov[old_qiov->niov + 1].iov_base = acb->write.cow_buf
                                            + (sector_end - block_begin) * 512;
            write_qiov->iov[old_qiov->niov + 1].iov_len =
                                                (block_end - sector_end) * 512;
            acb->write.cow_qiov = write_qiov;
            acb->write.cow_start_sector = block_begin;

            acb->write.hd_acb = bdrv_aio_readv (bs->backing_hd, block_begin,
                                    read_qiov, block_end - block_begin,
                                    finish_read_backing_for_copy_on_write, acb);
            if (!acb->write.hd_acb) {
                goto fail;
            }

            acb->copy_lock.begin = block_begin;
            acb->copy_lock.end = block_end;
            QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            QDEBUG ("WRITE: acb%llu-%p  "
                    "read_first_last_partial_blocks_from_backing  sector_num=%"
                    PRId64 " nb_sectors=%d\n", acb->uuid, acb, block_begin,
                    (int) (block_end - block_begin));
        } else {
            /* Case 2: Read the first block from the base image. */
            int nb = acb->sector_num - block_begin;
            const QEMUIOVector *old_qiov = acb->write.qiov;

            /* Space for data and metadata. */
            int buf_size = nb * 512 + 2 * sizeof (QEMUIOVector)
                                + sizeof (struct iovec) * (old_qiov->niov + 2);
            buf_size = ROUND_UP (buf_size, 512);
            acb->write.cow_buf = my_qemu_blockalign (bs->backing_hd, buf_size);

            /* For reading from the base image. */
            QEMUIOVector *read_qiov =
                (QEMUIOVector *) (acb->write.cow_buf + nb * 512);
            read_qiov->iov = (struct iovec *) (read_qiov + 1);
            read_qiov->nalloc = -1;
            read_qiov->niov = 1;
            read_qiov->iov[0].iov_base = acb->write.cow_buf;
            read_qiov->iov[0].iov_len = read_qiov->size = nb * 512;

            /* For writing to the FVD data file. */
            QEMUIOVector *write_qiov = (QEMUIOVector *) (read_qiov->iov + 1);
            write_qiov->iov = (struct iovec *) (write_qiov + 1);
            write_qiov->nalloc = -1;
            write_qiov->niov = old_qiov->niov + 1;
            write_qiov->size = old_qiov->size + read_qiov->size;

            /* The first entry is added for data read from the base image. */
            write_qiov->iov[0].iov_base = acb->write.cow_buf;
            write_qiov->iov[0].iov_len = read_qiov->size;
            memcpy (&write_qiov->iov[1], old_qiov->iov,
                    sizeof (struct iovec) * old_qiov->niov);
            acb->write.cow_qiov = write_qiov;
            acb->write.cow_start_sector = block_begin;

            acb->write.hd_acb = bdrv_aio_readv (bs->backing_hd,
                                    block_begin, read_qiov, nb,
                                    finish_read_backing_for_copy_on_write, acb);
            if (!acb->write.hd_acb) {
                goto fail;
            }

            acb->copy_lock.begin = block_begin;
            acb->copy_lock.end = block_begin + s->block_size;
            QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            QDEBUG ("WRITE: acb%llu-%p  read_first_partial_block_from_backing  "
                    "sector_num=%" PRId64 " nb_sectors=%d\n",
                    acb->uuid, acb, block_begin, nb);
        }
    } else {
        if (read_last_block) {
            /* Case 3: Read the last block from the base image. */
            int nb;
            if (block_end < s->nb_sectors_in_base_img) {
                nb = block_end - sector_end;
            }
            else {
                nb = s->nb_sectors_in_base_img - sector_end;
            }
            const QEMUIOVector *old_qiov = acb->write.qiov;

            /* Space for data and metadata. */
            int buf_size = nb * 512 + 2 * sizeof (QEMUIOVector)
                                + sizeof (struct iovec) * (old_qiov->niov + 2);
            buf_size = ROUND_UP (buf_size, 512);
            acb->write.cow_buf = my_qemu_blockalign (bs->backing_hd, buf_size);

            /* For reading from the base image. */
            QEMUIOVector *read_qiov = (QEMUIOVector *) (acb->write.cow_buf
                                                        + nb * 512);
            read_qiov->iov = (struct iovec *) (read_qiov + 1);
            read_qiov->nalloc = -1;
            read_qiov->niov = 1;
            read_qiov->iov[0].iov_base = acb->write.cow_buf;
            read_qiov->iov[0].iov_len = read_qiov->size = nb * 512;

            /* For writing to the FVD data file. */
            QEMUIOVector *write_qiov = (QEMUIOVector *) (read_qiov->iov + 1);
            write_qiov->iov = (struct iovec *) (write_qiov + 1);
            write_qiov->nalloc = -1;
            write_qiov->niov = old_qiov->niov + 1;
            write_qiov->size = old_qiov->size + read_qiov->size;
            memcpy (write_qiov->iov, old_qiov->iov,
                    sizeof (struct iovec) * old_qiov->niov);

            /* The last appended entry is for data read from the base image. */
            write_qiov->iov[old_qiov->niov].iov_base = acb->write.cow_buf;
            write_qiov->iov[old_qiov->niov].iov_len = read_qiov->size;
            acb->write.cow_qiov = write_qiov;
            acb->write.cow_start_sector = acb->sector_num;

            acb->write.hd_acb = bdrv_aio_readv (bs->backing_hd,
                                    sector_end, read_qiov, nb,
                                    finish_read_backing_for_copy_on_write, acb);
            if (!acb->write.hd_acb) {
                goto fail;
            }

            acb->copy_lock.end = block_end;
            acb->copy_lock.begin = block_end - s->block_size;
            QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            QDEBUG ("WRITE: acb%llu-%p  read_last_partial_block_from_backing  "
                    "sector_num=%" PRId64 " nb_sectors=%d\n",
                    acb->uuid, acb, sector_end, nb);
        } else {
            /* Case 4: Can write directly and no need to merge with data from
             * the base image. */
            QDEBUG ("WRITE: acb%llu-%p  "
                    "write_fvd_without_read_partial_block_from_backing\n",
                    acb->uuid, acb);
            acb->write.hd_acb = store_data (FALSE, acb, bs, acb->sector_num,
                                            acb->write.qiov, acb->nb_sectors,
                                            finish_write_data, acb);
            if (!acb->write.hd_acb) {
                goto fail;
            }
        }
    }

    QLIST_INSERT_HEAD (&s->write_locks, acb, write.next_write_lock);
    return 0;

  fail:
    if (acb->write.cow_buf) {
        my_qemu_vfree (acb->write.cow_buf);
    }
    return -1;
}
