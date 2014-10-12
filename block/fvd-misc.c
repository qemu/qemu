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
 *  A short description: this module implements misc functions of the
 *  BlockDriver interface for the Fast Virtual Disk (FVD) format.
 *===========================================================================*/

static void fvd_flush_cancel (FvdAIOCB * acb)
{
    if (acb->flush.data_acb) {
        bdrv_aio_cancel (acb->flush.data_acb);
    }
    if (acb->flush.metadata_acb) {
        bdrv_aio_cancel (acb->flush.metadata_acb);
    }
    my_qemu_aio_release (acb);
}

static void fvd_aio_cancel (BlockDriverAIOCB * blockacb)
{
    FvdAIOCB *acb = container_of (blockacb, FvdAIOCB, common);

    QDEBUG ("CANCEL: acb%llu-%p\n", acb->uuid, acb);

    switch (acb->type) {
    case OP_READ:
        fvd_read_cancel (acb);
        break;

    case OP_WRITE:
        fvd_write_cancel (acb);
        break;

    case OP_COPY:
        fvd_copy_cancel (acb);
        break;

    case OP_LOAD_COMPACT:
        fvd_load_compact_cancel (acb);
        break;

    case OP_STORE_COMPACT:
        fvd_store_compact_cancel (acb);
        break;

    case OP_WRAPPER:
        fvd_wrapper_cancel (acb);
        break;

    case OP_FLUSH:
        fvd_flush_cancel (acb);
        break;
    }
}

static inline void finish_flush (FvdAIOCB * acb)
{
    QDEBUG ("FLUSH: acb%llu-%p  finish_flush ret=%d\n",
            acb->uuid, acb, acb->flush.ret);
    acb->common.cb (acb->common.opaque, acb->flush.ret);
    my_qemu_aio_release (acb);
}

static void finish_flush_data (void *opaque, int ret)
{
    FvdAIOCB *acb = opaque;

    QDEBUG ("FLUSH: acb%llu-%p  finish_flush_data ret=%d\n",
            acb->uuid, acb, ret);

    if (acb->flush.ret == 0) {
        acb->flush.ret = ret;
    }

    acb->flush.data_acb = NULL;
    acb->flush.num_finished++;
    if (acb->flush.num_finished == 2) {
        finish_flush (acb);
    }
}

static void finish_flush_metadata (void *opaque, int ret)
{
    FvdAIOCB *acb = opaque;

    QDEBUG ("FLUSH: acb%llu-%p  finish_flush_metadata ret=%d\n",
            acb->uuid, acb, ret);

    if (acb->flush.ret == 0) {
        acb->flush.ret = ret;
    }

    acb->flush.metadata_acb = NULL;
    acb->flush.num_finished++;
    if (acb->flush.num_finished == 2) {
        finish_flush (acb);
    }
}

static BlockDriverAIOCB *fvd_aio_flush (BlockDriverState * bs,
                                BlockDriverCompletionFunc * cb, void *opaque)
{
    BDRVFvdState *s = bs->opaque;
    if (s->fvd_data == s->fvd_metadata) {
        return bdrv_aio_flush (s->fvd_metadata, cb, opaque);
    }

    FvdAIOCB *acb = my_qemu_aio_get (&fvd_aio_pool, bs, cb, opaque);
    if (!acb) {
        return NULL;
    }

    acb->type = OP_FLUSH;
    acb->flush.num_finished = 0;
    acb->flush.ret = 0;
    acb->flush.data_acb = bdrv_aio_flush (s->fvd_data, finish_flush_data, acb);
    if (!acb->flush.data_acb) {
        my_qemu_aio_release (acb);
        return NULL;
    }

    acb->flush.metadata_acb = bdrv_aio_flush (s->fvd_metadata,
                                              finish_flush_metadata, acb);
    if (!acb->flush.metadata_acb) {
        bdrv_aio_cancel (acb->flush.data_acb);
        my_qemu_aio_release (acb);
        return NULL;
    }

    QDEBUG ("FLUSH: acb%llu-%p  start\n", acb->uuid, acb);
    return &acb->common;
}

