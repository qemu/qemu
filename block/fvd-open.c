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
 *  A short description: this module implements bdrv_file_open() for FVD.
 *============================================================================*/

static void init_prefetch_timer (BlockDriverState * bs, BDRVFvdState * s);
static int init_data_file (BDRVFvdState * s, FvdHeader * header, int flags);
static int init_bitmap (BlockDriverState * bs, BDRVFvdState * s,
                        FvdHeader * header, const char *const filename);
static int load_table (BDRVFvdState * s, FvdHeader * header,
                       const char *const filename);
static int init_journal (int read_only, BlockDriverState * bs,
                         FvdHeader * header);
static int init_compact_image (BDRVFvdState * s, FvdHeader * header,
                               const char *const filename);

static QemuOptsList runtime_opts = {
    .name = "sim",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "File name of the image",
        },
        { /* end of list */ }
    },
};

static int fvd_open(BlockDriverState * bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVFvdState *s = bs->opaque;
    int ret;
    FvdHeader header;
    BlockDriver *drv;

    Error *local_err = NULL;
    const char *filename;

    QemuOpts *opts = qemu_opts_create_nofail(&runtime_opts);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (error_is_set(&local_err)) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -EINVAL;
    }

    filename = qemu_opt_get(opts, "filename");

    const char * protocol = strchr (filename, ':');
    if (protocol) {
        drv = bdrv_find_protocol (filename, true);
        filename = protocol + 1;
    }
    else {
        /* Use "raw" instead of "file" to allow storing the image on device. */
        drv = bdrv_find_format ("raw");
        if (!drv) {
            fprintf (stderr, "Failed to find the block device driver\n");
            return -EINVAL;
        }
    }

    s->fvd_metadata = bdrv_new ("");
    ret = bdrv_open(s->fvd_metadata, filename, NULL, flags, drv, &local_err);
    if (ret < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        return ret;
    }

    /* Initialize so that jumping to 'fail' would do cleanup properly. */
    s->stale_bitmap = NULL;
    s->fresh_bitmap = NULL;
    s->table = NULL;
    s->outstanding_copy_on_read_data = 0;
    QLIST_INIT (&s->write_locks);
    QLIST_INIT (&s->copy_locks);
    QLIST_INIT (&s->wait_for_journal);
    s->ongoing_journal_updates = 0;
    s->prefetch_acb = NULL;
    s->add_storage_cmd = NULL;
#ifdef FVD_DEBUG
    s->total_copy_on_read_data = s->total_prefetch_data = 0;
