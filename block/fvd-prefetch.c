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
 *  A short description: this FVD module implements the function of
 *  prefetching data from the base image and storing it in the FVD image.
 *============================================================================*/

static void resume_prefetch (BlockDriverState * bs, int64_t current_time);
static void do_next_prefetch_read (BlockDriverState * bs, int64_t current_time);

void fvd_init_prefetch (void *opaque)
{
    BlockDriverState * bs = opaque;
    BDRVFvdState *s = bs->opaque;
    FvdAIOCB *acb;
    int i;

    QDEBUG ("Start prefetching\n");

    if (bdrv_find_format ("blksim") == NULL) {
        /* In simulation mode, the random seed should not be initialized here.*/
        srandom (time (NULL) + getpid () + getpid () * 987654 + random ());
    }

    s->prefetch_acb =
        my_qemu_malloc (sizeof (FvdAIOCB *) * s->num_prefetch_slots);

    for (i = 0; i < s->num_prefetch_slots; i++) {
        acb = s->prefetch_acb[i] =
            my_qemu_aio_get (&fvd_aio_pool, bs, null_prefetch_cb, NULL);

        if (!acb) {
            s->prefetch_error = TRUE;
            int j;
            for (j = 0; j < i; j++) {
                my_qemu_aio_release (s->prefetch_acb[j]);
                s->prefetch_acb[j] = NULL;
            }

            my_qemu_free (s->prefetch_acb);
            s->prefetch_acb = NULL;
            fprintf (stderr,
                     "qemu_aio_get() failed and cannot start prefetching.\n");
            return;
        }

        acb->type = OP_COPY;
    }

    s->prefetch_state = PREFETCH_STATE_RUNNING;

    for (i = 0; i < s->num_prefetch_slots; i++) {
        acb = s->prefetch_acb[i];
        acb->copy.buffered_sector_begin = acb->copy.buffered_sector_end = 0;
        QLIST_INIT (&acb->copy_lock.dependent_writes);
        acb->copy_lock.next.le_prev = NULL;
        acb->copy.hd_acb = NULL;
        acb->sector_num = 0;
        acb->nb_sectors = 0;
        acb->copy.iov.iov_len = s->sectors_per_prefetch * 512;
        acb->copy.buf = acb->copy.iov.iov_base =
            my_qemu_blockalign (bs->backing_hd, acb->copy.iov.iov_len);
        qemu_iovec_init_external (&acb->copy.qiov, &acb->copy.iov, 1);
    }

    if (s->prefetch_timer) {
        timer_free(s->prefetch_timer);
        s->prefetch_timer =
            timer_new_ns(QEMU_CLOCK_REALTIME, (QEMUTimerCB *) resume_prefetch, bs);
    }

    s->pause_prefetch_requested = FALSE;
    s->unclaimed_prefetch_region_start = 0;
    s->prefetch_read_throughput = -1;        /* Indicate not initialized. */
    s->prefetch_write_throughput = -1;        /* Indicate not initialized. */
    s->prefetch_read_time = 0;
    s->prefetch_write_time = 0;
    s->prefetch_data_read = 0;
    s->prefetch_data_written = 0;
    s->next_prefetch_read_slot = 0;
    s->num_filled_prefetch_slots = 0;
    s->prefetch_read_active = FALSE;

    do_next_prefetch_read (bs, qemu_clock_get_ns(QEMU_CLOCK_REALTIME));
}

static void pause_prefetch (BDRVFvdState * s)
{
    int64_t ms = 1 + (int64_t) ((random () / ((double) RAND_MAX))
                                * s->prefetch_throttle_time);
    QDEBUG ("Pause prefetch for %" PRId64 " milliseconds\n", ms);
    /* When the timer expires, it goes to resume_prefetch(). */
    timer_mod(s->prefetch_timer, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + ms);
}

