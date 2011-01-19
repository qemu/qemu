/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*==============================================================================
 *  A short description: this module implements basic utility functions for
 *  the Fast Virtual Disk (FVD) format.
 *============================================================================*/

static inline int stale_bitmap_show_sector_in_base_img (int64_t sector_num,
                                                const BDRVFvdState * s)
{
    if (sector_num >= s->nb_sectors_in_base_img) {
        return FALSE;
    }

    int64_t block_num = sector_num / s->block_size;
    int64_t bitmap_byte_offset = block_num / 8;
    uint8_t bitmap_bit_offset = block_num % 8;
    uint8_t b = s->stale_bitmap[bitmap_byte_offset];
    return 0 == (int) ((b >> bitmap_bit_offset) & 0x01);
}

static inline int
fresh_bitmap_show_sector_in_base_img (int64_t sector_num,
                                              const BDRVFvdState * s)
{
    if (sector_num >= s->nb_sectors_in_base_img) {
        return FALSE;
    }

    int64_t block_num = sector_num / s->block_size;
    int64_t bitmap_byte_offset = block_num / 8;
    uint8_t bitmap_bit_offset = block_num % 8;
    uint8_t b = s->fresh_bitmap[bitmap_byte_offset];
    return 0 == (int) ((b >> bitmap_bit_offset) & 0x01);
}

static inline void update_fresh_bitmap (int64_t sector_num, int nb_sectors,
                                           const BDRVFvdState * s)
{
    if (sector_num >= s->nb_sectors_in_base_img) {
        return;
    }

    int64_t end = sector_num + nb_sectors;
    if (end > s->nb_sectors_in_base_img) {
        end = s->nb_sectors_in_base_img;
    }

    int64_t block_num = sector_num / s->block_size;
    int64_t block_end = (end - 1) / s->block_size;

    for (; block_num <= block_end; block_num++) {
        int64_t bitmap_byte_offset = block_num / 8;
        uint8_t bitmap_bit_offset = block_num % 8;
        uint8_t mask = (uint8_t) (0x01 << bitmap_bit_offset);
        uint8_t b = s->fresh_bitmap[bitmap_byte_offset];
        if (!(b & mask)) {
            b |= mask;
            s->fresh_bitmap[bitmap_byte_offset] = b;
        }
    }
}

static void update_stale_bitmap (BDRVFvdState * s, int64_t sector_num,
                                 int nb_sectors)
{
    if (sector_num >= s->nb_sectors_in_base_img) {
        return;
    }

    int64_t end = sector_num + nb_sectors;
    if (end > s->nb_sectors_in_base_img) {
        end = s->nb_sectors_in_base_img;
    }

    int64_t block_num = sector_num / s->block_size;
    const int64_t block_end = (end - 1) / s->block_size;

    for (; block_num <= block_end; block_num++) {
        int64_t bitmap_byte_offset = block_num / 8;
        uint8_t bitmap_bit_offset = block_num % 8;
        uint8_t mask = (uint8_t) (0x01 << bitmap_bit_offset);
        uint8_t b = s->stale_bitmap[bitmap_byte_offset];
        if (!(b & mask)) {
            ASSERT (s->stale_bitmap == s->fresh_bitmap ||
                    (s->fresh_bitmap[bitmap_byte_offset] & mask));
            b |= mask;
            s->stale_bitmap[bitmap_byte_offset] = b;
        }
    }
}

static void update_both_bitmaps (BDRVFvdState * s, int64_t sector_num,
                                 int nb_sectors)
{
    if (sector_num >= s->nb_sectors_in_base_img) {
        return;
    }

    int64_t end = sector_num + nb_sectors;
    if (end > s->nb_sectors_in_base_img) {
        end = s->nb_sectors_in_base_img;
    }

    int64_t block_num = sector_num / s->block_size;
    const int64_t block_end = (end - 1) / s->block_size;

    for (; block_num <= block_end; block_num++) {
        int64_t bitmap_byte_offset = block_num / 8;
        uint8_t bitmap_bit_offset = block_num % 8;
        uint8_t mask = (uint8_t) (0x01 << bitmap_bit_offset);
        uint8_t b = s->fresh_bitmap[bitmap_byte_offset];
        if (!(b & mask)) {
            b |= mask;
            s->fresh_bitmap[bitmap_byte_offset] =
                s->stale_bitmap[bitmap_byte_offset] = b;
        }
    }
}

