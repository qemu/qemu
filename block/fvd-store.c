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
 *  A short description: this FVD module implements storing data to a
 *  compact image.
 *===========================================================================*/

static uint32_t allocate_chunk (BlockDriverState * bs);
static inline FvdAIOCB *init_store_acb (int soft_write,
                                        QEMUIOVector * orig_qiov,
                                        BlockDriverState * bs,
                                        int64_t sector_num, int nb_sectors,
                                        FvdAIOCB * parent_acb,
                                        BlockDriverCompletionFunc * cb,
                                        void *opaque);
static void finish_store_data_in_compact_image (void *opaque, int ret);

static inline BlockDriverAIOCB *store_data (int soft_write,
                                            FvdAIOCB * parent_acb,
                                            BlockDriverState * bs,
                                            int64_t sector_num,
                                            QEMUIOVector * orig_qiov,
                                            int nb_sectors,
                                            BlockDriverCompletionFunc * cb,
                                            void *opaque)
{
    BDRVFvdState *s = bs->opaque;

    TRACE_STORE_IN_FVD ("store_data", sector_num, nb_sectors);

    if (!s->table) {
        /* Write directly since it is not a compact image. */
        return bdrv_aio_writev (s->fvd_data, s->data_offset + sector_num,
                                orig_qiov, nb_sectors, cb, opaque);
    } else {
        return store_data_in_compact_image (NULL, soft_write, parent_acb, bs,
                                            sector_num, orig_qiov, nb_sectors,
                                            cb, opaque);
    }
}

/* Store data in the compact image. The argument 'soft_write' means
 * the store was caused by copy-on-read or prefetching, which need not
 * update metadata immediately. */