static int fvd_flush (BlockDriverState * bs)
{
    BDRVFvdState *s = bs->opaque;
    int ret;

    QDEBUG ("fvd_flush() invoked\n");

    if (s->fvd_data) {
        if ((ret = bdrv_flush (s->fvd_data))) {
            return ret;
        }
    }
    if (s->fvd_metadata == s->fvd_data) {
        return 0;
    }

    return bdrv_flush (s->fvd_metadata);
}

static void fvd_close (BlockDriverState * bs)
{
    BDRVFvdState *s = bs->opaque;
    FvdAIOCB *acb;
    int i;

    if (s->prefetch_state == PREFETCH_STATE_RUNNING) {
        s->prefetch_state = PREFETCH_STATE_DISABLED;
    }
    if (s->prefetch_timer) {
        timer_del(s->prefetch_timer);
        timer_free(s->prefetch_timer);
        s->prefetch_timer = NULL;
    }

    /* Clean up prefetch operations. */
    if (s->prefetch_acb) {
        for (i = 0; i < s->num_prefetch_slots; i++) {
            if (s->prefetch_acb[i] != NULL) {
                acb = s->prefetch_acb[i];
                if (acb->copy.hd_acb) {
                    bdrv_aio_cancel (acb->copy.hd_acb);
                }
                my_qemu_vfree (s->prefetch_acb[i]->copy.buf);
                my_qemu_aio_release (s->prefetch_acb[i]);
                s->prefetch_acb[i] = NULL;
            }
        }
        my_qemu_free (s->prefetch_acb);
        s->prefetch_acb = NULL;
    }

    flush_metadata_to_disk_on_exit (bs);

    if (s->stale_bitmap) {
        my_qemu_vfree (s->stale_bitmap);
        if (s->fresh_bitmap != s->stale_bitmap) {
            my_qemu_vfree (s->fresh_bitmap);
        }
        s->stale_bitmap = NULL;
        s->fresh_bitmap = NULL;
    }

    if (s->table) {
        my_qemu_vfree (s->table);
        s->table = NULL;
    }

    if (s->fvd_metadata) {
        if (s->fvd_metadata != s->fvd_data) {
            bdrv_unref(s->fvd_metadata);
        }
        s->fvd_metadata = NULL;
    }
    if (s->fvd_data) {
        bdrv_unref(s->fvd_data);
        s->fvd_data = NULL;
    }

    if (s->add_storage_cmd) {
        my_qemu_free (s->add_storage_cmd);
        s->add_storage_cmd = NULL;
    }
#ifdef FVD_DEBUG
    dump_resource_summary (s);
#endif
}

static int fvd_probe (const uint8_t * buf, int buf_size, const char *filename)
{
    const FvdHeader *header = (const void *) buf;

    if (buf_size >= 2 * sizeof (uint32_t)
        && le32_to_cpu (header->magic) == FVD_MAGIC
        && le32_to_cpu (header->version) == FVD_VERSION) {
        return 100;
    } else {
        return 0;
    }
}