/* Return TRUE if a valid region is found. */
static int find_region_in_base_img (BDRVFvdState * s, int64_t * from,
                                    int64_t * to)
{
    int64_t sec = *from;
    int64_t last_sec = *to;

    if (last_sec > s->nb_sectors_in_base_img) {
        last_sec = s->nb_sectors_in_base_img;
    }

    if (sec >= last_sec) {
        return FALSE;
    }

    if (!fresh_bitmap_show_sector_in_base_img (sec, s)) {
        /* Find the first sector in the base image. */

        sec = ROUND_UP (sec + 1, s->block_size); /* Begin of next block. */
        while (1) {
            if (sec >= last_sec) {
                return FALSE;
            }
            if (fresh_bitmap_show_sector_in_base_img (sec, s)) {
                break;
            }
            sec += s->block_size;        /* Begin of the next block. */
        }
    }

    /* Find the end of the region in the base image. */
    int64_t first_sec = sec;
    sec = ROUND_UP (sec + 1, s->block_size);        /* Begin of next block. */
    while (1) {
        if (sec >= last_sec) {
            sec = last_sec;
            break;
        }
        if (!fresh_bitmap_show_sector_in_base_img (sec, s)) {
            break;
        }
        sec += s->block_size;        /* Begin of the next block. */
    }
    last_sec = sec;

    /* Check conflicting copy-on-reads. */
    FvdAIOCB *old;
    QLIST_FOREACH (old, &s->copy_locks, copy_lock.next) {
        if (old->copy_lock.begin <= first_sec
                && first_sec < old->copy_lock.end) {
            first_sec = old->copy_lock.end;
        }
        if (old->copy_lock.begin < last_sec && last_sec <= old->copy_lock.end) {
            last_sec = old->copy_lock.begin;
        }
    }

    if (first_sec >= last_sec) {
        return FALSE;        /* The entire region is already covered. */
    }

     /* This loop cannot be merged with the loop above. Otherwise, the logic
      * would be incorrect.  This loop covers the case that an old request
      * spans over a subset of the region being checked. */
    QLIST_FOREACH (old, &s->copy_locks, copy_lock.next) {
        if (first_sec <= old->copy_lock.begin
            && old->copy_lock.begin < last_sec) {
            last_sec = old->copy_lock.begin;
        }
    }

    /* Check conflicting writes. */
    QLIST_FOREACH (old, &s->write_locks, write.next_write_lock) {
        int64_t old_end = old->sector_num + old->nb_sectors;
        if (old->sector_num <= first_sec && first_sec < old_end) {
            first_sec = old_end;
        }
        if (old->sector_num < last_sec && last_sec <= old_end) {
            last_sec = old->sector_num;
        }
    }

    if (first_sec >= last_sec) {
        return FALSE;        /* The entire region is already covered. */
    }

     /* This loop cannot be merged with the loop above. Otherwise, the logic
      * would be incorrect.  This loop covers the case that an old request
      * spans over a subset of the region being checked. */
    QLIST_FOREACH (old, &s->write_locks, write.next_write_lock) {
        if (first_sec <= old->sector_num && old->sector_num < last_sec) {
            last_sec = old->sector_num;
        }
    }

    ASSERT (first_sec % s->block_size == 0 && (last_sec % s->block_size == 0
                || last_sec == s->nb_sectors_in_base_img));

    *from = first_sec;
    *to = last_sec;
    return TRUE;
}

static inline int bitmap_show_sector_in_base_img (int64_t sector_num,
                                                       const BDRVFvdState * s,
                                                       int bitmap_offset,
                                                       uint8_t * bitmap)
{
    if (sector_num >= s->nb_sectors_in_base_img) {
        return FALSE;
    }

    int64_t block_num = sector_num / s->block_size;
    int64_t bitmap_byte_offset = block_num / 8 - bitmap_offset;
    uint8_t bitmap_bit_offset = block_num % 8;
    uint8_t b = bitmap[bitmap_byte_offset];
    return 0 == (int) ((b >> bitmap_bit_offset) & 0x01);
}

static inline void copy_to_iov (struct iovec *iov, int *p_index,
                                uint8_t ** p_buf, int *p_left,
                                uint8_t * source, int total)
{
    int index = *p_index;
    uint8_t *buf = *p_buf;
    int left = *p_left;

    if (left <= 0) {
        index++;
        buf = iov[index].iov_base;
        left = iov[index].iov_len;
    }

    while (1) {
        if (left >= total) {
            memcpy (buf, source, total);
            *p_buf = buf + total;
            *p_left = left - total;
            *p_index = index;
            return;
        }

        memcpy (buf, source, left);
        total -= left;
        source += left;
        index++;
        buf = iov[index].iov_base;
        left = iov[index].iov_len;
    }
}

