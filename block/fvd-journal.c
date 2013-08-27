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
 *  A short description: this FVD module implements a journal for committing
 *  metadata changes. Each sector in the journal is self-contained so that
 *  updates are atomic. A sector may contain one or multiple journal records.
 *  There are two types of journal records:
 * bitmap_update and table_update.
 *   Format of a bitmap_update record:
 *         + BITMAP_JRECORD (uint32_t)
 *         + num_dirty_sectors (uint32_t)
 *         + dirty_sector_begin (int64_t)
 *   Format of a table_update record:
 *         + TABLE_JRECORD (uint32_t)
 *         + dirty_table_offset (uint32_t)
 *         + num_dirty_table_entries (uint32_t)
 *         +   table_entry_1 (uint32_t)
 *         +   table_entry_2 (uint32_t)
 *         +   ...
 * If both the bitmap and the table need update, one sector contains a
 * TABLE_JRECORD and a BITMAP_JRECORD, and these two records cover
 * the same range of virtual disk data so that the corresponding parts of the
 * bitmap and the table are always updated in one atomic operation.
 *============================================================================*/

#define BITMAP_JRECORD                 ((uint32_t)0x3F2AB8ED)
#define TABLE_JRECORD                ((uint32_t)0xB4E6F7AC)
#define EMPTY_JRECORD                ((uint32_t)0)
#define BITMAP_JRECORD_SIZE         (2*sizeof(uint32_t) + sizeof(int64_t))
#define TABLE_JRECORD_HDR_SIZE         (3*sizeof(uint32_t))
#define TABLE_JRECORDS_PER_SECTOR \
                ((512 - TABLE_JRECORD_HDR_SIZE)/sizeof(uint32_t))

/* One BITMAP_JRECORD and this number of BITMAP_JRECORDs can fit
 * in one journal sector. */
#define MIXED_JRECORDS_PER_SECTOR ((512 - TABLE_JRECORD_HDR_SIZE - \
                                BITMAP_JRECORD_SIZE) / sizeof(uint32_t))

static inline int64_t calc_min_journal_size (int64_t table_entries)
{
    return (table_entries + MIXED_JRECORDS_PER_SECTOR - 1)
                            / MIXED_JRECORDS_PER_SECTOR * 512;
}

static int init_journal (int read_only, BlockDriverState * bs,
                         FvdHeader * header)
{
    BDRVFvdState *s = bs->opaque;
    s->journal_size = header->journal_size / 512;
    s->journal_offset = header->journal_offset / 512;
    s->next_journal_sector = 0;

    if (read_only) {
        return 0;
    }

    if (s->journal_size <= 0) {
        if (!s->table && !s->fresh_bitmap) {
            return 0;        /* No need to use the journal. */
        }

        if (!header->clean_shutdown) {
            fprintf (stderr, "ERROR: the image may be corrupted because it was "
                     "not shut down gracefully last\ntime and it does not use "
                     "a journal. You may continue to use the image at your\n"
                     "own risk by manually resetting the clean_shutdown flag "
                     "in the image.\n\n");
            s->dirty_image = TRUE;
            if (in_qemu_tool) {
                return 0;        /* Allow qemu tools to use the image. */
            } else {
                /* Do not allow boot the VM until the clean_shutdown flag is
                 * manually cleaned. */
                return -1;
            }
        }

        QDEBUG ("Journal is disabled\n");
        return 0;
    }

    if (header->clean_shutdown) {
        QDEBUG ("Journal is skipped as the VM was shut down gracefully "
                "last time.\n");
        return 0;
    }

    QDEBUG ("Recover from the journal as the VM was not shut down gracefully "
            "last time.\n");

    uint8_t *journal = my_qemu_blockalign (s->fvd_metadata,
                                           s->journal_size * 512);
    int ret = bdrv_read (s->fvd_metadata, s->journal_offset,
                         journal, s->journal_size);
    if (ret < 0) {
        my_qemu_vfree (journal);
        fprintf (stderr, "Failed to read the journal (%" PRId64 ") bytes\n",
                 s->journal_size * 512);
        return -1;
    }

    /* Go through every journal sector. */
    uint8_t *sector = journal;
    uint8_t *journal_end = journal + s->journal_size * 512;
    while (sector < journal_end) {
        uint32_t *type = (uint32_t *) sector;        /* Journal record type. */
        while ((uint8_t *) type < (sector + 512)) {
            if (le32_to_cpu (*type) == BITMAP_JRECORD) {
                uint32_t *nb_sectors = type + 1; /* BITMAP_JRECORD field 2. */
                int64_t *sector_num = (int64_t *) (type + 2);        /* field 3. */
                if (s->stale_bitmap) {
                    update_both_bitmaps (s, le64_to_cpu (*sector_num),
                                     le32_to_cpu (*nb_sectors));
                    QDEBUG ("JOURNAL: recover BITMAP_JRECORD sector_num=%"
                            PRId64 " nb_sectors=%u\n",
                            le64_to_cpu (*sector_num),
                            le32_to_cpu (*nb_sectors));
                }

                /* First field of the next journal record. */
                type = (uint32_t *) sector_num + 1;
            } else if (le32_to_cpu (*type) == TABLE_JRECORD) {
                uint32_t *offset = type + 1;        /* TABLE_JRECORD field 2. */
                uint32_t *count = type + 2;        /* TABLE_JRECORD field 3. */
                uint32_t *content = type + 3;        /* fields 4 and beyond. */
                const uint32_t chunk = le32_to_cpu (*offset);
                const uint32_t n = le32_to_cpu (*count);
                uint32_t i;
                for (i = 0; i < n; i++) {
                    s->table[chunk + i] = content[i];

                    /* The dirty bit was not cleaned when the table entry was
                     * saved in the journal. */
                    CLEAN_DIRTY2 (s->table[chunk + i]);
                }
                type = content + n; /* First field of the next record. */
                QDEBUG ("JOURNAL: recover TABLE_JRECORD chunk_start=%u "
                        "nb_chunks=%u\n", chunk, n);
            } else {
                /* End of valid records in this journal sector. */
                ASSERT (le32_to_cpu (*type) == EMPTY_JRECORD);
                break;
            }
        }

        sector += 512;
    }
    my_qemu_vfree (journal);
    flush_metadata_to_disk (bs);        /* Write the recovered metadata. */

    return 0;
}