static BlockDriverAIOCB *store_data_in_compact_image (FvdAIOCB * acb,
                                                      int soft_write,
                                                      FvdAIOCB * parent_acb,
                                                      BlockDriverState * bs,
                                                      int64_t sector_num,
                                                      QEMUIOVector * orig_qiov,
                                                      const int nb_sectors,
                                                      BlockDriverCompletionFunc
                                                      * cb, void *opaque)
{
    BDRVFvdState *s = bs->opaque;

    const uint32_t first_chunk = sector_num / s->chunk_size;
    const uint32_t last_chunk = (sector_num + nb_sectors - 1) / s->chunk_size;
    int table_dirty = FALSE;
    uint32_t chunk;
    int64_t start_sec;

    /* Check if storag space is allocated. */
    for (chunk = first_chunk; chunk <= last_chunk; chunk++) {
        if (IS_EMPTY (s->table[chunk])) {
            uint32_t id = allocate_chunk (bs);
            if (IS_EMPTY (id)) {
                return NULL;
            }
            id |= DIRTY_TABLE;
            WRITE_TABLE (s->table[chunk], id);

            table_dirty = TRUE;
        } else if (IS_DIRTY (s->table[chunk])) {
            /* This is possible if a previous soft-write allocated the storage
             * space but did not flush the table entry change to the journal
             * and hence did not clean the dirty bit. This is also possible
             * with two concurrent hard-writes. The first hard-write allocated
             * the storage space but has not flushed the table entry change to
             * the journal yet and hence the table entry remains dirty. In
             * this case, the second hard-write will also try to flush this
             * dirty table entry to the journal. The outcome is correct since
             * they store the same metadata change in the journal (although
             * twice). For this race condition, we prefer to have two writes
             * to the journal rather than introducing a locking mechanism,
             * because this happens rarely and those two writes to the journal
             * are likely to be merged by the kernel into a single write since
             * they are likely to update back-to-back sectors in the journal.
             * A locking mechanism would be less efficient, because the large
             * size of chunks would cause unnecessary locking due to ``false
             * sharing'' of a chunk by two writes. */
            table_dirty = TRUE;
        }
    }

    const int update_table = (!soft_write && table_dirty);
    size_t iov_left;
    uint8_t *iov_buf;
    int nb, iov_index, nqiov, niov;
    uint32_t prev;

    if (first_chunk == last_chunk) {
        goto handle_one_continuous_region;
    }

    /* Count the number of qiov and iov needed to cover the continuous regions
     * of the compact image. */
    iov_left = orig_qiov->iov[0].iov_len;
    iov_buf = orig_qiov->iov[0].iov_base;
    iov_index = 0;
    nqiov = 0;
    niov = 0;
    prev = READ_TABLE (s->table[first_chunk]);

    /* Data in the first chunk. */
    nb = s->chunk_size - (sector_num % s->chunk_size);

    for (chunk = first_chunk + 1; chunk <= last_chunk; chunk++) {
        uint32_t current = READ_TABLE (s->table[chunk]);
        int64_t data_size;
        if (chunk < last_chunk) {
            data_size = s->chunk_size;
        } else {
            data_size = (sector_num + nb_sectors) % s->chunk_size;
            if (data_size == 0) {
                data_size = s->chunk_size;
            }
        }

        if (current == prev + 1) {
            nb += data_size;        /* Continue the previous region. */
        } else {
            /* Terminate the previous region. */
            niov +=
                count_iov (orig_qiov->iov, &iov_index, &iov_buf, &iov_left,
                           nb * 512);
            nqiov++;
            nb = data_size;        /* Data in the new region. */
        }
        prev = current;
    }

    if (nqiov == 0) {
      handle_one_continuous_region:
        /* A simple case. All data can be written out in one qiov and no new
         * chunks are allocated. */
        start_sec = READ_TABLE (s->table[first_chunk]) * s->chunk_size +
                                        (sector_num % s->chunk_size);

        if (!update_table && !acb) {
            if (parent_acb) {
                QDEBUG ("STORE: acb%llu-%p  "
                        "store_directly_without_table_update\n",
                        parent_acb->uuid, parent_acb);
            }
            return bdrv_aio_writev (s->fvd_data, s->data_offset + start_sec,
                                    orig_qiov, nb_sectors, cb, opaque);
        }

        if (!acb && !(acb = init_store_acb (soft_write, orig_qiov, bs,
                            sector_num, nb_sectors, parent_acb, cb, opaque))) {
            return NULL;
        }

        QDEBUG ("STORE: acb%llu-%p  store_directly  sector_num=%" PRId64
                " nb_sectors=%d\n", acb->uuid, acb, acb->sector_num,
                acb->nb_sectors);

        acb->store.update_table = update_table;
        acb->store.num_children = 1;
        acb->store.one_child.hd_acb =
            bdrv_aio_writev (s->fvd_data, s->data_offset + start_sec, orig_qiov,
                             nb_sectors, finish_store_data_in_compact_image,
                             &acb->store.one_child);
        if (acb->store.one_child.hd_acb) {
            acb->store.one_child.acb = acb;
            return &acb->common;
        } else {
            my_qemu_aio_release (acb);
            return NULL;
        }
    }

    /* qiov for the last continuous region. */
    niov += count_iov (orig_qiov->iov, &iov_index, &iov_buf,
                       &iov_left, nb * 512);
    nqiov++;
    ASSERT (iov_index == orig_qiov->niov - 1 && iov_left == 0);

    /* Need to submit multiple requests to the lower layer. */
    if (!acb && !(acb = init_store_acb (soft_write, orig_qiov, bs, sector_num,
                                        nb_sectors, parent_acb, cb, opaque))) {
        return NULL;
    }
    acb->store.update_table = update_table;
    acb->store.num_children = nqiov;

    if (!parent_acb) {
        QDEBUG ("STORE: acb%llu-%p  start  sector_num=%" PRId64
                " nb_sectors=%d\n", acb->uuid, acb, acb->sector_num,
                acb->nb_sectors);
    }

    /* Allocate memory and create multiple requests. */
    const size_t metadata_size = nqiov * (sizeof (CompactChildCB) +
                                          sizeof (QEMUIOVector))
                                    + niov * sizeof (struct iovec);
    acb->store.children = (CompactChildCB *) my_qemu_malloc (metadata_size);
    QEMUIOVector *q = (QEMUIOVector *) (acb->store.children + nqiov);
    struct iovec *v = (struct iovec *) (q + nqiov);

    start_sec = READ_TABLE (s->table[first_chunk]) * s->chunk_size +
                                        (sector_num % s->chunk_size);
    nqiov = 0;
    iov_index = 0;
    iov_left = orig_qiov->iov[0].iov_len;
    iov_buf = orig_qiov->iov[0].iov_base;
    prev = READ_TABLE (s->table[first_chunk]);

    /* Data in the first chunk. */
    if (first_chunk == last_chunk) {
        nb = nb_sectors;
    }
    else {
        nb = s->chunk_size - (sector_num % s->chunk_size);
    }

    for (chunk = first_chunk + 1; chunk <= last_chunk; chunk++) {
        uint32_t current = READ_TABLE (s->table[chunk]);
        int64_t data_size;
        if (chunk < last_chunk) {
            data_size = s->chunk_size;
        } else {
            data_size = (sector_num + nb_sectors) % s->chunk_size;
            if (data_size == 0) {
                data_size = s->chunk_size;
            }
        }

        if (current == prev + 1) {
            nb += data_size;        /* Continue the previous region. */
        } else {
            /* Terminate the previous continuous region. */
            niov = setup_iov (orig_qiov->iov, v, &iov_index,
                              &iov_buf, &iov_left, nb * 512);
            qemu_iovec_init_external (q, v, niov);
            QDEBUG ("STORE: acb%llu-%p  create_child %d sector_num=%" PRId64
                    " nb_sectors=%d niov=%d\n", acb->uuid, acb, nqiov,
                    start_sec, q->size / 512, q->niov);
            acb->store.children[nqiov].hd_acb =
                bdrv_aio_writev (s->fvd_data, s->data_offset + start_sec, q,
                                 q->size / 512,
                                 finish_store_data_in_compact_image,
                                 &acb->store.children[nqiov]);
            if (!acb->store.children[nqiov].hd_acb) {
                goto fail;
            }
            acb->store.children[nqiov].acb = acb;
            v += niov;
            q++;
            nqiov++;
            start_sec = current * s->chunk_size; /* Begin of the new region. */
            nb = data_size;        /* Data in the new region. */
        }
        prev = current;
    }

    /* Requst for the last chunk. */
    niov = setup_iov (orig_qiov->iov, v, &iov_index, &iov_buf,
                      &iov_left, nb * 512);
    ASSERT (iov_index == orig_qiov->niov - 1 && iov_left == 0);
    qemu_iovec_init_external (q, v, niov);

    QDEBUG ("STORE: acb%llu-%p  create_child_last %d sector_num=%" PRId64
            " nb_sectors=%d niov=%d\n", acb->uuid, acb, nqiov, start_sec,
            q->size / 512, q->niov);
    acb->store.children[nqiov].hd_acb =
        bdrv_aio_writev (s->fvd_data, s->data_offset + start_sec, q,
                         q->size / 512, finish_store_data_in_compact_image,
                         &acb->store.children[nqiov]);
    if (acb->store.children[nqiov].hd_acb) {
        acb->store.children[nqiov].acb = acb;
        return &acb->common;
    }

    int i;
  fail:
    QDEBUG ("STORE: acb%llu-%p  failed\n", acb->uuid, acb);
    for (i = 0; i < nqiov; i++) {
        bdrv_aio_cancel (acb->store.children[i].hd_acb);
    }
    my_qemu_free (acb->store.children);
    my_qemu_aio_release (acb);
    return NULL;
}

