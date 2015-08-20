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
 *  A short description: this module implements bdrv_create() for FVD.
 *============================================================================*/

static inline int64_t calc_min_journal_size (int64_t table_entries);
static inline int search_holes(const char *filename, size_t bitmap_size,
                    int32_t bitmap_start_offset, BlockDriverState * bs,
                    int64_t nb_sectors, int32_t hole_size, int32_t block_size);

static int fvd_create(const char *filename, QEMUOptionParameter *options,
                      Error **errp)
{
    int fd, ret;
    FvdHeader *header;
    int64_t virtual_disk_size = DEF_PAGE_SIZE;
    int32_t header_size;
    const char *base_img = NULL;
    const char *base_img_fmt = NULL;
    const char *data_file = NULL;
    const char *data_file_fmt = NULL;
    int32_t hole_size = 0;
    int copy_on_read = FALSE;
    int prefetch_start_delay = -1;
    int64_t prefetch_profile_size = 0;
    BlockDriverState *bs = NULL;
    int bitmap_size = 0;
    int64_t base_img_size = 0;
    int64_t table_size = 0;
    int64_t journal_size = 0;
    int32_t block_size = 0;

    header_size = sizeof (FvdHeader);
    header_size = ROUND_UP (header_size, DEF_PAGE_SIZE);
    header = my_qemu_mallocz (header_size);

    /* Read out options */
    while (options && options->name) {
        if (!strcmp (options->name, BLOCK_OPT_SIZE)) {
            virtual_disk_size = options->value.n;
        } else if (!strcmp (options->name,"prefetch_start_delay")) {
            if (options->value.n <= 0) {
                prefetch_start_delay = -1;
            } else {
                prefetch_start_delay = options->value.n;
            }
        } else if (!strcmp (options->name, BLOCK_OPT_BACKING_FILE)) {
            base_img = options->value.s;
        } else if (!strcmp (options->name, BLOCK_OPT_BACKING_FMT)) {
            base_img_fmt = options->value.s;
        } else if (!strcmp (options->name, "copy_on_read")) {
            copy_on_read = options->value.n;
        } else if (!strcmp (options->name, "data_file")) {
            data_file = options->value.s;
        } else if (!strcmp (options->name, "data_file_fmt")) {
            data_file_fmt = options->value.s;
        } else if (!strcmp (options->name, "detect_sparse_hole")) {
            hole_size = options->value.n;
        } else if (!strcmp (options->name, "compact_image")) {
            header->compact_image = options->value.n;
        } else if (!strcmp (options->name, "block_size")) {
            block_size = options->value.n;
        } else if (!strcmp (options->name, "chunk_size")) {
            header->chunk_size = options->value.n;
        } else if (!strcmp (options->name, "journal_size")) {
            journal_size = options->value.n;
        } else if (!strcmp (options->name, "storage_grow_unit")) {
            header->storage_grow_unit = options->value.n;
        } else if (!strcmp (options->name, "add_storage_cmd")
                   && options->value.s) {
            pstrcpy (header->add_storage_cmd, sizeof (header->add_storage_cmd),
                     options->value.s);
        }
        options++;
    }

    virtual_disk_size = ROUND_UP (virtual_disk_size, 512);

    /* Check if arguments are valid. */
    if (base_img && strlen (base_img) > 1023) {
        fprintf (stderr, "The base image name is longer than 1023 characters, "
                 "which is not allowed.\n");
        return -EINVAL;
    }

    if (base_img && hole_size > 0) {
        if (header->compact_image) {
            fprintf (stderr, "compact_image and detect_sparse_hole cannot be "
                     "enabled together. Please disable detect_sparse_hole. \n");
            return -EINVAL;
        }
        header->need_zero_init = TRUE;
    } else {
        header->need_zero_init = FALSE;
    }

    if (data_file) {
        pstrcpy (header->data_file, 1024, data_file);
        if (data_file_fmt) {
            pstrcpy (header->data_file_fmt, 16, data_file_fmt);
        }
    }

    header->magic = FVD_MAGIC;
    header->version = FVD_VERSION;
    header->virtual_disk_size = virtual_disk_size;
    header->clean_shutdown = TRUE;

    if (!base_img) {
        header->all_data_in_fvd_img = TRUE;
    } else {
        Error *local_err = NULL;
        int ret;

        bs = bdrv_new ("");
        if (!bs) {
            fprintf (stderr, "Failed to create a new block driver\n");
            return -1;
        }

        pstrcpy (header->base_img, 1024, base_img);
        if (base_img_fmt) {
            pstrcpy (header->base_img_fmt, 16, base_img_fmt);
            BlockDriver *drv = bdrv_find_format (base_img_fmt);
            if (!drv) {
                fprintf (stderr, "Failed to find driver for format '%s'\n",
                         base_img_fmt);
                return -1;
            }
            ret = bdrv_open(bs, header->data_file, NULL, 0, drv, &local_err);
        } else {
            ret = bdrv_open(bs, base_img, NULL, 0, NULL, &local_err);
        }

        if (ret < 0) {
            qerror_report_err(local_err);
            error_free(local_err);
            return -1;
        }

        base_img_size = bdrv_getlength (bs);
        base_img_size = MIN (virtual_disk_size, base_img_size);
        base_img_size = ROUND_UP (base_img_size, 512);

        if (block_size <= 0) {
            /* No block size is provided. Find the smallest block size that
             * does not make the bitmap too big. */
            block_size = 512;
            while (1) {
                int64_t blocks = (base_img_size + block_size - 1) / block_size;
                bitmap_size = (blocks + 7) / 8;
                if (bitmap_size <= MODERATE_BITMAP_SIZE) {
                    break;
                }
                block_size *= 2;
            }
        } else {
            block_size = ROUND_UP (block_size, 512);
            int64_t blocks = (base_img_size + block_size - 1) / block_size;
            bitmap_size = (blocks + 7) / 8;
        }

        bitmap_size = ROUND_UP (bitmap_size, DEF_PAGE_SIZE);
        header->bitmap_size = bitmap_size;
        header->block_size = block_size;
        header->bitmap_offset = header_size;

        prefetch_profile_size = header->prefetch_profile_entries *
                                    sizeof (PrefetchProfileEntry);
        prefetch_profile_size = ROUND_UP (prefetch_profile_size, DEF_PAGE_SIZE);
        header->base_img_size = base_img_size;
        header->max_outstanding_copy_on_read_data =
                                    MAX_OUTSTANDING_COPY_ON_READ_DATA;
        header->copy_on_read = copy_on_read;
        header->prefetch_start_delay =
                                    prefetch_start_delay;
        header->num_prefetch_slots = NUM_PREFETCH_SLOTS;
        header->bytes_per_prefetch = ROUND_UP (BYTES_PER_PREFETCH, block_size);
        header->prefetch_throttle_time = PREFETCH_THROTTLING_TIME;
        header->prefetch_read_throughput_measure_time =
                                    PREFETCH_MIN_MEASURE_READ_TIME;
        header->prefetch_write_throughput_measure_time =
                                    PREFETCH_MIN_MEASURE_WRITE_TIME;
        header->prefetch_perf_calc_alpha = PREFETCH_PERF_CALC_ALPHA;
        header->prefetch_min_read_throughput = PREFETCH_MIN_READ_THROUGHPUT;
        header->prefetch_min_write_throughput = PREFETCH_MIN_WRITE_THROUGHPUT;
        header->prefetch_max_read_throughput = PREFETCH_MAX_READ_THROUGHPUT;
        header->prefetch_max_write_throughput = PREFETCH_MAX_WRITE_THROUGHPUT;
        header->all_data_in_fvd_img = FALSE;
        header->unit_of_PrefetchProfileEntry_len = DEF_PAGE_SIZE;
        header->generate_prefetch_profile = FALSE; /* To be implemented. */
        header->profile_directed_prefetch_start_delay = -1;/*To be implemented*/
    }

    /* Set the table size. */
    if (header->compact_image) {
        if (header->chunk_size <= 0) {
            header->chunk_size = CHUNK_SIZE;
        }
        header->chunk_size = ROUND_UP (header->chunk_size, DEF_PAGE_SIZE);
        if (header->storage_grow_unit <= 0) {
            header->storage_grow_unit = STORAGE_GROW_UNIT;
        }
        if (header->storage_grow_unit < header->chunk_size) {
            header->storage_grow_unit = header->chunk_size;
        }
        int64_t table_entries =
            (virtual_disk_size + header->chunk_size - 1) / header->chunk_size;
        table_size = sizeof (uint32_t) * table_entries;
        table_size = ROUND_UP (table_size, DEF_PAGE_SIZE);
        header->table_offset = header_size + bitmap_size;
    }

    /* Set the journal size. */
    if (bitmap_size <= 0 && table_size <= 0) {
        header->journal_size = 0;        /* No need to use journal. */
    } else if (journal_size < 0) {
        /* Disable the use of journal, which reduces overhead but may cause
         * data corruption if the host crashes. This is a valid configuration
         * for some use cases, where data integrity is not critical.  */
        header->journal_size = 0;
    } else {
        if (journal_size == 0) {
            /* No journal size is specified. Use a default size. */
            journal_size = JOURNAL_SIZE;
        }
        if (table_size > 0) {
            /* Make sure that the journal is at least large enough to record
             * all table changes in one shot, which is the extremely unlikely
             * worst case. */
            int64_t vsize = virtual_disk_size + header->chunk_size - 1;
            int64_t table_entries = vsize / header->chunk_size;
            int64_t min_journal_size = calc_min_journal_size (table_entries);
            if (journal_size < min_journal_size) {
                journal_size = min_journal_size;
            }
        }
        journal_size = ROUND_UP (journal_size, DEF_PAGE_SIZE);
        header->journal_size = journal_size;
        header->journal_offset = header_size + bitmap_size + table_size;
    }

    const int64_t metadata_size = header_size + bitmap_size + table_size +
                                prefetch_profile_size + MAX (0, journal_size);
    header->metadata_size = metadata_size;

    fd = open (filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf (stderr, "Failed to open %s\n", filename);
        goto fail;
    }
    fvd_header_cpu_to_le (header);

    if (qemu_write_full (fd, header, header_size) != header_size) {
        fprintf (stderr, "Failed to write the header of %s\n", filename);
        goto fail;
    }

    /* Initialize the bitmap. */
    if (bitmap_size > 0) {
        uint8_t *bitmap = my_qemu_mallocz (bitmap_size);
        ret = qemu_write_full (fd, bitmap, bitmap_size);
        my_qemu_free (bitmap);
        if (ret != bitmap_size) {
            fprintf (stderr, "Failed to zero out the bitmap of %s\n", filename);
            goto fail;
        }
    }

    /* Initialize the table. */
    if (table_size > 0) {
        /* Set all entries to EMPTY_TABLE (0xFFFFFFFF). */
        uint8_t *empty_table = my_qemu_malloc (table_size);
        memset (empty_table, 0xFF, table_size);
        ret = qemu_write_full (fd, empty_table, table_size);
        my_qemu_free (empty_table);
        if (ret != table_size) {
            fprintf (stderr, "Failed to write the table of %s\n.", filename);
            goto fail;
        }
    }

    /* Initialize the journal. */
    if (journal_size > 0) {
        uint8_t *empty_journal = my_qemu_mallocz (journal_size);
        ret = qemu_write_full (fd, empty_journal, journal_size);
        my_qemu_free (empty_journal);
        if (ret != journal_size) {
            fprintf (stderr, "Failed to initialize the journal for %s\n.",
                     filename);
            goto fail;
        }
    }

    close (fd);
    ret = 0;

    if (bs && hole_size > 0) {
        ret = search_holes (filename, (size_t) bitmap_size, header_size, bs,
                            base_img_size / 512, hole_size, block_size);
    }

    if (bs) {
        bdrv_close (bs);
    }
    my_qemu_free (header);
    return ret;

  fail:
    if (bs) {
        bdrv_close (bs);
    }
    close (fd);
    my_qemu_free (header);
    return -1;
}