/*
 * This function first flushes in-memory metadata to disk and then recycle the
 * used journal sectors. It is possible to make this operation asynchronous so
 * that the performance is better.  However, the overall performance
 * improvement may be limited since recycling the journal happens very
 * infrequently and updating on-disk metadata finishes quickly because of the
 * small size of the metadata.
 */
static void recycle_journal (BDRVFvdState * s)
{
#ifdef FVD_DEBUG
    static int64_t recycle_count = 0;
    QDEBUG ("JOURNAL: start journal recycle %" PRId64 ".\n", recycle_count);
    recycle_count++;
    int64_t begin_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
#endif

    /* Write fresh_bitmap to disk. */
    if (s->fresh_bitmap) {
        int nb = (int) (s->bitmap_size / 512);
        QDEBUG ("JOURNAL: flush bitmap (%d sectors) to disk\n", nb);

        /* How to recover if this write fails? */
        bdrv_write (s->fvd_metadata, s->bitmap_offset, s->fresh_bitmap, nb);

        if (s->fresh_bitmap != s->stale_bitmap) {
            memcpy (s->stale_bitmap, s->fresh_bitmap, s->bitmap_size);
        }
    }

    /* Clean DIRTY_TABLE bit and write the table to disk. */
    if (s->table) {
        int table_entries =
            (int) (ROUND_UP (s->virtual_disk_size, s->chunk_size * 512) /
                   (s->chunk_size * 512));
        int i;
        for (i = 0; i < table_entries; i++) {
            CLEAN_DIRTY (s->table[i]);
        }

        int64_t table_size = sizeof (uint32_t) * table_entries;
        table_size = ROUND_UP (table_size, DEF_PAGE_SIZE);
        int nb = (int) (table_size / 512);
        QDEBUG ("JOURNAL: flush table (%d sectors) to disk\n", nb);

        /* How to recover if this write fails? */
        bdrv_write (s->fvd_metadata, s->table_offset, (uint8_t *) s->table, nb);
    }
    s->next_journal_sector = 0;

#ifdef FVD_DEBUG
    int64_t end_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    QDEBUG ("JOURNAL: journal recycle took %" PRId64 " ms.\n",
            (end_time - begin_time));
#endif
}

static void free_journal_sectors (BDRVFvdState * s)
{
    if (s->journal_size <= 0) {
        return;
    }

    s->ongoing_journal_updates--;
    ASSERT (s->ongoing_journal_updates >= 0);
    if (s->ongoing_journal_updates > 0 || QLIST_EMPTY (&s->wait_for_journal)) {
        return;
    }

    /* Some requests are waiting for the journal to be recycled in order to
     * get free journal sectors. */
    recycle_journal (s);

    /* Restart requests in the wait_for_journal list.  First make a copy of
     * the head and then empty the head. */
    FvdAIOCB *acb = QLIST_FIRST (&s->wait_for_journal);
    QLIST_INIT (&s->wait_for_journal);
    FvdAIOCB *next;

    /* Restart all dependent requests. Cannot use QLIST_FOREACH here, because
     * the next link might not be the same any more after the callback. */
    while (acb) {
        next = acb->jcb.next_wait_for_journal.le_next;
        acb->jcb.next_wait_for_journal.le_prev = NULL;
        QDEBUG ("WRITE: acb%llu-%p  restart_write_metadata_to_journal "
                "after recycle_journal\n", acb->uuid, acb);
        write_metadata_to_journal (acb);
        acb = next;
    }
}

