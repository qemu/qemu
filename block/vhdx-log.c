/*
 * Block driver for Hyper-V VHDX Images
 *
 * Copyright (c) 2013 Red Hat, Inc.,
 *
 * Authors:
 *  Jeff Cody <jcody@redhat.com>
 *
 *  This is based on the "VHDX Format Specification v1.00", published 8/25/2012
 *  by Microsoft:
 *      https://www.microsoft.com/en-us/download/details.aspx?id=34750
 *
 * This file covers the functionality of the metadata log writing, parsing, and
 * replay.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#include "qemu-common.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "block/vhdx.h"


typedef struct VHDXLogSequence {
    bool valid;
    uint32_t count;
    VHDXLogEntries log;
    VHDXLogEntryHeader hdr;
} VHDXLogSequence;

typedef struct VHDXLogDescEntries {
    VHDXLogEntryHeader hdr;
    VHDXLogDescriptor desc[];
} VHDXLogDescEntries;

static const MSGUID zero_guid = { 0 };

/* The log located on the disk is circular buffer containing
 * sectors of 4096 bytes each.
 *
 * It is assumed for the read/write functions below that the
 * circular buffer scheme uses a 'one sector open' to indicate
 * the buffer is full.  Given the validation methods used for each
 * sector, this method should be compatible with other methods that
 * do not waste a sector.
 */


/* Allow peeking at the hdr entry at the beginning of the current
 * read index, without advancing the read index */
static int vhdx_log_peek_hdr(BlockDriverState *bs, VHDXLogEntries *log,
                             VHDXLogEntryHeader *hdr)
{
    int ret = 0;
    uint64_t offset;
    uint32_t read;

    assert(hdr != NULL);

    /* peek is only supported on sector boundaries */
    if (log->read % VHDX_LOG_SECTOR_SIZE) {
        ret = -EFAULT;
        goto exit;
    }

    read = log->read;
    /* we are guaranteed that a) log sectors are 4096 bytes,
     * and b) the log length is a multiple of 1MB. So, there
     * is always a round number of sectors in the buffer */
    if ((read + sizeof(VHDXLogEntryHeader)) > log->length) {
        read = 0;
    }

    if (read == log->write) {
        ret = -EINVAL;
        goto exit;
    }

    offset = log->offset + read;

    ret = bdrv_pread(bs->file, offset, hdr, sizeof(VHDXLogEntryHeader));
    if (ret < 0) {
        goto exit;
    }

exit:
    return ret;
}

/* Index increment for log, based on sector boundaries */
static int vhdx_log_inc_idx(uint32_t idx, uint64_t length)
{
    idx += VHDX_LOG_SECTOR_SIZE;
    /* we are guaranteed that a) log sectors are 4096 bytes,
     * and b) the log length is a multiple of 1MB. So, there
     * is always a round number of sectors in the buffer */
    return idx >= length ? 0 : idx;
}


/* Reset the log to empty */
static void vhdx_log_reset(BlockDriverState *bs, BDRVVHDXState *s)
{
    MSGUID guid = { 0 };
    s->log.read = s->log.write = 0;
    /* a log guid of 0 indicates an empty log to any parser of v0
     * VHDX logs */
    vhdx_update_headers(bs, s, false, &guid);
}

/* Reads num_sectors from the log (all log sectors are 4096 bytes),
 * into buffer 'buffer'.  Upon return, *sectors_read will contain
 * the number of sectors successfully read.
 *
 * It is assumed that 'buffer' is already allocated, and of sufficient
 * size (i.e. >= 4096*num_sectors).
 *
 * If 'peek' is true, then the tail (read) pointer for the circular buffer is
 * not modified.
 *
 * 0 is returned on success, -errno otherwise.  */
static int vhdx_log_read_sectors(BlockDriverState *bs, VHDXLogEntries *log,
                                 uint32_t *sectors_read, void *buffer,
                                 uint32_t num_sectors, bool peek)
{
    int ret = 0;
    uint64_t offset;
    uint32_t read;

    read = log->read;

    *sectors_read = 0;
    while (num_sectors) {
        if (read == log->write) {
            /* empty */
            break;
        }
        offset = log->offset + read;

        ret = bdrv_pread(bs->file, offset, buffer, VHDX_LOG_SECTOR_SIZE);
        if (ret < 0) {
            goto exit;
        }
        read = vhdx_log_inc_idx(read, log->length);

        *sectors_read = *sectors_read + 1;
        num_sectors--;
    }

exit:
    if (!peek) {
        log->read = read;
    }
    return ret;
}