static inline void init_data_region (BDRVFvdState * s)
{
    bdrv_truncate (s->fvd_data, s->data_offset * 512 + s->virtual_disk_size);
    s->data_region_prepared = TRUE;
}

static inline void update_clean_shutdown_flag (BDRVFvdState * s, int clean)
{
    FvdHeader header;
    if (!read_fvd_header (s, &header)) {
        header.clean_shutdown = clean;

        if (!update_fvd_header (s, &header)) {
            QDEBUG ("Set clean_shutdown to %s\n", BOOL (clean));
        }
    }
}

static inline int stale_bitmap_need_update (FvdAIOCB * acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;
    int64_t end = acb->sector_num + acb->nb_sectors;

    if (end > s->nb_sectors_in_base_img) {
        end = s->nb_sectors_in_base_img;
    }
    int64_t block_end = (end - 1) / s->block_size;
    int64_t block_num = acb->sector_num / s->block_size;

    for (; block_num <= block_end; block_num++) {
        int64_t bitmap_byte_offset = block_num / 8;
        uint8_t bitmap_bit_offset = block_num % 8;
        uint8_t mask = (uint8_t) (0x01 << bitmap_bit_offset);
        uint8_t b = s->stale_bitmap[bitmap_byte_offset];
        if (!(b & mask)) {
            return TRUE;
        }
    }

    return FALSE;
}

static int update_fresh_bitmap_and_check_stale_bitmap (FvdAIOCB * acb)
{
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    if (acb->sector_num >= s->nb_sectors_in_base_img) {
        return FALSE;
    }

    int need_update = FALSE;
    int64_t end = acb->sector_num + acb->nb_sectors;

    if (end > s->nb_sectors_in_base_img) {
        end = s->nb_sectors_in_base_img;
    }

    int64_t block_end = (end - 1) / s->block_size;
    int64_t block_num = acb->sector_num / s->block_size;

    for (; block_num <= block_end; block_num++) {
        int64_t bitmap_byte_offset = block_num / 8;
        uint8_t bitmap_bit_offset = block_num % 8;
        uint8_t mask = (uint8_t) (0x01 << bitmap_bit_offset);
        uint8_t b = s->stale_bitmap[bitmap_byte_offset];
        if (b & mask) {
            /* If the bit in stale_bitmap is set, the corresponding bit in
             * fresh_bitmap must be set already. */
            continue;
        }

        need_update = TRUE;
        b = s->fresh_bitmap[bitmap_byte_offset];
        if (!(b & mask)) {
            b |= mask;
            s->fresh_bitmap[bitmap_byte_offset] = b;
        }
    }

    return need_update;
}

static void fvd_header_cpu_to_le (FvdHeader * header)
{
    cpu_to_le32s (&header->magic);
    cpu_to_le32s (&header->version);
    cpu_to_le32s ((uint32_t *) & header->all_data_in_fvd_img);
    cpu_to_le32s ((uint32_t *) & header->generate_prefetch_profile);
    cpu_to_le64s ((uint64_t *) & header->metadata_size);
    cpu_to_le64s ((uint64_t *) & header->virtual_disk_size);
    cpu_to_le64s ((uint64_t *) & header->base_img_size);
    cpu_to_le64s ((uint64_t *) & header->max_outstanding_copy_on_read_data);
    cpu_to_le64s ((uint64_t *) & header->bitmap_offset);
    cpu_to_le64s ((uint64_t *) & header->prefetch_profile_offset);
    cpu_to_le64s ((uint64_t *) & header->prefetch_profile_entries);
    cpu_to_le64s ((uint64_t *) & header->bitmap_size);
    cpu_to_le32s ((uint32_t *) & header->copy_on_read);
    cpu_to_le32s ((uint32_t *) & header->need_zero_init);
    cpu_to_le32s ((uint32_t *) & header->prefetch_start_delay);
    cpu_to_le32s ((uint32_t *) & header->profile_directed_prefetch_start_delay);
    cpu_to_le32s ((uint32_t *) & header->num_prefetch_slots);
    cpu_to_le32s ((uint32_t *) & header->bytes_per_prefetch);
    cpu_to_le32s ((uint32_t *) & header->prefetch_throttle_time);
    cpu_to_le32s ((uint32_t *) & header->prefetch_read_throughput_measure_time);
    cpu_to_le32s ((uint32_t *) &header->prefetch_write_throughput_measure_time);
    cpu_to_le32s ((uint32_t *) & header->prefetch_perf_calc_alpha);
    cpu_to_le32s ((uint32_t *) & header->prefetch_min_read_throughput);
    cpu_to_le32s ((uint32_t *) & header->prefetch_min_write_throughput);
    cpu_to_le32s ((uint32_t *) & header->prefetch_max_read_throughput);
    cpu_to_le32s ((uint32_t *) & header->prefetch_max_write_throughput);
    cpu_to_le32s ((uint32_t *) & header->block_size);
    cpu_to_le32s ((uint32_t *) & header->unit_of_PrefetchProfileEntry_len);
    cpu_to_le32s ((uint32_t *) & header->compact_image);
    cpu_to_le64s ((uint64_t *) & header->chunk_size);
    cpu_to_le64s ((uint64_t *) & header->storage_grow_unit);
    cpu_to_le64s ((uint64_t *) & header->table_offset);
    cpu_to_le32s ((uint32_t *) & header->clean_shutdown);
    cpu_to_le64s ((uint64_t *) & header->journal_offset);
    cpu_to_le64s ((uint64_t *) & header->journal_size);
}