static int64_t allocate_journal_sectors (BDRVFvdState * s, FvdAIOCB * acb,
                                         int num_sectors)
{
    ASSERT (num_sectors <= s->journal_size);

    if (!QLIST_EMPTY (&s->wait_for_journal)) {
        /* Waiting for journal recycle to finish. */
        ASSERT (s->ongoing_journal_updates > 0);
        QDEBUG ("WRITE: acb%llu-%p  wait_for_journal_recycle\n",
                acb->uuid, acb);
        QLIST_INSERT_HEAD (&s->wait_for_journal, acb,
                           jcb.next_wait_for_journal);
        return -1;
    }

    int64_t journal_sec;
    if (s->next_journal_sector + num_sectors <= s->journal_size) {
      alloc_sector:
        journal_sec = s->next_journal_sector;
        s->next_journal_sector += num_sectors;
        s->ongoing_journal_updates++;
        return journal_sec;
    }

    /* No free journal sector is available. Check if the journal can be
     * recycled now. */
    if (s->ongoing_journal_updates == 0) {
        recycle_journal (s);
        goto alloc_sector;
    }

    /* Waiting for journal recycle to finish. It will be waken up later in
     * free_journal_sectors(). */
    QLIST_INSERT_HEAD (&s->wait_for_journal, acb, jcb.next_wait_for_journal);
    QDEBUG ("WRITE: acb%llu-%p  wait_for_journal_recycle\n", acb->uuid, acb);
    return -1;
}

static void finish_write_journal (void *opaque, int ret)
{
    FvdAIOCB *acb = (FvdAIOCB *) opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    if (ret == 0) {
        QDEBUG ("JOURNAL: acb%llu-%p  finish_write_journal\n", acb->uuid, acb);

        if (s->table) {
            /* Update the table. */
            int i;
            const uint32_t first_chunk = acb->sector_num / s->chunk_size;
            const uint32_t last_chunk = (acb->sector_num + acb->nb_sectors - 1)
                                                            / s->chunk_size;
            for (i = first_chunk; i <= last_chunk; i++) {
                CLEAN_DIRTY2 (s->table[i]);
            }
        }

        if (s->stale_bitmap) {
            /* If fresh_bitmap differs from stale_bitmap, fresh_bitmap has
             * already been updated in finish_write_data() when invoking
             * update_fresh_bitmap_and_check_stale_bitmap(). */
            update_stale_bitmap (s, acb->sector_num, acb->nb_sectors);
        }
    } else {
        QDEBUG ("JOURNAL: acb%llu-%p  finish_write_journal error ret=%d\n",
                acb->uuid, acb, ret);
    }

    /* Clean up. */
    if (acb->type == OP_STORE_COMPACT) {
        acb->common.cb (acb->common.opaque, ret);
        if (acb->jcb.iov.iov_base != NULL) {
            my_qemu_vfree (acb->jcb.iov.iov_base);
        }
        my_qemu_aio_release (acb);
    } else {
        ASSERT (acb->type == OP_WRITE);
        finish_write (acb, ret);
    }

    free_journal_sectors (s);
}