/* Validates a log entry header */
static bool vhdx_log_hdr_is_valid(VHDXLogEntries *log, VHDXLogEntryHeader *hdr,
                                  BDRVVHDXState *s)
{
    int valid = false;

    if (memcmp(&hdr->signature, "loge", 4)) {
        goto exit;
    }

    /* if the individual entry length is larger than the whole log
     * buffer, that is obviously invalid */
    if (log->length < hdr->entry_length) {
        goto exit;
    }

    /* length of entire entry must be in units of 4KB (log sector size) */
    if (hdr->entry_length % (VHDX_LOG_SECTOR_SIZE)) {
        goto exit;
    }

    /* per spec, sequence # must be > 0 */
    if (hdr->sequence_number == 0) {
        goto exit;
    }

    /* log entries are only valid if they match the file-wide log guid
     * found in the active header */
    if (!guid_eq(hdr->log_guid, s->headers[s->curr_header]->log_guid)) {
        goto exit;
    }

    if (hdr->descriptor_count * sizeof(VHDXLogDescriptor) > hdr->entry_length) {
        goto exit;
    }

    valid = true;

exit:
    return valid;
}

/*
 * Given a log header, this will validate that the descriptors and the
 * corresponding data sectors (if applicable)
 *
 * Validation consists of:
 *      1. Making sure the sequence numbers matches the entry header
 *      2. Verifying a valid signature ('zero' or 'desc' for descriptors)
 *      3. File offset field is a multiple of 4KB
 *      4. If a data descriptor, the corresponding data sector
 *         has its signature ('data') and matching sequence number
 *
 * @desc: the data buffer containing the descriptor
 * @hdr:  the log entry header
 *
 * Returns true if valid
 */
static bool vhdx_log_desc_is_valid(VHDXLogDescriptor *desc,
                                   VHDXLogEntryHeader *hdr)
{
    bool ret = false;

    if (desc->sequence_number != hdr->sequence_number) {
        goto exit;
    }
    if (desc->file_offset % VHDX_LOG_SECTOR_SIZE) {
        goto exit;
    }

    if (!memcmp(&desc->signature, "zero", 4)) {
        if (desc->zero_length % VHDX_LOG_SECTOR_SIZE == 0) {
            /* valid */
            ret = true;
        }
    } else if (!memcmp(&desc->signature, "desc", 4)) {
            /* valid */
            ret = true;
    }

exit:
    return ret;
}


/* Prior to sector data for a log entry, there is the header
 * and the descriptors referenced in the header:
 *
 * [] = 4KB sector
 *
 * [ hdr, desc ][   desc   ][ ... ][ data ][ ... ]
 *
 * The first sector in a log entry has a 64 byte header, and
 * up to 126 32-byte descriptors.  If more descriptors than
 * 126 are required, then subsequent sectors can have up to 128
 * descriptors.  Each sector is 4KB.  Data follows the descriptor
 * sectors.
 *
 * This will return the number of sectors needed to encompass
 * the passed number of descriptors in desc_cnt.
 *
 * This will never return 0, even if desc_cnt is 0.
 */
static int vhdx_compute_desc_sectors(uint32_t desc_cnt)
{
    uint32_t desc_sectors;

    desc_cnt += 2; /* account for header in first sector */
    desc_sectors = desc_cnt / 128;
    if (desc_cnt % 128) {
        desc_sectors++;
    }

    return desc_sectors;
}


/* Reads the log header, and subsequent descriptors (if any).  This
 * will allocate all the space for buffer, which must be NULL when
 * passed into this function. Each descriptor will also be validated,
 * and error returned if any are invalid. */