#endif

    if (bdrv_pread (s->fvd_metadata, 0, &header, sizeof (header)) !=
        sizeof (header)) {
        fprintf (stderr, "Failed to read the header of %s\n", filename);
        goto fail;
    }

    fvd_header_le_to_cpu (&header);

    if (header.magic != FVD_MAGIC || header.version != FVD_VERSION) {
        fprintf (stderr, "Incorrect magic number in the header of %s: "
                 "magic=%0X version=%d expect_magic=%0X expect_version=%d\n",
                 filename, header.magic, header.version, FVD_MAGIC,
                 FVD_VERSION);
        goto fail;
    }
    if (header.virtual_disk_size % 512 != 0) {
        fprintf (stderr, "Disk size %"PRId64" in the header of %s is not "
                 "a multple of 512.\n", header.virtual_disk_size, filename);
        goto fail;
    }

    /* Initialize the fields of BDRVFvdState. */
    s->dirty_image = FALSE;
    s->block_size = header.block_size / 512;
    s->bitmap_size = header.bitmap_size;
    s->prefetch_error = FALSE;
    s->prefetch_timer = NULL;
    s->sectors_per_prefetch = (header.bytes_per_prefetch + 511) / 512;
    s->prefetch_throttle_time = header.prefetch_throttle_time;
    s->prefetch_perf_calc_alpha = header.prefetch_perf_calc_alpha / 100.0;
    s->prefetch_read_throughput_measure_time =
                        header.prefetch_read_throughput_measure_time;
    s->prefetch_write_throughput_measure_time =
                        header.prefetch_write_throughput_measure_time;

    /* Convert KB/s to bytes/millisec. */
    s->prefetch_min_read_throughput =
            ((double) header.prefetch_min_read_throughput) * 1024.0 / 1000.0;
    s->prefetch_min_write_throughput =
            ((double) header.prefetch_min_write_throughput) * 1024.0 / 1000.0;

    if (header.base_img[0] != 0 && s->sectors_per_prefetch%s->block_size != 0) {
        fprintf (stderr, "sectors_per_prefetch (%d) is not a multiple of "
                 "block_size (%d)\n",
                 s->sectors_per_prefetch * 512, s->block_size * 512);
    }
    s->max_outstanding_copy_on_read_data =
        header.max_outstanding_copy_on_read_data;
    if (s->max_outstanding_copy_on_read_data < header.block_size * 2) {
        s->max_outstanding_copy_on_read_data = header.block_size;
    }

    if (header.num_prefetch_slots < 1) {
        s->num_prefetch_slots = 1;
    } else {
        s->num_prefetch_slots = header.num_prefetch_slots;
    }
    if (in_qemu_tool) {
        /* No prefetching in a qemu tool. */
        s->prefetch_start_delay = -1;

#ifndef SIMULATED_TEST_WITH_QEMU_IO
        s->copy_on_read = FALSE;        /* No prefetching in a qemu tool. */
#else
        /* But allow debugging copy_on_read in qemu-io if configured. */
        s->copy_on_read = header.copy_on_read;
#endif
    } else {
        s->prefetch_start_delay = header.prefetch_start_delay;
        s->copy_on_read = header.copy_on_read;
    }
    s->virtual_disk_size = header.virtual_disk_size;
    s->bitmap_offset = header.bitmap_offset / 512;
    s->nb_sectors_in_base_img = header.base_img_size / 512;
    bs->total_sectors = s->virtual_disk_size / 512;

    if (init_data_file (s, &header, flags)) {
        goto fail;
    }

    if (init_bitmap (bs, s, &header, filename)) {
        goto fail;
    }

    if (load_table (s, &header, filename)) {
        goto fail;
    }

    const int read_only = !(flags & BDRV_O_RDWR);
    if (init_journal (read_only, bs, &header)) {
        goto fail;
    }

    /* This must be done after init_journal() because it may use metadata
     * recovered from the journal. */
    if (init_compact_image (s, &header, filename)) {
        goto fail;
    }

    if (!read_only) {
        /* This flag will be cleaned later when the image is shut down
         * gracefully. */
        update_clean_shutdown_flag (s, FALSE);
    }
    init_prefetch_timer (bs, s);

    QDEBUG ("copy_on_read=%s block_size=%d journal_size=%" PRId64
            " prefetching_delay=%d prefetch_slots=%d "
            "prefetch_read_threshold_KB=%.0lf "
            "prefetch_write_threshold_KB=%.0lf "
            "prefetch_throttle_time=%d bytes_per_prefetch=%d "
            "max_outstanding_copy_on_read_data=%"PRId64"\n",
            BOOL (s->copy_on_read), s->block_size * 512,
            s->journal_size * 512, s->prefetch_start_delay,
            s->num_prefetch_slots,
            s->prefetch_min_read_throughput * 1000.0 / 1024.0,
            s->prefetch_min_write_throughput * 1000.0 / 1024.0,
            s->prefetch_throttle_time, s->sectors_per_prefetch * 512,
            s->max_outstanding_copy_on_read_data);

    return 0;

  fail:
    fprintf (stderr, "Failed to open %s using the FVD format.\n", filename);
    fvd_close (bs);
    return -1;
}

static int load_table (BDRVFvdState * s, FvdHeader * header,
                       const char *const filename)
{
    if (!header->compact_image) {
        return 0;
    }

    /* Initialize the table. */
    s->table_offset = header->table_offset / 512;
    s->chunk_size = header->chunk_size / 512;
    int64_t vsize = header->virtual_disk_size + header->chunk_size - 1;
    int table_entries = vsize / header->chunk_size;
    int64_t table_size = sizeof (uint32_t) * table_entries;
    table_size = ROUND_UP (table_size, DEF_PAGE_SIZE);
    s->table = my_qemu_blockalign (s->fvd_metadata, (size_t) table_size);

    if (bdrv_pread (s->fvd_metadata, header->table_offset, s->table, table_size)
        != table_size) {
        fprintf (stderr, "Failed to read the table of %s\n", filename);
        return -1;
    }

    return 0;
}