static void write_metadata_to_journal (FvdAIOCB * acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;
    int64_t journal_sec;
    int num_journal_sectors;

    ASSERT ((s->table || s->fresh_bitmap)
            && (acb->type == OP_WRITE || acb->type == OP_STORE_COMPACT));

    /* Is journal is disabled? */
    if (s->journal_size <= 0) {
        finish_write_journal (acb, 0);
        return;
    }

    if (!s->table) {
        /* Only update the bitmap. */
        num_journal_sectors = 1;
        journal_sec = allocate_journal_sectors (s, acb, num_journal_sectors);
        if (journal_sec < 0) {
            /* No journal sector is available now. It will be waken up later
             * in free_journal_sectors(). */
            return;
        }
        acb->jcb.iov.iov_len = 512;
        acb->jcb.iov.iov_base = my_qemu_blockalign (s->fvd_metadata, 512);

        uint32_t *type = (uint32_t *) acb->jcb.iov.iov_base; /* Field 1. */
        uint32_t *nb_sectors = type + 1;        /* BITMAP_JRECORD field 2. */
        int64_t *sector_num = (int64_t *) (type + 2);        /* field 3. */
        *type = cpu_to_le32 (BITMAP_JRECORD);
        *nb_sectors = cpu_to_le32 ((uint32_t) acb->nb_sectors);
        *sector_num = cpu_to_le64 (acb->sector_num);
        *((uint32_t *) (sector_num + 1)) = EMPTY_JRECORD;/* Mark record end. */

    } else if (!s->fresh_bitmap) {
        /* Only update the table. */
        const int64_t first_chunk = acb->sector_num / s->chunk_size;
        const int64_t last_chunk = (acb->sector_num + acb->nb_sectors - 1)
                                                            / s->chunk_size;
        int num_chunks = last_chunk - first_chunk + 1;
        num_journal_sectors = (num_chunks + TABLE_JRECORDS_PER_SECTOR - 1)
                                                / TABLE_JRECORDS_PER_SECTOR;
        journal_sec = allocate_journal_sectors (s, acb, num_journal_sectors);
        if (journal_sec < 0) {
            /* No journal sector is available now. It will be waken up later
             * in free_journal_sectors(). */
            return;
        }

        acb->jcb.iov.iov_len = num_journal_sectors * 512;
        acb->jcb.iov.iov_base = my_qemu_blockalign (s->fvd_metadata,
                                                    acb->jcb.iov.iov_len);

        uint32_t *type = (uint32_t *) acb->jcb.iov.iov_base; /* Field 1. */
        int64_t chunk = first_chunk;

        while (1) {
            /* Start a new journal sector. */
            uint32_t *offset = type + 1;        /* TABLE_JRECORD field 2. */
            uint32_t *count = type + 2;        /* TABLE_JRECORD field 3. */
            uint32_t *content = type + 3;        /* Fields 4 and beyond. */
            *type = cpu_to_le32 (TABLE_JRECORD);
            *offset = cpu_to_le32 (chunk);

            if (num_chunks <= TABLE_JRECORDS_PER_SECTOR) {
                /* This is the last journal sector. */
                *count = cpu_to_le32 (num_chunks);
                memcpy (content, &s->table[chunk],
                        sizeof (uint32_t) * num_chunks);
                if (num_chunks < TABLE_JRECORDS_PER_SECTOR) {
                    *(content + num_chunks) = EMPTY_JRECORD; /* Mark end. */
                }
                break;
            }

            *count = cpu_to_le32 (TABLE_JRECORDS_PER_SECTOR);
            memcpy (content, &s->table[chunk],
                    sizeof (uint32_t) * TABLE_JRECORDS_PER_SECTOR);
            chunk += TABLE_JRECORDS_PER_SECTOR;
            num_chunks -= TABLE_JRECORDS_PER_SECTOR;

            /* Next TABLE_JRECORD field 1. */
            type = content + TABLE_JRECORDS_PER_SECTOR;
        }
    } else {
        /* Update both the table and the bitmap. It may use multiple journal
         * sectors. Each sector is self-contained, including a TABLE_JRECORD
         * and a BITMAP_JRECORD. The two records one the same sector cover the
         * same range of virtual disk data.  The purpose is to update the
         * corresponding parts of the bitmap and the table in one atomic
         * operation. */
        const int64_t first_chunk = acb->sector_num / s->chunk_size;
        const int64_t last_chunk = (acb->sector_num + acb->nb_sectors - 1)
                                                / s->chunk_size;
        int num_chunks = last_chunk - first_chunk + 1;
        num_journal_sectors = (num_chunks + MIXED_JRECORDS_PER_SECTOR - 1)
                                                / MIXED_JRECORDS_PER_SECTOR;
        journal_sec = allocate_journal_sectors (s, acb, num_journal_sectors);
        if (journal_sec < 0) {
            /* No journal sector is available now. It will be waken up later
             * in free_journal_sectors(). */
            return;
        }
        acb->jcb.iov.iov_len = num_journal_sectors * 512;
        acb->jcb.iov.iov_base = my_qemu_blockalign (s->fvd_metadata,
                                                    acb->jcb.iov.iov_len);

        uint32_t *type = (uint32_t *) acb->jcb.iov.iov_base; /* Field 1. */
        int64_t chunk = first_chunk;
        int64_t sector_num = acb->sector_num;
        uint32_t nb_sectors;
        if (num_journal_sectors == 1) {
            nb_sectors = acb->nb_sectors;
        } else {
            /* Number of sectors that fall into the first chunk. */
            nb_sectors = (first_chunk + MIXED_JRECORDS_PER_SECTOR)
                                    * s->chunk_size - acb->sector_num;
        }

        while (1) {
            /* Start a new journal sector. */
            uint32_t *offset = type + 1;        /* TABLE_JRECORD field 2. */
            uint32_t *count = type + 2;                /* TABLE_JRECORD field 3. */
            uint32_t *content = type + 3;         /* Fields 4 and beyond. */
            *type = cpu_to_le32 (TABLE_JRECORD);
            *offset = cpu_to_le32 (chunk);

            if (num_chunks <= MIXED_JRECORDS_PER_SECTOR) {
                /* This is the last journal sector. */
                *count = cpu_to_le32 (num_chunks);
                memcpy (content, &s->table[chunk],
                        sizeof (uint32_t) * num_chunks);

                /* A BITMAP_JRECORD follows a TABLE_JRECORD so that they are
                 * updated in one atomic operatoin. */
                type = content + num_chunks;        /* BITMAP_JRECORD field 1. */
                uint32_t *p_nb_sectors = type + 1; /* BITMAP_JRECORD field 2. */
                int64_t *p_sector_num = (int64_t *) (type + 2);        /* Field 3. */
                *type = cpu_to_le32 (BITMAP_JRECORD);
                *p_nb_sectors = cpu_to_le32 (nb_sectors);
                *p_sector_num = cpu_to_le64 (sector_num);

                if (num_chunks < MIXED_JRECORDS_PER_SECTOR) {
                    *((uint32_t *) (p_sector_num + 1)) = EMPTY_JRECORD;        /*End*/
                }
                break;
            }

            *count = cpu_to_le32 (MIXED_JRECORDS_PER_SECTOR);
            memcpy (content, &s->table[chunk],
                    sizeof (uint32_t) * MIXED_JRECORDS_PER_SECTOR);

            /* A BITMAP_JRECORD follows a TABLE_JRECORD so that they are
             * updated in one atomic operatoin. */
            type = content + MIXED_JRECORDS_PER_SECTOR;                /* Field 1. */
            uint32_t *p_nb_sectors = type + 1;        /* BITMAP_JRECORD field 2. */
            int64_t *p_sector_num = (int64_t *) (type + 2);        /* Field 3. */
            *type = cpu_to_le32 (BITMAP_JRECORD);
            *p_nb_sectors = cpu_to_le32 (nb_sectors);
            *p_sector_num = cpu_to_le64 (sector_num);

            /* Prepare for the next journal sector. */
            type = (uint32_t *) (p_sector_num + 1);
            chunk += MIXED_JRECORDS_PER_SECTOR;
            sector_num = chunk * s->chunk_size;
            num_chunks -= MIXED_JRECORDS_PER_SECTOR;
            if (num_chunks <= MIXED_JRECORDS_PER_SECTOR) {
                /* Data sectors covered by the last journal sector. */
                nb_sectors = (acb->sector_num + acb->nb_sectors)
                                            - chunk * s->chunk_size;
            } else {
                nb_sectors = s->chunk_size * MIXED_JRECORDS_PER_SECTOR;
            }
        }
    }

    QDEBUG ("JOURNAL: acb%llu-%p  write_metadata_to_journal journal_sec=%"
            PRId64 " nb_journal_sectors=%d\n", acb->uuid, acb, journal_sec,
            num_journal_sectors);
    qemu_iovec_init_external (&acb->jcb.qiov, &acb->jcb.iov, 1);
    acb->jcb.hd_acb = bdrv_aio_writev (s->fvd_metadata,
                                       s->journal_offset + journal_sec,
                                       &acb->jcb.qiov, num_journal_sectors,
                                       finish_write_journal, acb);
    if (!acb->jcb.hd_acb) {
        finish_write_journal (acb, -1);
    }
}

#ifdef FVD_DEBUG
static int emulate_host_crash = TRUE;
#else
static int emulate_host_crash = FALSE;
#endif

static void flush_metadata_to_disk_on_exit (BlockDriverState *bs)
{
    BDRVFvdState *s = bs->opaque;

    if (bs->read_only || !s->fvd_metadata) {
        return;
    }

    /* If (emulate_host_crash==TRUE), do not flush metadata to disk
     * so that it has to rely on journal for recovery. */
    if (s->journal_size <= 0 || !emulate_host_crash) {
        flush_metadata_to_disk (bs);
        if (!s->dirty_image) {
            update_clean_shutdown_flag (s, TRUE);
        }
    }
}

void fvd_enable_host_crash_test (void)
{
    emulate_host_crash = TRUE;
}