/* For the optimization called "free write to zero-filled blocks". See Section
 * 3.3.3 of the FVD-cow paper. */
static inline int search_holes (const char *filename, size_t bitmap_size,
                                int32_t bitmap_start_offset,
                                BlockDriverState * bs, int64_t nb_sectors,
                                int32_t hole_size, int32_t block_size)
{
    const int fd = open (filename, O_RDWR | O_BINARY | O_LARGEFILE, 0);
    if (fd < 0) {
        fprintf (stderr, "Failed to open %s for read and write.\n", filename);
        return -1;
    }

    printf ("Searching zero-filled sectors in the base image. Please wait...");
    fflush (stdout);

    uint8_t *bitmap =
        (uint8_t *) mmap (NULL, bitmap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          fd, (off_t) bitmap_start_offset);
    if (bitmap == MAP_FAILED) {
        fprintf (stderr, "Failed to mmap() %s\n", filename);
        close (fd);
        return -1;
    }

    if (hole_size < block_size) {
        hole_size = block_size;
    }
    hole_size = ROUND_UP (hole_size, block_size);
    nb_sectors = ROUND_DOWN (nb_sectors, hole_size);
    const int sectors_per_hole = hole_size / 512;
    const int sectors_per_block = block_size / 512;
    int num_int64_in_hole = hole_size / 8;
    int64_t hole_count = 0;
    int i, ret = 0;
    int64_t sec = 0;
    uint8_t *p = my_qemu_blockalign (bs, hole_size);

    while (sec < nb_sectors) {
        int64_t *q;

        if (bdrv_read (bs, sec, p, sectors_per_hole) < 0) {
            fprintf (stderr, "Error in reading the base image\n");
            ret = -1;
            goto done;
        }

        /* All zeros? */
        q = (int64_t *) p;
        for (i = 0; i < num_int64_in_hole; i++) {
            if (*q != 0) {
                break;
            }
            q++;
        }

        if (i < num_int64_in_hole) {
            /* This is not a hole. */
            sec += sectors_per_hole;
        } else {
             /* These  sectors consist of only zeros.  Set the flag to
              * indicate that there is no need to read this sector from the
              * base image.  See Section 3.3.3 of the FVD-cow paper for the
              * rationale. */
            hole_count++;
            int64_t end = sec + sectors_per_hole;
            while (sec < end) {
                int block_num = sec / sectors_per_block;
                int64_t bitmap_byte_offset = block_num / 8;
                uint8_t bitmap_bit_offset = block_num % 8;
                int8_t mask = (uint8_t) (0x01 << bitmap_bit_offset);
                uint8_t b = bitmap[bitmap_byte_offset];
                if (!(b & mask)) {
                    b |= mask;
                    bitmap[bitmap_byte_offset] |= mask;
                }
                sec += sectors_per_block;
            }
        }
    }

  done:
    printf ("\nFound %" PRId64
            " zero-filled hole regions. Image creation done.\n", hole_count);
    my_qemu_vfree (p);
    munmap (bitmap, bitmap_size);
    close (fd);
    return ret;
}