static int64_t fvd_get_block_status(BlockDriverState * bs, int64_t sector_num,
                                    int nb_sectors, int *pnum)
{
    BDRVFvdState *s = bs->opaque;

    if (s->prefetch_state == PREFETCH_STATE_FINISHED
        || sector_num >= s->nb_sectors_in_base_img
        || !fresh_bitmap_show_sector_in_base_img (sector_num, s)) {
        /* For the three cases that data may be saved in the FVD data file, we
         * still need to check the underlying storage because those data could
         * be holes in a sparse image, due to the optimization of "free write
         * to zero-filled blocks". See Section 3.3.3 of the FVD-cow paper.
         * This also covers the case of no base image. */

        if (!s->table) {
            return bdrv_is_allocated (s->fvd_data, s->data_offset + sector_num,
                                      nb_sectors, pnum);
        }

        /* Use the table to figure it out. */
        int64_t first_chunk = sector_num / s->chunk_size;
        int64_t last_chunk = (sector_num + nb_sectors - 1) / s->chunk_size;
        int allocated = !IS_EMPTY (s->table[first_chunk]);
        int count;

        if (first_chunk == last_chunk) {
            /* All data in one chunk. */
            *pnum = nb_sectors;
            return allocated;
        }

        /* Data in the first chunk. */
        count = s->chunk_size - (sector_num % s->chunk_size);

        /* Full chunks. */
        first_chunk++;
        while (first_chunk < last_chunk) {
            if ((allocated && IS_EMPTY (s->table[first_chunk]))
                || (!allocated && !IS_EMPTY (s->table[first_chunk]))) {
                *pnum = count;
                return allocated;
            }

            count += s->chunk_size;
            first_chunk++;
        }

        /* Data in the last chunk. */
        if ((allocated && !IS_EMPTY (s->table[last_chunk]))
            || (!allocated && IS_EMPTY (s->table[last_chunk]))) {
            int nb = (sector_num + nb_sectors) % s->chunk_size;
            count += nb ? nb : s->chunk_size;
        }

        *pnum = count;
        return allocated;
    }

    /* Use the FVD metadata to find out sectors in the base image. */
    int64_t end = sector_num + nb_sectors;
    if (end > s->nb_sectors_in_base_img) {
        end = s->nb_sectors_in_base_img;
    }

    int64_t next = sector_num + 1;
    while (next < end && fresh_bitmap_show_sector_in_base_img (next, s)) {
        next++;
    }

    *pnum = next - sector_num;
    return FALSE;
}

static void update_usage (void)
{
    printf ("Usage: update <image_file> [attribute=val]\n       See outputs of"
            "the 'info' command for all available attributes.\n");
}