static uint32_t allocate_chunk (BlockDriverState * bs)
{
    BDRVFvdState *s = bs->opaque;

    /* Check if there is sufficient storage space. */
    if (s->used_storage + s->chunk_size > s->data_storage) {
        if (s->add_storage_cmd) {
            if (system (s->add_storage_cmd)) {
                fprintf (stderr, "Error in executing %s\n", s->add_storage_cmd);
            }
        } else {
            /* If the image is stored on a file system, the image file size
             * can be increased by bdrv_truncate. */
            int64_t new_size = (s->data_offset + s->used_storage +
                                s->storage_grow_unit) * 512;
            bdrv_truncate (s->fvd_data, new_size);
        }

        /* Check how much storage is available now. */
        int64_t size = bdrv_getlength (s->fvd_data);
        if (size < 0) {
            fprintf (stderr, "Error in bdrv_getlength(%s)\n", bs->filename);
            return EMPTY_TABLE;
        }
        s->data_storage = size / 512 - s->data_offset;
        if (s->used_storage + s->chunk_size > s->data_storage) {
            fprintf (stderr, "Could not allocate more storage space.\n");
            return EMPTY_TABLE;
        }

        QDEBUG ("Increased storage to %" PRId64 " bytes.\n", size);
    }

    uint32_t allocated_chunk_id = s->used_storage / s->chunk_size;
    s->used_storage += s->chunk_size;
    return allocated_chunk_id;
}