static QEMUOptionParameter fvd_create_options[] = {
    {
     .name = BLOCK_OPT_SIZE,
     .type = OPT_SIZE,
     .help = "Virtual disk size"},
    {
     .name = "compact_image",
     .type = OPT_FLAG,
     .help = "compact_image=on|off"},
    {
     .name = "block_size",
     .type = OPT_SIZE,
     .help = "Block size"},
    {
     .name = "chunk_size",
     .type = OPT_SIZE,
     .help = "Chunk size"},
    {
     .name = "storage_grow_unit",
     .type = OPT_SIZE,
     .help = "Storage grow unit"},
    {
     .name = "add_storage_cmd",
     .type = OPT_STRING,
     .help = "Command to add storage when FSI runs out of space"},
    {
     .name = BLOCK_OPT_BACKING_FILE,
     .type = OPT_STRING,
     .help = "File name of a backing image"},
    {
     .name = BLOCK_OPT_BACKING_FMT,
     .type = OPT_STRING,
     .help = "Image format of the backing image"},
    {
     .name = "data_file",
     .type = OPT_STRING,
     .help = "File name of a separate data file"},
    {
     .name = "data_file_fmt",
     .type = OPT_STRING,
     .help = "Image format of the separate data file"},
    {
     .name = "copy_on_read",
     .type = OPT_FLAG,
     .help = "copy_on_read=on|off"},
    {
     .name = "prefetch_start_delay",
     .type = OPT_NUMBER,
     .help = "Delay in seconds before starting whole image prefetching. "
         "Prefetching is disabled if the delay is not a positive number."},
    {
     .name = "detect_sparse_hole",
     .type = OPT_SIZE,
     .help = "Minimum size (in bytes) of a continuous zero-filled region to be "
         "considered as a sparse file hole in the backing image (setting it "
         "to 0 turns off sparse file detection)"},
    {
     .name = "journal_size",
     .type = OPT_SIZE,
     .help = "Journal size"},
    {NULL}
};