static int fvd_get_info (BlockDriverState * bs, BlockDriverInfo * bdi)
{
    BDRVFvdState *s = bs->opaque;
    FvdHeader header;

    if (read_fvd_header (s, &header) < 0) {
        return -1;
    }

    printf ("========= Begin of FVD specific information ==================\n");
    printf ("magic\t\t\t\t\t\t%0X\n", header.magic);
    printf ("version\t\t\t\t\t\t%d\n", header.version);
    printf ("virtual_disk_size (bytes)\t\t\t%" PRId64 "\n",
            header.virtual_disk_size);
    printf ("disk_metadata_size (bytes)\t\t\t%" PRId64 "\n",
            header.metadata_size);
    if (header.data_file[0]) {
        printf ("data_file\t\t\t\t\t%s\n", header.data_file);
    }
    if (header.data_file_fmt[0]) {
        printf ("data_file_fmt\t\t\t\t%s\n", header.data_file_fmt);
    }

    if (header.base_img[0] != 0) {
        printf ("base_img\t\t\t\t\t%s\n", header.base_img);
        printf ("all_data_in_fvd_img\t\t\t\t%s\n",
                BOOL (header.all_data_in_fvd_img));
        printf ("base_img_size (bytes)\t\t\t\t%" PRId64 "\n",
                header.base_img_size);
        printf ("bitmap_offset (bytes)\t\t\t\t%" PRId64 "\n",
                header.bitmap_offset);
        printf ("bitmap_size (bytes)\t\t\t\t%" PRId64 "\n", header.bitmap_size);
        printf ("prefetch_profile_offset (bytes)\t\t\t%" PRId64 "\n",
                header.prefetch_profile_offset);
        printf ("prefetch_profile_entries\t\t\t%" PRId64 "\n",
                header.prefetch_profile_entries);
        printf ("prefetch_profile_entry_len_unit\t\t\t%d\n",
                header.unit_of_PrefetchProfileEntry_len);
        printf ("block_size\t\t\t\t\t%d\n", header.block_size);
        printf ("copy_on_read\t\t\t\t\t%s\n", BOOL (header.copy_on_read));
        printf ("max_outstanding_copy_on_read_data (bytes)\t%" PRId64 "\n",
                header.max_outstanding_copy_on_read_data);
        printf ("prefetch_start_delay (sec)\t\t\t%d\n",
                header.prefetch_start_delay);
        printf ("profile_directed_prefetch_start_delay (sec)\t%d\n",
                header.profile_directed_prefetch_start_delay);
        printf ("max_num_outstanding_prefetch_writes\t\t%d\n",
                header.num_prefetch_slots);
        printf ("bytes_per_prefetch\t\t\t\t%d\n", header.bytes_per_prefetch);
        printf ("prefetch_over_threshold_throttle_time (ms)\t%d\n",
                header.prefetch_throttle_time);
        printf ("prefetch_read_throughput_measure_time (ms)\t%d\n",
                header.prefetch_read_throughput_measure_time);
        printf ("prefetch_write_throughput_measure_time (ms)\t%d\n",
                header.prefetch_write_throughput_measure_time);
        printf ("prefetch_min_read_throughput_threshold (KB/s)\t%d\n",
                header.prefetch_min_read_throughput);
        printf ("prefetch_min_write_throughput_threshold (KB/s)\t%d\n",
                header.prefetch_min_write_throughput);
        printf ("prefetch_max_read_throughput_threshold (KB/s)\t%d\n",
                header.prefetch_max_read_throughput);
        printf ("prefetch_max_write_throughput_threshold (KB/s)\t%d\n",
                header.prefetch_max_write_throughput);
        printf ("prefetch_perf_calc_alpha\t\t\t%d\n",
                header.prefetch_perf_calc_alpha);
        printf ("generate_prefetch_profile\t\t\t%s\n",
                BOOL (header.generate_prefetch_profile));
    }

    printf ("need_zero_init\t\t\t\t\t%s\n", BOOL (header.need_zero_init));
    printf ("compact_image\t\t\t\t\t%s\n", BOOL (header.compact_image));
    if (header.compact_image) {
        printf ("data_storage (bytes)\t\t\t\t%" PRId64 "\n",
                s->data_storage * 512);
        printf ("chunk_size (bytes)\t\t\t\t%" PRId64 "\n", header.chunk_size);
        printf ("used_chunks (bytes)\t\t\t\t%" PRId64 "\n",
                s->used_storage * 512);
        printf ("storage_grow_unit (bytes)\t\t\t%" PRId64 "\n",
                header.storage_grow_unit);
        printf ("table_offset (bytes)\t\t\t\t%" PRId64 "\n",
                header.table_offset);
        int64_t vsize = ROUND_UP (s->virtual_disk_size, s->chunk_size * 512);
        int table_entries = vsize / (s->chunk_size * 512);
        int64_t table_size = sizeof (uint32_t) * table_entries;
        table_size = ROUND_UP (table_size, DEF_PAGE_SIZE);
        printf ("table_size (bytes)\t\t\t\t%" PRId64 "\n", table_size);

        if (header.add_storage_cmd[0] != 0) {
            printf ("add_storage_cmd\t\t\t\t\t%s\n", header.add_storage_cmd);
        }
    }
    printf ("clean_shutdown\t\t\t\t\t%s\n", BOOL (header.clean_shutdown));
    if (header.journal_size > 0) {
        printf ("journal_offset\t\t\t\t\t%" PRId64 "\n", header.journal_offset);
        printf ("journal_size\t\t\t\t\t%" PRId64 "\n", header.journal_size);
    }
    printf ("========= End of FVD specific information ====================\n");

    bdi->cluster_size = 0;
    bdi->vm_state_offset = 0;
    return 0;
}

static int fvd_has_zero_init (BlockDriverState * bs)
{
    BDRVFvdState *s = bs->opaque;
    return bdrv_has_zero_init (s->fvd_data);
}

