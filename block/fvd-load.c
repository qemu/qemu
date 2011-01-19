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
 *  A short description: this FVD module implements loading data from a
 *  compact image.
 *============================================================================*/

static void aio_wrapper_bh (void *opaque);
static void finish_load_data_from_compact_image (void *opaque, int ret);
static inline FvdAIOCB *init_load_acb (FvdAIOCB * parent_acb,
                                       BlockDriverState * bs,
                                       int64_t sector_num,
                                       QEMUIOVector * orig_qiov, int nb_sectors,
                                       BlockDriverCompletionFunc * cb,
                                       void *opaque);

static inline BlockDriverAIOCB *load_data (FvdAIOCB * parent_acb,
                                           BlockDriverState * bs,
                                           int64_t sector_num,
                                           QEMUIOVector * orig_qiov,
                                           int nb_sectors,
                                           BlockDriverCompletionFunc * cb,
                                           void *opaque)
{
    BDRVFvdState *s = bs->opaque;

    if (!s->table) {
        /* Load directly since it is not a compact image. */
        return bdrv_aio_readv (s->fvd_data, s->data_offset + sector_num,
                               orig_qiov, nb_sectors, cb, opaque);
    } else {
        return load_data_from_compact_image (NULL, parent_acb, bs, sector_num,
                                             orig_qiov, nb_sectors, cb, opaque);
    }
}