static void finish_store_data_in_compact_image (void *opaque, int ret)
{
    CompactChildCB *child = opaque;
    FvdAIOCB *acb = child->acb;

    /* Now fvd_store_compact_cancel(), if invoked, won't cancel this child
     * request. */
    child->hd_acb = NULL;

    if (acb->store.ret == 0) {
        acb->store.ret = ret;
    } else {
        QDEBUG ("STORE: acb%llu-%p  store_child=%d total_children=%d error "
                "ret=%d\n", acb->uuid, acb, acb->store.finished_children,
             acb->store.num_children, ret);
    }

    acb->store.finished_children++;
    if (acb->store.finished_children < acb->store.num_children) {
        QDEBUG ("STORE: acb%llu-%p  store_finished_children=%d "
                "total_children=%d\n", acb->uuid, acb,
                acb->store.finished_children, acb->store.num_children);
        return;
    }

    /* All child requests finished. Free buffers. */
    if (acb->store.children) {
        my_qemu_free (acb->store.children);
        acb->store.children = NULL;
    }

    if (acb->store.ret) {        /* error */
        QDEBUG ("STORE: acb%llu-%p  "
                "store_last_child_finished_with_error ret=%d\n",
                acb->uuid, acb, acb->store.ret);
        acb->common.cb (acb->common.opaque, acb->store.ret);
        my_qemu_aio_release (acb);
        return;
    }

    if (!acb->store.update_table) {
        QDEBUG ("STORE: acb%llu-%p  "
                "store_last_child_finished_without_table_update\n",
                acb->uuid, acb);
        acb->common.cb (acb->common.opaque, acb->store.ret);
        my_qemu_aio_release (acb);
        return;
    }

    /* Check whether the table entries are still dirty. Note that while saving
     * this write to disk, other writes might have already flushed the dirty
     * table entries to the journal. If those table entries are no longer
     * dirty, depending on the behavior of parent_acb, it might be able to
     * skip a journal update. */
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;
    uint32_t first_chunk = acb->sector_num / s->chunk_size;
    const uint32_t last_chunk =
        (acb->sector_num + acb->nb_sectors - 1) / s->chunk_size;
    int update_table = FALSE;
    uint32_t chunk;
    for (chunk = first_chunk; chunk <= last_chunk; chunk++) {
        if (IS_DIRTY (s->table[chunk])) {
            update_table = TRUE;
            break;
        }
    }

    if (acb->store.parent_acb) {
        /* Metadata update will be handled by the parent write. */
        ASSERT (acb->store.parent_acb->type == OP_WRITE);
        QDEBUG ("STORE: acb%llu-%p  "
                "store_last_child_finished_with_parent_do_table_update\n",
                acb->uuid, acb);
        acb->store.parent_acb->write.update_table = update_table;
        acb->common.cb (acb->common.opaque, acb->store.ret);
        my_qemu_aio_release (acb);
        return;
    }

    if (update_table) {
        QDEBUG ("STORE: acb%llu-%p  "
                "store_last_child_finished_and_start_table_update\n",
                acb->uuid, acb);
        write_metadata_to_journal (acb);
    } else {
        QDEBUG ("STORE: acb%llu-%p  "
                "store_last_child_finished_without_table_update\n",
                acb->uuid, acb);
        acb->common.cb (acb->common.opaque, acb->store.ret);
        my_qemu_aio_release (acb);
    }
}

static inline FvdAIOCB *init_store_acb (int soft_write,
                                        QEMUIOVector * orig_qiov,
                                        BlockDriverState * bs,
                                        int64_t sector_num, int nb_sectors,
                                        FvdAIOCB * parent_acb,
                                        BlockDriverCompletionFunc * cb,
                                        void *opaque)
{
    FvdAIOCB *acb = my_qemu_aio_get (&fvd_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }
    acb->type = OP_STORE_COMPACT;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->store.soft_write = soft_write;
    acb->store.orig_qiov = orig_qiov;
    acb->store.parent_acb = parent_acb;
    acb->store.finished_children = 0;
    acb->store.num_children = 0;
    acb->store.one_child.hd_acb = NULL;
    acb->store.children = NULL;
    acb->store.ret = 0;
    acb->jcb.iov.iov_base = NULL;
    acb->jcb.hd_acb = NULL;
    acb->jcb.next_wait_for_journal.le_prev = NULL;
    COPY_UUID (acb, parent_acb);

    return acb;
}

static void fvd_store_compact_cancel (FvdAIOCB * acb)
{
    if (acb->store.children) {
        int i;
        for (i = 0; i < acb->store.num_children; i++) {
            if (acb->store.children[i].hd_acb) {
                bdrv_aio_cancel (acb->store.children[i].hd_acb);
            }
        }
        my_qemu_free (acb->store.children);
    }
    if (acb->store.one_child.hd_acb) {
        bdrv_aio_cancel (acb->store.one_child.hd_acb);
    }
    if (acb->jcb.hd_acb) {
        bdrv_aio_cancel (acb->jcb.hd_acb);
        free_journal_sectors (acb->common.bs->opaque);
    }
    if (acb->jcb.iov.iov_base != NULL) {
        my_qemu_vfree (acb->jcb.iov.iov_base);
    }
    if (acb->jcb.next_wait_for_journal.le_prev) {
        QLIST_REMOVE (acb, jcb.next_wait_for_journal);
    }

    my_qemu_aio_release (acb);
}