static void terminate_prefetch (BlockDriverState * bs, int final_state)
{
    BDRVFvdState *s = bs->opaque;
    int i;

    ASSERT (!s->prefetch_read_active && s->num_filled_prefetch_slots == 0);

    for (i = 0; i < s->num_prefetch_slots; i++) {
        if (s->prefetch_acb) {
            my_qemu_vfree (s->prefetch_acb[i]->copy.buf);
            my_qemu_aio_release (s->prefetch_acb[i]);
            s->prefetch_acb[i] = NULL;
        }
    }
    my_qemu_free (s->prefetch_acb);
    s->prefetch_acb = NULL;

    if (s->prefetch_timer) {
        timer_del(s->prefetch_timer);
        timer_free(s->prefetch_timer);
        s->prefetch_timer = NULL;
    }

    if (final_state == PREFETCH_STATE_FINISHED) {
        if (s->prefetch_error) {
            s->prefetch_state = PREFETCH_STATE_DISABLED;
        } else {
            s->prefetch_state = PREFETCH_STATE_FINISHED;
        }
    } else {
        s->prefetch_state = final_state;
    }

    if (s->prefetch_state == PREFETCH_STATE_FINISHED) {
        QDEBUG ("FVD prefetching finished successfully.\n");

        if (s->stale_bitmap) {
            memset (s->stale_bitmap, 0xFF, s->bitmap_size);
            if (s->fresh_bitmap && s->fresh_bitmap != s->stale_bitmap) {
                memset (s->fresh_bitmap, 0xFF, s->bitmap_size);
            }
        }

        /* Flush the table since its entries may be dirty due to 'soft-write'
         * by prefetching or copy-on-read. */
        flush_metadata_to_disk (bs);

        /* Update the on-disk header. */
        FvdHeader header;
        read_fvd_header (s, &header);
        header.all_data_in_fvd_img = TRUE;
        update_fvd_header (s, &header);
        s->copy_on_read = FALSE;
    } else if (s->prefetch_state == PREFETCH_STATE_DISABLED) {
        QDEBUG ("FVD disk prefetching disabled.\n");
    }
}

static void do_next_prefetch_read (BlockDriverState * bs, int64_t current_time)
{
    FvdAIOCB *acb;
    BDRVFvdState *s = bs->opaque;
    int64_t begin, end;

    ASSERT (!s->prefetch_read_active
            && s->num_filled_prefetch_slots < s->num_prefetch_slots
            && !s->pause_prefetch_requested);

    /* Find the next region to prefetch. */
    begin = s->unclaimed_prefetch_region_start;
    while (1) {
        if (begin >= s->nb_sectors_in_base_img) {
            s->unclaimed_prefetch_region_start = s->nb_sectors_in_base_img;
            if (s->num_filled_prefetch_slots == 0) {
                terminate_prefetch (bs, PREFETCH_STATE_FINISHED);
            }
            return;
        }
        end = begin + s->sectors_per_prefetch;
        if (end > s->nb_sectors_in_base_img) {
            end = s->nb_sectors_in_base_img;
        }
        if (find_region_in_base_img (s, &begin, &end)) {
            break;
        }
        begin = end;
    }

    ASSERT (begin % s->block_size == 0
            && (end % s->block_size == 0 || end == s->nb_sectors_in_base_img));

    acb = s->prefetch_acb[s->next_prefetch_read_slot];
    acb->copy.buffered_sector_begin = acb->sector_num = begin;
    acb->copy.buffered_sector_end = s->unclaimed_prefetch_region_start = end;
    acb->nb_sectors = end - begin;
    acb->copy.qiov.size = acb->copy.iov.iov_len = acb->nb_sectors * 512;
    acb->copy.iov.iov_base = acb->copy.buf;
    acb->copy.last_prefetch_op_start_time = current_time;
    acb->copy.hd_acb = bdrv_aio_readv (bs->backing_hd, acb->sector_num,
                                       &acb->copy.qiov, acb->nb_sectors,
                                       finish_prefetch_read, acb);


    if (acb->copy.hd_acb == NULL) {
        QDEBUG ("PREFETCH: error when starting read for sector_num=%" PRId64
                " nb_sectors=%d\n", acb->sector_num, acb->nb_sectors);
        s->prefetch_error = TRUE;
        s->prefetch_state = PREFETCH_STATE_DISABLED;
        if (s->num_filled_prefetch_slots == 0) {
            terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
        }
    } else {
        s->prefetch_read_active = TRUE;
        QDEBUG ("PREFETCH: start read for sector_num=%" PRId64
                " nb_sectors=%d total_prefetched_bytes=%" PRId64 "\n",
                acb->sector_num, acb->nb_sectors, s->total_prefetch_data);
#ifdef FVD_DEBUG
    s->total_prefetch_data += acb->copy.iov.iov_len;
#endif
    }
}