static int vhdx_log_read_desc(BlockDriverState *bs, BDRVVHDXState *s,
                              VHDXLogEntries *log, VHDXLogDescEntries **buffer)
{
    int ret = 0;
    uint32_t desc_sectors;
    uint32_t sectors_read;
    VHDXLogEntryHeader hdr;
    VHDXLogDescEntries *desc_entries = NULL;
    int i;

    assert(*buffer == NULL);

    ret = vhdx_log_peek_hdr(bs, log, &hdr);
    if (ret < 0) {
        goto exit;
    }
    vhdx_log_entry_hdr_le_import(&hdr);
    if (vhdx_log_hdr_is_valid(log, &hdr, s) == false) {
        ret = -EINVAL;
        goto exit;
    }

    desc_sectors = vhdx_compute_desc_sectors(hdr.descriptor_count);
    desc_entries = qemu_blockalign(bs, desc_sectors * VHDX_LOG_SECTOR_SIZE);

    ret = vhdx_log_read_sectors(bs, log, &sectors_read, desc_entries,
                                desc_sectors, false);
    if (ret < 0) {
        goto free_and_exit;
    }
    if (sectors_read != desc_sectors) {
        ret = -EINVAL;
        goto free_and_exit;
    }

    /* put in proper endianness, and validate each desc */
    for (i = 0; i < hdr.descriptor_count; i++) {
        vhdx_log_desc_le_import(&desc_entries->desc[i]);
        if (vhdx_log_desc_is_valid(&desc_entries->desc[i], &hdr) == false) {
            ret = -EINVAL;
            goto free_and_exit;
        }
    }

    *buffer = desc_entries;
    goto exit;

free_and_exit:
    qemu_vfree(desc_entries);
exit:
    return ret;
}


/* Flushes the descriptor described by desc to the VHDX image file.
 * If the descriptor is a data descriptor, than 'data' must be non-NULL,
 * and >= 4096 bytes (VHDX_LOG_SECTOR_SIZE), containing the data to be
 * written.
 *
 * Verification is performed to make sure the sequence numbers of a data
 * descriptor match the sequence number in the desc.
 *
 * For a zero descriptor, it may describe multiple sectors to fill with zeroes.
 * In this case, it should be noted that zeroes are written to disk, and the
 * image file is not extended as a sparse file.  */
static int vhdx_log_flush_desc(BlockDriverState *bs, VHDXLogDescriptor *desc,
                               VHDXLogDataSector *data)
{
    int ret = 0;
    uint64_t seq, file_offset;
    uint32_t offset = 0;
    void *buffer = NULL;
    uint64_t count = 1;
    int i;

    buffer = qemu_blockalign(bs, VHDX_LOG_SECTOR_SIZE);

    if (!memcmp(&desc->signature, "desc", 4)) {
        /* data sector */
        if (data == NULL) {
            ret = -EFAULT;
            goto exit;
        }

        /* The sequence number of the data sector must match that
         * in the descriptor */
        seq = data->sequence_high;
        seq <<= 32;
        seq |= data->sequence_low & 0xffffffff;

        if (seq != desc->sequence_number) {
            ret = -EINVAL;
            goto exit;
        }

        /* Each data sector is in total 4096 bytes, however the first
         * 8 bytes, and last 4 bytes, are located in the descriptor */
        memcpy(buffer, &desc->leading_bytes, 8);
        offset += 8;

        memcpy(buffer+offset, data->data, 4084);
        offset += 4084;

        memcpy(buffer+offset, &desc->trailing_bytes, 4);

    } else if (!memcmp(&desc->signature, "zero", 4)) {
        /* write 'count' sectors of sector */
        memset(buffer, 0, VHDX_LOG_SECTOR_SIZE);
        count = desc->zero_length / VHDX_LOG_SECTOR_SIZE;
    }

    file_offset = desc->file_offset;

    /* count is only > 1 if we are writing zeroes */
    for (i = 0; i < count; i++) {
        ret = bdrv_pwrite_sync(bs->file, file_offset, buffer,
                               VHDX_LOG_SECTOR_SIZE);
        if (ret < 0) {
            goto exit;
        }
        file_offset += VHDX_LOG_SECTOR_SIZE;
    }

exit:
    qemu_vfree(buffer);
    return ret;
}

/* Flush the entire log (as described by 'logs') to the VHDX image
 * file, and then set the log to 'empty' status once complete.
 *
 * The log entries should be validate prior to flushing */