static void fvd_header_le_to_cpu (FvdHeader * header)
{
    le32_to_cpus (&header->magic);
    le32_to_cpus (&header->version);
    le32_to_cpus ((uint32_t *) & header->all_data_in_fvd_img);
    le32_to_cpus ((uint32_t *) & header->generate_prefetch_profile);
    le64_to_cpus ((uint64_t *) & header->metadata_size);
    le64_to_cpus ((uint64_t *) & header->virtual_disk_size);
    le64_to_cpus ((uint64_t *) & header->base_img_size);
    le64_to_cpus ((uint64_t *) & header->max_outstanding_copy_on_read_data);
    le64_to_cpus ((uint64_t *) & header->bitmap_offset);
    le64_to_cpus ((uint64_t *) & header->prefetch_profile_offset);
    le64_to_cpus ((uint64_t *) & header->prefetch_profile_entries);
    le64_to_cpus ((uint64_t *) & header->bitmap_size);
    le32_to_cpus ((uint32_t *) & header->copy_on_read);
    le32_to_cpus ((uint32_t *) & header->need_zero_init);
    le32_to_cpus ((uint32_t *) & header->prefetch_start_delay);
    le32_to_cpus ((uint32_t *) & header->profile_directed_prefetch_start_delay);
    le32_to_cpus ((uint32_t *) & header->num_prefetch_slots);
    le32_to_cpus ((uint32_t *) & header->bytes_per_prefetch);
    le32_to_cpus ((uint32_t *) & header->prefetch_throttle_time);
    le32_to_cpus ((uint32_t *) & header->prefetch_read_throughput_measure_time);
    le32_to_cpus ((uint32_t *) &header->prefetch_write_throughput_measure_time);
    le32_to_cpus ((uint32_t *) & header->prefetch_perf_calc_alpha);
    le32_to_cpus ((uint32_t *) & header->prefetch_min_read_throughput);
    le32_to_cpus ((uint32_t *) & header->prefetch_min_write_throughput);
    le32_to_cpus ((uint32_t *) & header->prefetch_max_read_throughput);
    le32_to_cpus ((uint32_t *) & header->prefetch_max_write_throughput);
    le32_to_cpus ((uint32_t *) & header->block_size);
    le32_to_cpus ((uint32_t *) & header->unit_of_PrefetchProfileEntry_len);
    le32_to_cpus ((uint32_t *) & header->compact_image);
    le64_to_cpus ((uint64_t *) & header->chunk_size);
    le64_to_cpus ((uint64_t *) & header->storage_grow_unit);
    le64_to_cpus ((uint64_t *) & header->table_offset);
    le32_to_cpus ((uint32_t *) & header->clean_shutdown);
    le64_to_cpus ((uint64_t *) & header->journal_offset);
    le64_to_cpus ((uint64_t *) & header->journal_size);
}