static BlockDriverAIOCB *
load_data_from_compact_image (FvdAIOCB * acb, FvdAIOCB * parent_acb,
                              BlockDriverState * bs, int64_t sector_num,
                              QEMUIOVector * orig_qiov, int nb_sectors,
                              BlockDriverCompletionFunc * cb, void *opaque)
{
    BDRVFvdState *s = bs->opaque;
    const uint32_t first_chunk = sector_num / s->chunk_size;
    const uint32_t last_chunk = (sector_num + nb_sectors - 1) / s->chunk_size;
    uint32_t chunk;
    int64_t start_sec;
    int i;

    if (first_chunk == last_chunk) {
        goto handle_one_continuous_region;
    }

    /* Count the number of qiov and iov needed to cover the continuous regions
     * of the compact image. */
    int iov_index = 0;
    size_t iov_left = orig_qiov->iov[0].iov_len;
    uint8_t *iov_buf = orig_qiov->iov[0].iov_base;
    int nqiov = 0;
    int nziov = 0;        /* Number of empty regions. */
    int niov = 0;
    uint32_t prev = READ_TABLE2 (s->table[first_chunk]);

    /* Amount of data in the first chunk. */
    int nb = s->chunk_size - (sector_num % s->chunk_size);

    for (chunk = first_chunk + 1; chunk <= last_chunk; chunk++) {
        uint32_t current = READ_TABLE2 (s->table[chunk]);
        int64_t data_size;
        if (chunk < last_chunk) {
            data_size = s->chunk_size;
        } else {
            data_size = (sector_num + nb_sectors) % s->chunk_size;
            if (data_size == 0) {
                data_size = s->chunk_size;
            }
        }

        if ((IS_EMPTY (current) && IS_EMPTY (prev)) ||
            (!IS_EMPTY (prev) && !IS_EMPTY (current) && current == prev + 1)) {
            nb += data_size;        /* Belong to the previous continuous region. */
        } else {
            /* Terminate the previous continuous region. */
            if (IS_EMPTY (prev)) {
                /* Skip this empty region. */
                count_iov (orig_qiov->iov, &iov_index, &iov_buf,
                           &iov_left, nb * 512);
                nziov++;
            } else {
                niov += count_iov (orig_qiov->iov, &iov_index, &iov_buf,
                                   &iov_left, nb * 512);
                nqiov++;
            }
            nb = data_size;        /* Data in the new region. */
        }
        prev = current;
    }

    if (nqiov == 0 && nziov == 0) {
        /* All data can be read in one qiov. Reuse orig_qiov. */
      handle_one_continuous_region:
        if (IS_EMPTY (s->table[first_chunk])) {
            /* Fill qiov with zeros. */
            for (i = 0; i < orig_qiov->niov; i++) {
                memset (orig_qiov->iov[i].iov_base,
                        0, orig_qiov->iov[i].iov_len);
            }

            /* Use a bh to invoke the callback. */
            if (!acb) {
                if (!(acb = my_qemu_aio_get (&fvd_aio_pool, bs, cb, opaque))) {
                    return NULL;
                }
                COPY_UUID (acb, parent_acb);
            }

            QDEBUG ("LOAD: acb%llu-%p  load_fill_all_with_zeros\n",
                    acb->uuid, acb);
            acb->type = OP_WRAPPER;
            acb->wrapper.bh = qemu_bh_new (aio_wrapper_bh, acb);
            qemu_bh_schedule (acb->wrapper.bh);
            return &acb->common;
        }

        /* A non-empty region. */
        start_sec = READ_TABLE (s->table[first_chunk]) * s->chunk_size +
                                    (sector_num % s->chunk_size);
        if (!acb) {
            if (parent_acb) {
                QDEBUG ("LOAD: acb%llu-%p  "
                        "load_directly_as_one_continuous_region\n",
                        parent_acb->uuid, acb);
            }
            return bdrv_aio_readv (s->fvd_data, s->data_offset + start_sec,
                                   orig_qiov, nb_sectors, cb, opaque);
        }

        QDEBUG ("LOAD: acb%llu-%p  load_directly_as_one_continuous_region\n",
                acb->uuid, acb);
        acb->load.num_children = 1;
        acb->load.one_child.hd_acb =
            bdrv_aio_readv (s->fvd_data, s->data_offset + start_sec, orig_qiov,
                            nb_sectors, finish_load_data_from_compact_image,
                            &acb->load.one_child);
        if (acb->load.one_child.hd_acb) {
            acb->load.one_child.acb = acb;
            return &acb->common;
        } else {
            my_qemu_aio_release (acb);
            return NULL;
        }
    }

    /* qiov for the last continuous region. */
    if (!IS_EMPTY (prev)) {
        niov += count_iov (orig_qiov->iov, &iov_index, &iov_buf,
                           &iov_left, nb * 512);
        nqiov++;
        ASSERT (iov_index == orig_qiov->niov - 1 && iov_left == 0);
    }

    /* Need to submit multiple requests to the lower layer. Initialize acb. */
    if (!acb && !(acb = init_load_acb (parent_acb, bs, sector_num,
                                       orig_qiov, nb_sectors, cb, opaque))) {
        return NULL;
    }
    acb->load.num_children = nqiov;

    /* Allocate memory and create multiple requests. */
    acb->load.children = my_qemu_malloc ((sizeof (CompactChildCB) +
                                          sizeof (QEMUIOVector)) * nqiov +
                                         sizeof (struct iovec) * niov);
    QEMUIOVector *q = (QEMUIOVector *) (acb->load.children + nqiov);
    struct iovec *v = (struct iovec *) (q + nqiov);

    /* Set up iov and qiov. */
    nqiov = 0;
    iov_index = 0;
    iov_left = orig_qiov->iov[0].iov_len;
    iov_buf = orig_qiov->iov[0].iov_base;
    nb = s->chunk_size - (sector_num % s->chunk_size); /* Data in first chunk.*/
    prev = READ_TABLE2 (s->table[first_chunk]);

    /* if (IS_EMPTY(prev)), start_sec will not be used later, and hence safe. */
    start_sec = prev * s->chunk_size + (sector_num % s->chunk_size);

    for (chunk = first_chunk + 1; chunk <= last_chunk; chunk++) {
        uint32_t current = READ_TABLE2 (s->table[chunk]);
        int64_t data_size;
        if (chunk < last_chunk) {
            data_size = s->chunk_size;
        } else {
            data_size = (sector_num + nb_sectors) % s->chunk_size;
            if (data_size == 0) {
                data_size = s->chunk_size;
            }
        }

        if ((IS_EMPTY (prev) && IS_EMPTY (current)) ||
            (!IS_EMPTY (prev) && !IS_EMPTY (current) && current == prev + 1)) {
            nb += data_size;        /* Continue the previous region. */
        } else {
            /* Terminate the previous continuous region. */
            if (IS_EMPTY (prev)) {
                zero_iov (orig_qiov->iov, &iov_index, &iov_buf, &iov_left,
                          nb * 512);        /* Fill iov data with zeros. */
            } else {
                niov = setup_iov (orig_qiov->iov, v, &iov_index, &iov_buf,
                                  &iov_left, nb * 512);
                qemu_iovec_init_external (q, v, niov);
                QDEBUG ("LOAD: acb%llu-%p  create_child %d sector_num=%" PRId64
                        " nb_sectors=%d niov=%d\n", acb->uuid, acb, nqiov,
                        start_sec, nb, niov);
                acb->load.children[nqiov].hd_acb =
                    bdrv_aio_readv (s->fvd_data, s->data_offset + start_sec, q,
                                    nb, finish_load_data_from_compact_image,
                                    &acb->load.children[nqiov]);
                if (!acb->load.children[nqiov].hd_acb) {
                    goto fail;
                }
                acb->load.children[nqiov].acb = acb;
                v += niov;
                q++;
                nqiov++;
            }

            nb = data_size;

            /* if (IS_EMPTY(current)), start_sec will not be used later. */
            start_sec = current * s->chunk_size;
        }
        prev = current;
    }

    /* The last continuous region. */
    if (IS_EMPTY (prev)) {
        zero_iov (orig_qiov->iov, &iov_index, &iov_buf, &iov_left, nb * 512);
    } else {
        niov = setup_iov (orig_qiov->iov, v, &iov_index, &iov_buf,
                          &iov_left, nb * 512);
        qemu_iovec_init_external (q, v, niov);
        QDEBUG ("LOAD: acb%llu-%p  create_child %d sector_num=%" PRId64
                " nb_sectors=%d niov=%d\n", acb->uuid, acb, nqiov, start_sec,
                nb, niov);
        acb->load.children[nqiov].hd_acb =
            bdrv_aio_readv (s->fvd_data, s->data_offset + start_sec, q, nb,
                            finish_load_data_from_compact_image,
                            &acb->load.children[nqiov]);
        if (!acb->load.children[nqiov].hd_acb) {
            goto fail;
        }
        acb->load.children[nqiov].acb = acb;
    }
    ASSERT (iov_index == orig_qiov->niov - 1 && iov_left == 0);

    return &acb->common;

  fail:
    for (i = 0; i < nqiov; i++) {
        bdrv_aio_cancel (acb->load.children[i].hd_acb);
    }
    my_qemu_free (acb->load.children);
    my_qemu_aio_release (acb);
    return NULL;
}