static int vhdx_log_flush(BlockDriverState *bs, BDRVVHDXState *s,
                          VHDXLogSequence *logs)
{
    int ret = 0;
    int i;
    uint32_t cnt, sectors_read;
    uint64_t new_file_size;
    void *data = NULL;
    VHDXLogDescEntries *desc_entries = NULL;
    VHDXLogEntryHeader hdr_tmp = { 0 };

    cnt = logs->count;

    data = qemu_blockalign(bs, VHDX_LOG_SECTOR_SIZE);

    ret = vhdx_user_visible_write(bs, s);
    if (ret < 0) {
        goto exit;
    }

    /* each iteration represents one log sequence, which may span multiple
     * sectors */
    while (cnt--) {
        ret = vhdx_log_peek_hdr(bs, &logs->log, &hdr_tmp);
        if (ret < 0) {
            goto exit;
        }
        /* if the log shows a FlushedFileOffset larger than our current file
         * size, then that means the file has been truncated / corrupted, and
         * we must refused to open it / use it */
        if (hdr_tmp.flushed_file_offset > bdrv_getlength(bs->file)) {
            ret = -EINVAL;
            goto exit;
        }

        ret = vhdx_log_read_desc(bs, s, &logs->log, &desc_entries);
        if (ret < 0) {
            goto exit;
        }

        for (i = 0; i < desc_entries->hdr.descriptor_count; i++) {
            if (!memcmp(&desc_entries->desc[i].signature, "desc", 4)) {
                /* data sector, so read a sector to flush */
                ret = vhdx_log_read_sectors(bs, &logs->log, &sectors_read,
                                            data, 1, false);
                if (ret < 0) {
                    goto exit;
                }
                if (sectors_read != 1) {
                    ret = -EINVAL;
                    goto exit;
                }
            }

            ret = vhdx_log_flush_desc(bs, &desc_entries->desc[i], data);
            if (ret < 0) {
                goto exit;
            }
        }
        if (bdrv_getlength(bs->file) < desc_entries->hdr.last_file_offset) {
            new_file_size = desc_entries->hdr.last_file_offset;
            if (new_file_size % (1024*1024)) {
                /* round up to nearest 1MB boundary */
                new_file_size = ((new_file_size >> 20) + 1) << 20;
                bdrv_truncate(bs->file, new_file_size);
            }
        }
        qemu_vfree(desc_entries);
        desc_entries = NULL;
    }

    bdrv_flush(bs);
    /* once the log is fully flushed, indicate that we have an empty log
     * now.  This also sets the log guid to 0, to indicate an empty log */
    vhdx_log_reset(bs, s);

exit:
    qemu_vfree(data);
    qemu_vfree(desc_entries);
    return ret;
}

static int vhdx_validate_log_entry(BlockDriverState *bs, BDRVVHDXState *s,
                                   VHDXLogEntries *log, uint64_t seq,
                                   bool *valid, VHDXLogEntryHeader *entry)
{
    int ret = 0;
    VHDXLogEntryHeader hdr;
    void *buffer = NULL;
    uint32_t i, desc_sectors, total_sectors, crc;
    uint32_t sectors_read = 0;
    VHDXLogDescEntries *desc_buffer = NULL;

    *valid = false;

    ret = vhdx_log_peek_hdr(bs, log, &hdr);
    if (ret < 0) {
        goto inc_and_exit;
    }

    vhdx_log_entry_hdr_le_import(&hdr);


    if (vhdx_log_hdr_is_valid(log, &hdr, s) == false) {
        goto inc_and_exit;
    }

    if (seq > 0) {
        if (hdr.sequence_number != seq + 1) {
            goto inc_and_exit;
        }
    }

    desc_sectors = vhdx_compute_desc_sectors(hdr.descriptor_count);

    /* Read desc sectors, and calculate log checksum */

    total_sectors = hdr.entry_length / VHDX_LOG_SECTOR_SIZE;


    /* read_desc() will incrememnt the read idx */
    ret = vhdx_log_read_desc(bs, s, log, &desc_buffer);
    if (ret < 0) {
        goto free_and_exit;
    }

    crc = vhdx_checksum_calc(0xffffffff, (void *)desc_buffer,
                            desc_sectors * VHDX_LOG_SECTOR_SIZE, 4);
    crc ^= 0xffffffff;

    buffer = qemu_blockalign(bs, VHDX_LOG_SECTOR_SIZE);
    if (total_sectors > desc_sectors) {
        for (i = 0; i < total_sectors - desc_sectors; i++) {
            sectors_read = 0;
            ret = vhdx_log_read_sectors(bs, log, &sectors_read, buffer,
                                        1, false);
            if (ret < 0 || sectors_read != 1) {
                goto free_and_exit;
            }
            crc = vhdx_checksum_calc(crc, buffer, VHDX_LOG_SECTOR_SIZE, -1);
            crc ^= 0xffffffff;
        }
    }
    crc ^= 0xffffffff;
    if (crc != desc_buffer->hdr.checksum) {
        goto free_and_exit;
    }

    *valid = true;
    *entry = hdr;
    goto free_and_exit;

inc_and_exit:
    log->read = vhdx_log_inc_idx(log->read, log->length);

free_and_exit:
    qemu_vfree(buffer);
    qemu_vfree(desc_buffer);
    return ret;
}