static int init_compact_image (BDRVFvdState * s, FvdHeader * header,
                               const char *const filename)
{
    if (!header->compact_image) {
        s->data_region_prepared = FALSE;
        return 0;
    }

    /* Scan the table to find the max allocated chunk. */
    int i;
    uint32_t max_chunk = 0;
    int empty_disk = TRUE;
    int table_entries =
        (int) (ROUND_UP (header->virtual_disk_size, header->chunk_size) /
               header->chunk_size);
    for (i = 0; i < table_entries; i++) {
        if (!IS_EMPTY (s->table[i])) {
            empty_disk = FALSE;
            uint32_t id = READ_TABLE (s->table[i]);
            if (id > max_chunk) {
                max_chunk = id;
            }
        }
    }
    if (!empty_disk) {
        max_chunk++;
    }
    s->used_storage = max_chunk * s->chunk_size;
    s->storage_grow_unit = header->storage_grow_unit / 512;

    /* Check if the image is directly stored on a raw device, including
     * logical volume. If so, figure out the size of the device. */
    struct stat stat_buf;
    if (stat (filename, &stat_buf) != 0) {
        fprintf (stderr, "Failed to stat() %s\n", filename);
        return -1;
    }

    /* Check how much storage space is already allocated. */
    int64_t size = bdrv_getlength (s->fvd_data);
    if (size < 0) {
        fprintf (stderr, "Failed in bdrv_getlength(%s)\n", filename);
        return -1;
    }
    const int64_t min_size = (s->data_offset + s->used_storage) * 512;
    if (size < min_size) {
        fprintf (stderr, "The size of device %s is not even big enough to "
                 "store already allocated data.\n",
                 filename);
        return -1;
    }

    if (S_ISBLK (stat_buf.st_mode) || S_ISCHR (stat_buf.st_mode)) {
        /* Initialize the command to grow storage space. */
        char cmd[2048];
        if (header->add_storage_cmd[0] == 0) {
            s->add_storage_cmd = NULL;
        } else {
            if (strcmp (header->add_storage_cmd, "builtin:lvextend") == 0) {
                /* Note the following:
                 *     1. lvextend may generate warning messages like "File
                 *     descriptor...leaked...", * which is fine.  See the
                 *     following from LVM manual: "On invocation, lvm requires
                 *     that only  the  standard  file  descriptors stdin,
                 *     stdout * and stderr are available.  If others are
                 *     found, they get closed and messages are issued warning
                 *     about the leak."
                 *     2. Instead of using the lvextend command line, one
                 *     option is to use liblvm directly, which avoids creating
                 *     a process to resize a LV.
                 *     3. On Ubuntu, /bin/sh is linked to /bin/dash, which
                 *     does not support ">&" for stdout and stderr
                 *     redirection. */
                snprintf (cmd, sizeof (cmd) - 1, "/sbin/lvextend -L+%" PRId64
                          "B %s >/dev/null 2>/dev/null",
                          header->storage_grow_unit,
                          header->data_file[0] ? header->data_file : filename);
            } else {
                snprintf (cmd, sizeof (cmd) - 1, "%s %" PRId64
                          " %s >/dev/null 2>/dev/null",
                          header->add_storage_cmd, header->storage_grow_unit,
                          header->data_file[0] ? header->data_file : filename);
            }

            int len = strlen (cmd);
            s->add_storage_cmd = my_qemu_malloc (len + 1);
            memcpy (s->add_storage_cmd, cmd, len + 1);
        }
    }

    s->data_storage = size / 512 - s->data_offset;
    s->fvd_data->growable = TRUE;
    s->data_region_prepared = TRUE;

    return 0;
}