static void aio_wrapper_bh (void *opaque)
{
    FvdAIOCB *acb = opaque;
    acb->common.cb (acb->common.opaque, 0);
    qemu_bh_delete (acb->wrapper.bh);
    my_qemu_aio_release (acb);
}

static void finish_load_data_from_compact_image (void *opaque, int ret)
{
    CompactChildCB *child = opaque;
    FvdAIOCB *acb = child->acb;

    /* Now fvd_store_compact_cancel(), if invoked, won't cancel this child
     * request. */
    child->hd_acb = NULL;

    if (acb->load.ret == 0) {
        acb->load.ret = ret;
    } else {
        QDEBUG ("LOAD: acb%llu-%p  load_child=%d total_children=%d "
                "error ret=%d\n", acb->uuid, acb, acb->load.finished_children,
                acb->load.num_children, ret);
    }

    acb->load.finished_children++;
    if (acb->load.finished_children < acb->load.num_children) {
        QDEBUG ("LOAD: acb%llu-%p  load_finished_children=%d "
                "total_children=%d\n", acb->uuid, acb,
                acb->load.finished_children, acb->load.num_children);
        return;
    }

    QDEBUG ("LOAD: acb%llu-%p  load_last_child_finished ret=%d\n", acb->uuid,
            acb, acb->load.ret);
    acb->common.cb (acb->common.opaque, acb->load.ret);
    if (acb->load.children) {
        my_qemu_free (acb->load.children);
    }
    my_qemu_aio_release (acb);
}

static inline FvdAIOCB *init_load_acb (FvdAIOCB * parent_acb,
                                       BlockDriverState * bs,
                                       int64_t sector_num,
                                       QEMUIOVector * orig_qiov,
                                       int nb_sectors,
                                       BlockDriverCompletionFunc * cb,
                                       void *opaque)
{
    FvdAIOCB *const acb = my_qemu_aio_get (&fvd_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }
    acb->type = OP_LOAD_COMPACT;
    acb->sector_num = sector_num;
    acb->nb_sectors = nb_sectors;
    acb->load.parent_acb = parent_acb;
    acb->load.finished_children = 0;
    acb->load.children = NULL;
    acb->load.one_child.hd_acb = NULL;
    acb->load.orig_qiov = orig_qiov;
    acb->load.ret = 0;
    COPY_UUID (acb, parent_acb);
    return acb;
}

static void fvd_wrapper_cancel (FvdAIOCB * acb)
{
    qemu_bh_cancel (acb->wrapper.bh);
    qemu_bh_delete (acb->wrapper.bh);
    my_qemu_aio_release (acb);
}

static void fvd_load_compact_cancel (FvdAIOCB * acb)
{
    if (acb->load.children) {
        int i;
        for (i = 0; i < acb->load.num_children; i++) {
            if (acb->load.children[i].hd_acb) {
                bdrv_aio_cancel (acb->load.children[i].hd_acb);
            }
        }
        my_qemu_free (acb->load.children);
    }
    if (acb->load.one_child.hd_acb) {
        bdrv_aio_cancel (acb->load.one_child.hd_acb);
    }
    my_qemu_aio_release (acb);
}