/* Search through the log circular buffer, and find the valid, active
 * log sequence, if any exists
 * */
static int vhdx_log_search(BlockDriverState *bs, BDRVVHDXState *s,
                           VHDXLogSequence *logs)
{
    int ret = 0;
    uint32_t tail;
    bool seq_valid = false;
    VHDXLogSequence candidate = { 0 };
    VHDXLogEntryHeader hdr = { 0 };
    VHDXLogEntries curr_log;

    memcpy(&curr_log, &s->log, sizeof(VHDXLogEntries));
    curr_log.write = curr_log.length;   /* assume log is full */
    curr_log.read = 0;


    /* now we will go through the whole log sector by sector, until
     * we find a valid, active log sequence, or reach the end of the
     * log buffer */
    for (;;) {
        uint64_t curr_seq = 0;
        VHDXLogSequence current = { 0 };

        tail = curr_log.read;

        ret = vhdx_validate_log_entry(bs, s, &curr_log, curr_seq,
                                      &seq_valid, &hdr);
        if (ret < 0) {
            goto exit;
        }

        if (seq_valid) {
            current.valid     = true;
            current.log       = curr_log;
            current.log.read  = tail;
            current.log.write = curr_log.read;
            current.count     = 1;
            current.hdr       = hdr;


            for (;;) {
                ret = vhdx_validate_log_entry(bs, s, &curr_log, curr_seq,
                                              &seq_valid, &hdr);
                if (ret < 0) {
                    goto exit;
                }
                if (seq_valid == false) {
                    break;
                }
                current.log.write = curr_log.read;
                current.count++;

                curr_seq = hdr.sequence_number;
            }
        }

        if (current.valid) {
            if (candidate.valid == false ||
                current.hdr.sequence_number > candidate.hdr.sequence_number) {
                candidate = current;
            }
        }

        if (curr_log.read < tail) {
            break;
        }
    }

    *logs = candidate;

    if (candidate.valid) {
        /* this is the next sequence number, for writes */
        s->log.sequence = candidate.hdr.sequence_number + 1;
    }


exit:
    return ret;
}

/* Parse the replay log.  Per the VHDX spec, if the log is present
 * it must be replayed prior to opening the file, even read-only.
 *
 * If read-only, we must replay the log in RAM (or refuse to open
 * a dirty VHDX file read-only) */
int vhdx_parse_log(BlockDriverState *bs, BDRVVHDXState *s, bool *flushed)
{
    int ret = 0;
    VHDXHeader *hdr;
    VHDXLogSequence logs = { 0 };

    hdr = s->headers[s->curr_header];

    *flushed = false;

    /* s->log.hdr is freed in vhdx_close() */
    if (s->log.hdr == NULL) {
        s->log.hdr = qemu_blockalign(bs, sizeof(VHDXLogEntryHeader));
    }

    s->log.offset = hdr->log_offset;
    s->log.length = hdr->log_length;

    if (s->log.offset < VHDX_LOG_MIN_SIZE ||
        s->log.offset % VHDX_LOG_MIN_SIZE) {
        ret = -EINVAL;
        goto exit;
    }

    /* per spec, only log version of 0 is supported */
    if (hdr->log_version != 0) {
        ret = -EINVAL;
        goto exit;
    }

    /* If either the log guid, or log length is zero,
     * then a replay log is not present */
    if (guid_eq(hdr->log_guid, zero_guid)) {
        goto exit;
    }

    if (hdr->log_length == 0) {
        goto exit;
    }

    if (hdr->log_length % VHDX_LOG_MIN_SIZE) {
        ret = -EINVAL;
        goto exit;
    }


    /* The log is present, we need to find if and where there is an active
     * sequence of valid entries present in the log.  */

    ret = vhdx_log_search(bs, s, &logs);
    if (ret < 0) {
        goto exit;
    }

    if (logs.valid) {
        /* now flush the log */
        ret = vhdx_log_flush(bs, s, &logs);
        if (ret < 0) {
            goto exit;
        }
        *flushed = true;
    }


exit:
    return ret;
}