static int init_data_file (BDRVFvdState * s, FvdHeader * header, int flags)
{
    Error *local_err = NULL;
    int ret;

    if (header->data_file[0]) {
        /* Open a separate data file. */
        s->data_offset = 0;
        s->fvd_data = bdrv_new ("");
        if (!s->fvd_data) {
            fprintf (stderr, "Failed to create a new block device driver.\n");
            return -1;
        }

        if (header->data_file_fmt[0] == 0) {
            ret = bdrv_open(s->fvd_data, header->data_file, NULL, flags, NULL,
                            &local_err);
        } else {
            BlockDriver *data_drv = bdrv_find_format (header->data_file_fmt);
            if (!data_drv) {
                fprintf (stderr, "Failed to find driver for image format "
                         "'%s' of data file %s\n",
                         header->data_file_fmt, header->data_file);
                return -1;
            }
            ret = bdrv_open(s->fvd_data, header->data_file,
                            NULL, flags, data_drv, &local_err);
        }
        if (ret != 0) {
            qerror_report_err(local_err);
            error_free(local_err);
            return -1;
        }
    } else {
        s->data_offset = header->metadata_size / 512;        /* In sectors. */
        s->fvd_data = s->fvd_metadata;
    }

    if (header->need_zero_init && !bdrv_has_zero_init (s->fvd_data)) {
        if (in_qemu_tool) {
            /* Only give a warning to allow 'qemu-img update' to modify
             * need_zero_init if the user manually zero-init the device. */
            fprintf (stderr, "Warning: image needs zero_init but it is not "
                     "supported by the storage media.\n");
        } else {
            fprintf (stderr, "Error: image needs zero_init but it is not "
                     "supported by the storage media.\n");
            return -EINVAL;
        }
    }

    return 0;
}

static int init_bitmap (BlockDriverState * bs, BDRVFvdState * s,
                        FvdHeader * header, const char *const filename)
{
    if (header->all_data_in_fvd_img) {
        /* This also covers the case of no base image. */
        s->prefetch_state = PREFETCH_STATE_FINISHED;
        s->copy_on_read = FALSE;
        s->prefetch_start_delay = -1;

        if (bs->backing_file[0] != 0) {
            /* No need to use the base image. It may operate without problem
             * even if the base image is no longer accessible. */
            bs->backing_file[0] = 0;
        }
    } else {
        ASSERT (header->base_img[0] != 0);
        pstrcpy (bs->backing_file, 1024, header->base_img);
        const int flags = O_RDONLY | O_BINARY | O_LARGEFILE;
        int test_backing_fd = open (bs->backing_file, flags);
        if (test_backing_fd < 0) {
            fprintf (stderr, "Failed to open the base image %s for read.\n",
                     bs->backing_file);
            return -1;
        }
        close (test_backing_fd);

        /* This will be enabled in init_prefetch() after a timer expires. */
        s->prefetch_state = PREFETCH_STATE_DISABLED;

        s->stale_bitmap = my_qemu_blockalign (s->fvd_metadata,
                                              (size_t) s->bitmap_size);
        if (bdrv_pread (s->fvd_metadata, header->bitmap_offset,
                        s->stale_bitmap, s->bitmap_size) != s->bitmap_size) {
            fprintf (stderr, "Failed to the bitmap of %s.\n", filename);
            return -1;
        }

        if (s->copy_on_read || (s->prefetch_state != PREFETCH_STATE_FINISHED &&
                                s->prefetch_start_delay > 0)) {
            /* Use two bitmaps only if copy_on_read or prefetching is enabled.
             * See Section 3.3.4 of the FVD-cow paper. */
            s->fresh_bitmap = my_qemu_blockalign (s->fvd_metadata,
                                                  s->bitmap_size);
            memcpy (s->fresh_bitmap, s->stale_bitmap, s->bitmap_size);
        } else {
            s->fresh_bitmap = s->stale_bitmap;
        }
    }

    return 0;
}

static void init_prefetch_timer (BlockDriverState * bs, BDRVFvdState * s)
{
#ifndef SIMULATED_TEST_WITH_QEMU_IO
    if (in_qemu_tool) {
        return;
    }
#endif

    if (s->prefetch_state == PREFETCH_STATE_FINISHED ||
        s->prefetch_start_delay <= 0) {
        return;
    }

    /* Start prefetching after a delay. Times 1000 to convert sec to ms. */
    int64_t expire = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + s->prefetch_start_delay * 1000;
    s->prefetch_timer = timer_new_ns(QEMU_CLOCK_REALTIME, fvd_init_prefetch, bs);
    timer_mod(s->prefetch_timer, expire);
}