static void finish_prefetch_write (void *opaque, int ret)
{
    FvdAIOCB *acb = (FvdAIOCB *) opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;
    int64_t begin, end;
    const int64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    ASSERT (acb->nb_sectors > 0 && s->num_filled_prefetch_slots > 0);

    QLIST_REMOVE (acb, copy_lock.next);
    restart_dependent_writes (acb);
    acb->copy.hd_acb = NULL;
    QLIST_INIT (&acb->copy_lock.dependent_writes);

    if (ret != 0) {
        QDEBUG ("PREFETCH: finished write with error for sector_num=%" PRId64
                " nb_sectors=%d\n", acb->sector_num, acb->nb_sectors);
        s->num_filled_prefetch_slots = 0;
        s->prefetch_error = TRUE;
        s->prefetch_state = PREFETCH_STATE_DISABLED;
        if (!s->prefetch_read_active) {
            terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
        }
        return;
    }

    /* No need to update the on-disk bitmap or the stale bitmap. See Section
     * 3.3.4 of the FVD-cow paper. */
    update_fresh_bitmap (acb->sector_num, acb->nb_sectors, s);

    const int64_t write_time =
        current_time - acb->copy.last_prefetch_op_start_time;
    s->prefetch_write_time += write_time;
    s->prefetch_data_written += acb->nb_sectors * 512;

    QDEBUG ("PREFETCH: write_finished  sector_num=%" PRId64
            " nb_sectors=%d  write_time=%d (ms)\n", acb->sector_num,
            acb->nb_sectors, (int) write_time);

    /* Calculate throughput and determine if it needs to pause prefetching due
     * to low throughput. */
    if (s->prefetch_timer && s->prefetch_throttle_time > 0
        && !s->pause_prefetch_requested
        && s->prefetch_write_time > s->prefetch_write_throughput_measure_time) {
        const double this_round_throughput =
            s->prefetch_data_written / (double) s->prefetch_write_time;
        if (s->prefetch_write_throughput < 0) {
            /* Previously not initialized. */
            s->prefetch_write_throughput = this_round_throughput;
        } else {
            s->prefetch_write_throughput =
                s->prefetch_perf_calc_alpha * s->prefetch_write_throughput +
                (1 - s->prefetch_perf_calc_alpha) * this_round_throughput;
        }
        if (s->prefetch_write_throughput < s->prefetch_min_write_throughput) {
            QDEBUG ("PREFETCH: slow_write  this_write=%d (ms)  "
                    "this_write_throughput=%.3lf (MB/s)   "
                    "avg_write_throughput=%.3lf (MB/s)\n",
                    (int) write_time,
                    this_round_throughput / 1048576 * 1000,
                    s->prefetch_write_throughput / 1048576 * 1000);

            /* Make a randomized decision to pause prefetching. This avoids
             * pausing all contending FVD drivers. See Section 3.4.2 of the
             * FVD-cow paper. */
            if (random () > (RAND_MAX / 2)) {
                QDEBUG ("PREFETCH: pause requested.\n");
                s->pause_prefetch_requested = TRUE;
            } else {
                QDEBUG ("PREFETCH: continue due to 50%% probability, despite "
                        "slow write.\n");
                s->prefetch_write_throughput = -1; /*Indicate not initialized.*/
            }
        } else {
            QDEBUG ("PREFETCH: this_write_throughput=%.3lf (MB/s)   "
                    "avg_write_throughput=%.3lf (MB/s)\n",
                    this_round_throughput / 1048576 * 1000,
                    s->prefetch_write_throughput / 1048576 * 1000);
        }

        /* Preparing for measuring the next round of throughput. */
        s->prefetch_data_written = 0;
        s->prefetch_write_time = 0;
    }

    /* Find in this prefetch slot the next section of prefetched but
     * not-yet-written data. */
    begin = acb->sector_num + acb->nb_sectors;
    if (begin < acb->copy.buffered_sector_end) {
        end = acb->copy.buffered_sector_end;
        if (find_region_in_base_img (s, &begin, &end)) {
            acb->sector_num = begin;
            acb->nb_sectors = end - begin;
            acb->copy.iov.iov_base = acb->copy.buf +
                            (begin - acb->copy.buffered_sector_begin) * 512;
            acb->copy.qiov.size = acb->copy.iov.iov_len = acb->nb_sectors * 512;
            QDEBUG ("PREFETCH: write_data  sector_num=%" PRId64
                    " nb_sectors=%d\n", acb->sector_num, acb->nb_sectors);
            acb->copy.hd_acb = store_data (TRUE, acb, bs, acb->sector_num,
                                           &acb->copy.qiov, acb->nb_sectors,
                                           finish_prefetch_write, acb);
            if (acb->copy.hd_acb == NULL) {
                QDEBUG ("PREFETCH: error in starting bdrv_aio_writev().\n");
                s->num_filled_prefetch_slots = 0;
                s->prefetch_error = TRUE;
                s->prefetch_state = PREFETCH_STATE_DISABLED;
                if (!s->prefetch_read_active) {
                    terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
                }
            } else {
                acb->copy_lock.begin = begin;
                acb->copy_lock.end = end;
                QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            }

            return;
        }
    }

    s->num_filled_prefetch_slots--;

    if (s->prefetch_state == PREFETCH_STATE_DISABLED) {
        if (s->num_filled_prefetch_slots == 0 && !s->prefetch_read_active) {
            terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
        }
        return;
    }

    if (begin >= s->nb_sectors_in_base_img) {
        /* Prefetching finished. */
        ASSERT (s->num_filled_prefetch_slots == 0 && !s->prefetch_read_active);
        terminate_prefetch (bs, PREFETCH_STATE_FINISHED);
        return;
    }

    if (s->pause_prefetch_requested) {
        if (s->num_filled_prefetch_slots == 0) {
            if (!s->prefetch_read_active) {
                pause_prefetch (s);
            } else {
                QDEBUG ("PREFETCH: wait for the read operation to finish in "
                        "order to pause prefetch.\n");
            }
            return;
        }
    }

    /* Write out data in the next prefetched slot. */
    while (s->num_filled_prefetch_slots > 0) {
        int k = s->next_prefetch_read_slot - s->num_filled_prefetch_slots;
        if (k < 0) {
            k += s->num_prefetch_slots;
        }
        acb = s->prefetch_acb[k];

        int64_t begin = acb->copy.buffered_sector_begin;
        int64_t end = acb->copy.buffered_sector_end;
        if (find_region_in_base_img (s, &begin, &end)) {
            acb->copy.last_prefetch_op_start_time = current_time;
            acb->sector_num = begin;
            acb->nb_sectors = end - begin;
            acb->copy.iov.iov_base =
                acb->copy.buf + (begin - acb->copy.buffered_sector_begin) * 512;
            acb->copy.qiov.size = acb->copy.iov.iov_len = acb->nb_sectors * 512;
            QDEBUG ("PREFETCH: writes data: sector_num=%" PRId64
                    " nb_sectors=%d\n", acb->sector_num, acb->nb_sectors);
            acb->copy.hd_acb = store_data (TRUE, acb, bs, acb->sector_num,
                                           &acb->copy.qiov, acb->nb_sectors,
                                           finish_prefetch_write, acb);

            if (acb->copy.hd_acb == NULL) {
                QDEBUG ("PREFETCH: error cannot get a control block to write "
                        "a prefetched block.\n");
                s->prefetch_error = TRUE;
                s->prefetch_state = PREFETCH_STATE_DISABLED;
                s->num_filled_prefetch_slots = 0;
                if (!s->prefetch_read_active) {
                    terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
                }
                return;
            }

            acb->copy_lock.begin = begin;
            acb->copy_lock.end = end;
            QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            break;
        } else {
            QDEBUG ("PREFETCH: discard prefetched data as they have been "
                    "covered: sector_num=%" PRId64 " nb_sectors=%d\n",
                    acb->sector_num, acb->nb_sectors);
            s->num_filled_prefetch_slots--;
        }
    }

    /* If the reader was stopped due to lack of slots, start the reader. */
    if (!s->prefetch_read_active && !s->pause_prefetch_requested) {
        do_next_prefetch_read (bs, current_time);
    }
}