static void flush_metadata_to_disk (BlockDriverState * bs)
{
    BDRVFvdState *s = bs->opaque;

    if (bs->read_only || !s->fvd_metadata) {
        return;
    }

    if (s->stale_bitmap) {
        /* Flush fresh_bitmap to disk. */
        int nb = (int) (s->bitmap_size / 512);
        QDEBUG ("Flush FVD bitmap (%d sectors) to disk\n", nb);
        bdrv_write (s->fvd_metadata, s->bitmap_offset, s->fresh_bitmap, nb);
    }

    if (s->table) {
        /* Flush table to disk. */
        int table_entries =
            (int) (ROUND_UP (s->virtual_disk_size, s->chunk_size * 512) /
                   (s->chunk_size * 512));

        /* Clean the DIRTY_TABLE bit. */
        int i;
        for (i = 0; i < table_entries; i++) {
            CLEAN_DIRTY (s->table[i]);
        }

        int64_t table_size = sizeof (uint32_t) * table_entries;
        table_size = ROUND_UP (table_size, DEF_PAGE_SIZE);
        int nb = (int) (table_size / 512);
        QDEBUG ("Flush FVD table (%d sectors) to disk\n", nb);
        bdrv_write (s->fvd_metadata, s->table_offset, (uint8_t *) s->table, nb);
    }
}

static int read_fvd_header (BDRVFvdState * s, FvdHeader * header)
{
    if (bdrv_pread (s->fvd_metadata, 0, header, sizeof (FvdHeader)) !=
        sizeof (FvdHeader)) {
        fprintf (stderr, "Failed to read the FVD header.\n");
        return -1;
    }

    fvd_header_le_to_cpu (header);

    if (header->magic != FVD_MAGIC || header->version != FVD_VERSION) {
        fprintf (stderr, "Error: image does not have the correct FVD format "
                 "magic number in header\n");
        return -1;
    }

    return 0;
}

static int update_fvd_header (BDRVFvdState * s, FvdHeader * header)
{
    fvd_header_cpu_to_le (header);
    int ret = bdrv_pwrite (s->fvd_metadata, 0, header, sizeof (FvdHeader));

    if (ret != sizeof (FvdHeader)) {
        fprintf (stderr, "Failed to update the FVD header.\n");
        ASSERT (FALSE);
        return -EIO;
    }

    return 0;
}

static void null_prefetch_cb (void *opaque, int ret)
{
    /* Nothing to do and will never be invoked. Only need it to distinguish
     * copy-on-read from prefetch. */
    ASSERT (FALSE);
}

static int count_iov (struct iovec *orig_iov, int *p_index, uint8_t ** p_buf,
                      size_t * p_left, size_t total)
{
    int index = *p_index;
    uint8_t *buf = *p_buf;
    int left = *p_left;
    int count = 0;

    if (left <= 0) {
        index++;
        buf = orig_iov[index].iov_base;
        left = orig_iov[index].iov_len;
    }

    while (1) {
        if (left >= total) {
            *p_buf = buf + total;
            *p_left = left - total;
            *p_index = index;
            return count + 1;
        }

        total -= left;
        index++;
        buf = orig_iov[index].iov_base;
        left = orig_iov[index].iov_len;
        count++;
    }
}

static int setup_iov (struct iovec *orig_iov, struct iovec *new_iov,
                      int *p_index, uint8_t ** p_buf, size_t * p_left,
                      size_t total)
{
    int index = *p_index;
    uint8_t *buf = *p_buf;
    int left = *p_left;
    int count = 0;

    if (left <= 0) {
        index++;
        buf = orig_iov[index].iov_base;
        left = orig_iov[index].iov_len;
    }

    while (1) {
        if (left >= total) {
            new_iov[count].iov_base = buf;
            new_iov[count].iov_len = total;
            *p_buf = buf + total;
            *p_left = left - total;
            *p_index = index;
            return count + 1;
        }

        new_iov[count].iov_base = buf;
        new_iov[count].iov_len = left;
        total -= left;
        index++;
        buf = orig_iov[index].iov_base;
        left = orig_iov[index].iov_len;
        count++;
    }
}

static int zero_iov (struct iovec *orig_iov, int *p_index, uint8_t ** p_buf,
                     size_t * p_left, size_t total)
{
    int index = *p_index;
    uint8_t *buf = *p_buf;
    int left = *p_left;
    int count = 0;

    if (left <= 0) {
        index++;
        buf = orig_iov[index].iov_base;
        left = orig_iov[index].iov_len;
    }

    while (1) {
        if (left >= total) {
            memset (buf, 0, total);
            *p_buf = buf + total;
            *p_left = left - total;
            *p_index = index;
            return count + 1;
        }

        memset (buf, 0, left);
        total -= left;
        index++;
        buf = orig_iov[index].iov_base;
        left = orig_iov[index].iov_len;
        count++;
    }
}