static int fvd_update (BlockDriverState * bs, int argc, char **argv)
{
    BDRVFvdState *s = bs->opaque;
    FvdHeader header;
    int i;

    if (argc <= 0) {
        update_usage ();
        return -1;
    }

    if (strcmp (argv[0], "-h") == 0 || strcmp (argv[0], "--help") == 0
        || strcmp (argv[0], "-o") == 0) {
        update_usage ();
        return 0;
    }

    read_fvd_header (s, &header);

    for (i = 0; i < argc; i++) {
        char *attr = argv[i];
        char *val = strchr (attr, '=');
        if (val == NULL) {
            fprintf (stderr, "Error: string '%s' is not in the format of "
                     "'attribute=val' without spaces.\n", attr);
            return -1;
        }
        *val = 0;
        val++;

        if (strcmp (attr, "size") == 0) {
            int64_t new_size;
            new_size = atoll (val);
            int len = strlen (val);
            if (val[len - 1] == 'G') {
                new_size *= ((int64_t) 1024) * 1024 * 1024;
            } else if (val[len - 1] == 'M') {
                new_size *= ((int64_t) 1024) * 1024;
            } else if (val[len - 1] == 'K') {
                new_size *= ((int64_t) 1024);
            } else if (val[len - 1] == 'B') {
                /* No change to new_size as it is already in bytes. */
            } else {
                /* If no unit is specified, the default unit is KB. */
                new_size *= ((int64_t) 1024);
            }

            if (new_size <= 0) {
                fprintf (stderr, "Error: size %s is not positive.\n", val);
                return -1;
            }

            new_size = ROUND_UP (new_size, 512);
            if (new_size < header.virtual_disk_size) {
                printf ("Warning: image's new size %" PRId64
                        " is smaller than the original size %" PRId64
                        ". Some image data will be truncated.\n",
                        new_size, header.virtual_disk_size);
            }
            header.virtual_disk_size = new_size;
            printf ("Image resized to %" PRId64 " bytes.\n", new_size);
        } else if (strcmp (attr, "base_img") == 0) {
            if (strlen (val) > 1023) {
                fprintf (stderr, "Error: the new base image name is longer "
                         "than 1023, which is not allowed.\n");
                return -1;
            }

            memset (header.base_img, 0, 1024);
            pstrcpy (header.base_img, 1024, val);
            printf ("Backing file updated to '%s'.\n", val);
        } else if (strcmp (attr, "data_file") == 0) {
            if (strlen (val) > 1023) {
                fprintf (stderr, "Error: the new data file name is longer "
                         "than 1023, which is not allowed.\n");
                return -1;
            }

            memset (header.data_file, 0, 1024);
            pstrcpy (header.data_file, 1024, val);
            printf ("Data file updated to '%s'.\n", val);
        } else if (strcmp (attr, "need_zero_init") == 0) {
            if (strcasecmp (val, "true") == 0 || strcasecmp (val, "on") == 0) {
                header.need_zero_init = TRUE;
                printf ("need_zero_init is turned on for this disk.\n");
            } else {
                header.need_zero_init = FALSE;
                printf ("need_zero_init is turned off for this disk.\n");
            }
        } else if (strcmp (attr, "copy_on_read") == 0) {
            if (strcasecmp (val, "true") == 0 || strcasecmp (val, "on") == 0) {
                header.copy_on_read = TRUE;
                printf ("Copy on read is enabled for this disk.\n");
            } else {
                header.copy_on_read = FALSE;
                printf ("Copy on read is disabled for this disk.\n");
            }
        } else if (strcmp (attr, "clean_shutdown") == 0) {
            if (strcasecmp (val, "true") == 0 || strcasecmp (val, "on") == 0) {
                header.clean_shutdown = TRUE;
                printf ("clean_shutdown is manually set to true\n");
            } else {
                header.clean_shutdown = FALSE;
                printf ("clean_shutdown is manually set to false\n");
            }
        } else if (strcmp (attr, "max_outstanding_copy_on_read_data") == 0) {
            header.max_outstanding_copy_on_read_data = atoll (val);
            if (header.max_outstanding_copy_on_read_data <= 0) {
                fprintf (stderr, "Error: max_outstanding_copy_on_read_data "
                         "must be positive while the provided value is %"
                         PRId64 ".\n",
                         header.max_outstanding_copy_on_read_data);
                return -1;
            }
            printf ("max_outstanding_copy_on_read_data updated to %" PRId64
                    ".\n", header.max_outstanding_copy_on_read_data);
        } else if (strcmp (attr, "prefetch_start_delay") == 0) {
            header.prefetch_start_delay = atoi (val);
            if (header.prefetch_start_delay >= 0) {
                printf ("Prefetch starting delay updated to %d seconds.\n",
                        header.prefetch_start_delay);
            }
            else {
                printf ("Prefetch starting delay updated to %d seconds. "
                        "Because of the negative value, prefetching is "
                        "disabled for this image.\n",
                        header.prefetch_start_delay);
            }
        } else if (strcmp (attr, "max_num_outstanding_prefetch_writes") == 0) {
            header.num_prefetch_slots = atoi (val);
            if (header.num_prefetch_slots < 1) {
                fprintf (stderr, "Error: max_num_outstanding_prefetch_writes "
                         "%d is not a positive integer.\n",
                         header.num_prefetch_slots);
                return -1;
            }
            printf ("max_num_outstanding_prefetch_writes updated to %d.\n",
                    header.num_prefetch_slots);
        } else if (strcmp (attr, "bytes_per_prefetch") == 0) {
            header.bytes_per_prefetch = atoi (val);
            if (header.bytes_per_prefetch < DEF_PAGE_SIZE) {
                fprintf (stderr, "Error: bytes_per_prefetch cannot be smaller "
                         "than %d.\n", DEF_PAGE_SIZE);
                return -1;
            }
            printf ("bytes_per_prefetch updated to %d.\n",
                    header.bytes_per_prefetch);
        } else if (strcmp (attr, "prefetch_min_read_throughput_threshold")==0) {
            header.prefetch_min_read_throughput = atoi (val);
            printf ("prefetch_min_read_throughput_threshold updated to %d "
                    "KB/s\n", header.prefetch_min_read_throughput);
        } else if (strcmp (attr,"prefetch_min_write_throughput_threshold")==0) {
            header.prefetch_min_write_throughput = atoi (val);
            printf ("prefetch_min_write_throughput_threshold updated to %d "
                    "KB/s\n", header.prefetch_min_write_throughput);
        } else if (strcmp (attr, "prefetch_perf_calc_alpha") == 0) {
            header.prefetch_perf_calc_alpha = atoi (val);
            printf ("prefetch_perf_calc_alpha updated to %d\n",
                    header.prefetch_perf_calc_alpha);
        } else if (strcmp (attr, "prefetch_read_throughput_measure_time")==0) {
            header.prefetch_read_throughput_measure_time = atoi (val);
            printf ("prefetch_read_throughput_measure_time updated to %d ms\n",
                    header.prefetch_read_throughput_measure_time);
        } else if (strcmp (attr, "prefetch_write_throughput_measure_time")==0) {
            header.prefetch_write_throughput_measure_time = atoi (val);
            printf ("prefetch_write_throughput_measure_time updated to %d ms\n",
                    header.prefetch_write_throughput_measure_time);
        } else if (strcmp (attr, "prefetch_over_threshold_throttle_time")==0) {
            header.prefetch_throttle_time = atoi (val);
            if (header.prefetch_throttle_time > 0) {
                printf ("prefetch_over_threshold_throttle_time updated to %d "
                        "milliseconds.\n", header.prefetch_throttle_time);
            } else {
                printf ("prefetch_over_threshold_throttle_time updated to %d "
                        "milliseconds. It is not positive and hence no "
                        "throttling will be applied to prefetch.\n",
                        header.prefetch_throttle_time);
            }
        } else {
            fprintf (stderr, "Error: unknown setting '%s=%s'\n", attr, val);
            return -1;
        }
    }

    update_fvd_header (s, &header);
    return 0;
}