static void finish_prefetch_read (void *opaque, int ret)
{
    FvdAIOCB *acb = (FvdAIOCB *) opaque;
    BlockDriverState *bs = acb->common.bs;
    BDRVFvdState *s = bs->opaque;

    ASSERT (s->prefetch_read_active && s->num_filled_prefetch_slots >= 0
            && s->num_filled_prefetch_slots < s->num_prefetch_slots);

    s->prefetch_read_active = FALSE;
    acb->copy.hd_acb = NULL;

    if (s->prefetch_state == PREFETCH_STATE_DISABLED) {
        if (s->num_filled_prefetch_slots == 0) {
            terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
        }
        return;
    }

    if (ret != 0) {
        QDEBUG ("PREFETCH: read_error  sector_num=%" PRId64 " nb_sectors=%d.\n",
                acb->sector_num, acb->nb_sectors);
        s->prefetch_error = TRUE;
        s->prefetch_state = PREFETCH_STATE_DISABLED;
        if (s->num_filled_prefetch_slots == 0) {
            terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
        }
        return;
    }

    const int64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    const int64_t read_time = current_time -
                        acb->copy.last_prefetch_op_start_time;
    s->prefetch_read_time += read_time;
    s->prefetch_data_read += acb->nb_sectors * 512;

    QDEBUG ("PREFETCH: read_finished  sector_num=%" PRId64
            " nb_sectors=%d  read_time=%d (ms)\n", acb->sector_num,
            acb->nb_sectors, (int) read_time);

    /* Calculate throughput and determine if it needs to pause prefetching due
     * to low throughput. */
    if (s->prefetch_timer && s->prefetch_throttle_time > 0
        && !s->pause_prefetch_requested
        && s->prefetch_read_time > s->prefetch_read_throughput_measure_time) {
        const double this_round_throughput =
            s->prefetch_data_read / (double) s->prefetch_read_time;
        if (s->prefetch_read_throughput < 0) {
            /* Previously not initialized. */
            s->prefetch_read_throughput = this_round_throughput;
        } else {
            s->prefetch_read_throughput = s->prefetch_perf_calc_alpha *
                s->prefetch_read_throughput +
                (1 - s->prefetch_perf_calc_alpha) * this_round_throughput;
        }
        if (s->prefetch_read_throughput < s->prefetch_min_read_throughput) {
            QDEBUG ("PREFETCH: slow_read read_time=%d (ms)   "
                    "this_read_throughput=%.3lf (MB/s) "
                    "avg_read_throughput=%.3lf (MB/s)\n",
                    (int) read_time, this_round_throughput / 1048576 * 1000,
                    s->prefetch_read_throughput / 1048576 * 1000);

            /* Make a randomized decision to pause prefetching. This avoids
             * pausing all contending FVD drivers. See Section 3.4.2 of the
             * FVD-cow paper. */
            if (random () > (RAND_MAX / 2)) {
                QDEBUG ("PREFETCH: pause requested.\n");
                s->pause_prefetch_requested = TRUE;
            } else {
                QDEBUG ("PREFETCH: continue due to 50%% probability, "
                        "despite slow read.\n");
                s->prefetch_read_throughput = -1; /*Indicate not initialized.*/
            }
        } else {
            QDEBUG ("PREFETCH: this_read_throughput=%.3lf (MB/s)    "
                    "avg_read_throughput=%.3lf (MB/s)\n",
                    this_round_throughput / 1048576 * 1000,
                    s->prefetch_read_throughput / 1048576 * 1000);
        }

        /* Preparing for measuring the next round of throughput. */
        s->prefetch_data_read = 0;
        s->prefetch_read_time = 0;
    }

    if (s->num_filled_prefetch_slots > 0) {
        /* There is one ongoing write for prefetched data. This slot will be
         * written out later. */
        s->num_filled_prefetch_slots++;
        s->next_prefetch_read_slot++;
        if (s->next_prefetch_read_slot >= s->num_prefetch_slots) {
            s->next_prefetch_read_slot = 0;
        }
    } else {
        /* The writer is not active. Start the writer. */
        int64_t begin = acb->copy.buffered_sector_begin;
        int64_t end = acb->copy.buffered_sector_end;
        if (find_region_in_base_img (s, &begin, &end)) {
            acb->copy.last_prefetch_op_start_time = current_time;
            acb->sector_num = begin;
            acb->nb_sectors = end - begin;
            acb->copy.iov.iov_base =
                acb->copy.buf + (begin - acb->copy.buffered_sector_begin) * 512;
            acb->copy.qiov.size = acb->copy.iov.iov_len = acb->nb_sectors * 512;
            QDEBUG ("PREFETCH: writes_data sector_num=%" PRId64
                    " nb_sectors=%d\n", acb->sector_num, acb->nb_sectors);
            acb->copy.hd_acb = store_data (TRUE, acb, bs, acb->sector_num,
                                           &acb->copy.qiov, acb->nb_sectors,
                                           finish_prefetch_write, acb);

            if (acb->copy.hd_acb == NULL) {
                QDEBUG ("PREFETCH: error cannot get control block to write a "
                        "prefetched block.\n");
                s->prefetch_error = TRUE;
                s->prefetch_state = PREFETCH_STATE_DISABLED;
                if (s->num_filled_prefetch_slots == 0) {
                    terminate_prefetch (bs, PREFETCH_STATE_DISABLED);
                }
                return;
            }

            acb->copy_lock.begin = begin;
            acb->copy_lock.end = end;
            QLIST_INSERT_HEAD (&s->copy_locks, acb, copy_lock.next);
            s->num_filled_prefetch_slots++;
            s->next_prefetch_read_slot++;
            if (s->next_prefetch_read_slot >= s->num_prefetch_slots) {
                s->next_prefetch_read_slot = 0;
            }
        } else {
            /* The current prefetch slot will be reused to prefetch the next
             * bunch of data. */
            QDEBUG ("PREFETCH: discard prefetched data as they have been "
                    "covered: sector_num=%" PRId64 " nb_sectors=%d\n",
                    acb->sector_num, acb->nb_sectors);
        }
    }

    if (s->num_filled_prefetch_slots >= s->num_prefetch_slots) {
        QDEBUG ("PREFETCH: halt read because no slot is available.\n");
    } else {
        if (s->pause_prefetch_requested) {
            if (s->num_filled_prefetch_slots == 0) {
                pause_prefetch (s);
            }
        } else {
            do_next_prefetch_read (bs, current_time);
        }
    }
}

static void resume_prefetch (BlockDriverState * bs, int64_t current_time)
{
    BDRVFvdState *s = bs->opaque;

    if (s->prefetch_state != PREFETCH_STATE_RUNNING) {
        return;
    }

    ASSERT (s->num_filled_prefetch_slots == 0 && !s->prefetch_read_active);
    QDEBUG ("PREFETCH: resume.\n");

    s->pause_prefetch_requested = FALSE;
    s->prefetch_read_throughput = -1;        /* Indicate not initialized. */
    s->prefetch_write_throughput = -1;        /* Indicate not initialized. */
    s->prefetch_read_time = 0;
    s->prefetch_write_time = 0;
    s->prefetch_data_read = 0;
    s->prefetch_data_written = 0;

    do_next_prefetch_read (bs, qemu_clock_get_ns(QEMU_CLOCK_REALTIME));
}
