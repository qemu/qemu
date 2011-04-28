/*
 * QEMU ATAPI Emulation
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw/ide/internal.h"
#include "hw/scsi.h"

static void ide_atapi_cmd_read_dma_cb(void *opaque, int ret);

static void padstr8(uint8_t *buf, int buf_size, const char *src)
{
    int i;
    for(i = 0; i < buf_size; i++) {
        if (*src)
            buf[i] = *src++;
        else
            buf[i] = ' ';
    }
}

static inline void cpu_to_ube16(uint8_t *buf, int val)
{
    buf[0] = val >> 8;
    buf[1] = val & 0xff;
}

static inline void cpu_to_ube32(uint8_t *buf, unsigned int val)
{
    buf[0] = val >> 24;
    buf[1] = val >> 16;
    buf[2] = val >> 8;
    buf[3] = val & 0xff;
}

static inline int ube16_to_cpu(const uint8_t *buf)
{
    return (buf[0] << 8) | buf[1];
}

static inline int ube32_to_cpu(const uint8_t *buf)
{
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static void lba_to_msf(uint8_t *buf, int lba)
{
    lba += 150;
    buf[0] = (lba / 75) / 60;
    buf[1] = (lba / 75) % 60;
    buf[2] = lba % 75;
}

static inline int media_present(IDEState *s)
{
    return (s->nb_sectors > 0);
}

/* XXX: DVDs that could fit on a CD will be reported as a CD */
static inline int media_is_dvd(IDEState *s)
{
    return (media_present(s) && s->nb_sectors > CD_MAX_SECTORS);
}

static inline int media_is_cd(IDEState *s)
{
    return (media_present(s) && s->nb_sectors <= CD_MAX_SECTORS);
}

static void cd_data_to_raw(uint8_t *buf, int lba)
{
    /* sync bytes */
    buf[0] = 0x00;
    memset(buf + 1, 0xff, 10);
    buf[11] = 0x00;
    buf += 12;
    /* MSF */
    lba_to_msf(buf, lba);
    buf[3] = 0x01; /* mode 1 data */
    buf += 4;
    /* data */
    buf += 2048;
    /* XXX: ECC not computed */
    memset(buf, 0, 288);
}

static int cd_read_sector(BlockDriverState *bs, int lba, uint8_t *buf,
                           int sector_size)
{
    int ret;

    switch(sector_size) {
    case 2048:
        ret = bdrv_read(bs, (int64_t)lba << 2, buf, 4);
        break;
    case 2352:
        ret = bdrv_read(bs, (int64_t)lba << 2, buf + 16, 4);
        if (ret < 0)
            return ret;
        cd_data_to_raw(buf, lba);
        break;
    default:
        ret = -EIO;
        break;
    }
    return ret;
}

void ide_atapi_cmd_ok(IDEState *s)
{
    s->error = 0;
    s->status = READY_STAT | SEEK_STAT;
    s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    ide_set_irq(s->bus);
}

void ide_atapi_cmd_error(IDEState *s, int sense_key, int asc)
{
#ifdef DEBUG_IDE_ATAPI
    printf("atapi_cmd_error: sense=0x%x asc=0x%x\n", sense_key, asc);
#endif
    s->error = sense_key << 4;
    s->status = READY_STAT | ERR_STAT;
    s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    s->sense_key = sense_key;
    s->asc = asc;
    ide_set_irq(s->bus);
}

void ide_atapi_io_error(IDEState *s, int ret)
{
    /* XXX: handle more errors */
    if (ret == -ENOMEDIUM) {
        ide_atapi_cmd_error(s, SENSE_NOT_READY,
                            ASC_MEDIUM_NOT_PRESENT);
    } else {
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                            ASC_LOGICAL_BLOCK_OOR);
    }
}

/* The whole ATAPI transfer logic is handled in this function */
void ide_atapi_cmd_reply_end(IDEState *s)
{
    int byte_count_limit, size, ret;
#ifdef DEBUG_IDE_ATAPI
    printf("reply: tx_size=%d elem_tx_size=%d index=%d\n",
           s->packet_transfer_size,
           s->elementary_transfer_size,
           s->io_buffer_index);
#endif
    if (s->packet_transfer_size <= 0) {
        /* end of transfer */
        ide_transfer_stop(s);
        s->status = READY_STAT | SEEK_STAT;
        s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
        ide_set_irq(s->bus);
#ifdef DEBUG_IDE_ATAPI
        printf("status=0x%x\n", s->status);
#endif
    } else {
        /* see if a new sector must be read */
        if (s->lba != -1 && s->io_buffer_index >= s->cd_sector_size) {
            ret = cd_read_sector(s->bs, s->lba, s->io_buffer, s->cd_sector_size);
            if (ret < 0) {
                ide_transfer_stop(s);
                ide_atapi_io_error(s, ret);
                return;
            }
            s->lba++;
            s->io_buffer_index = 0;
        }
        if (s->elementary_transfer_size > 0) {
            /* there are some data left to transmit in this elementary
               transfer */
            size = s->cd_sector_size - s->io_buffer_index;
            if (size > s->elementary_transfer_size)
                size = s->elementary_transfer_size;
            s->packet_transfer_size -= size;
            s->elementary_transfer_size -= size;
            s->io_buffer_index += size;
            ide_transfer_start(s, s->io_buffer + s->io_buffer_index - size,
                               size, ide_atapi_cmd_reply_end);
        } else {
            /* a new transfer is needed */
            s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO;
            byte_count_limit = s->lcyl | (s->hcyl << 8);
#ifdef DEBUG_IDE_ATAPI
            printf("byte_count_limit=%d\n", byte_count_limit);
#endif
            if (byte_count_limit == 0xffff)
                byte_count_limit--;
            size = s->packet_transfer_size;
            if (size > byte_count_limit) {
                /* byte count limit must be even if this case */
                if (byte_count_limit & 1)
                    byte_count_limit--;
                size = byte_count_limit;
            }
            s->lcyl = size;
            s->hcyl = size >> 8;
            s->elementary_transfer_size = size;
            /* we cannot transmit more than one sector at a time */
            if (s->lba != -1) {
                if (size > (s->cd_sector_size - s->io_buffer_index))
                    size = (s->cd_sector_size - s->io_buffer_index);
            }
            s->packet_transfer_size -= size;
            s->elementary_transfer_size -= size;
            s->io_buffer_index += size;
            ide_transfer_start(s, s->io_buffer + s->io_buffer_index - size,
                               size, ide_atapi_cmd_reply_end);
            ide_set_irq(s->bus);
#ifdef DEBUG_IDE_ATAPI
            printf("status=0x%x\n", s->status);
#endif
        }
    }
}

/* send a reply of 'size' bytes in s->io_buffer to an ATAPI command */
static void ide_atapi_cmd_reply(IDEState *s, int size, int max_size)
{
    if (size > max_size)
        size = max_size;
    s->lba = -1; /* no sector read */
    s->packet_transfer_size = size;
    s->io_buffer_size = size;    /* dma: send the reply data as one chunk */
    s->elementary_transfer_size = 0;
    s->io_buffer_index = 0;

    if (s->atapi_dma) {
        s->status = READY_STAT | SEEK_STAT | DRQ_STAT;
        s->bus->dma->ops->start_dma(s->bus->dma, s,
                                   ide_atapi_cmd_read_dma_cb);
    } else {
        s->status = READY_STAT | SEEK_STAT;
        ide_atapi_cmd_reply_end(s);
    }
}

/* start a CD-CDROM read command */
static void ide_atapi_cmd_read_pio(IDEState *s, int lba, int nb_sectors,
                                   int sector_size)
{
    s->lba = lba;
    s->packet_transfer_size = nb_sectors * sector_size;
    s->elementary_transfer_size = 0;
    s->io_buffer_index = sector_size;
    s->cd_sector_size = sector_size;

    s->status = READY_STAT | SEEK_STAT;
    ide_atapi_cmd_reply_end(s);
}

static void ide_atapi_cmd_check_status(IDEState *s)
{
#ifdef DEBUG_IDE_ATAPI
    printf("atapi_cmd_check_status\n");
#endif
    s->error = MC_ERR | (SENSE_UNIT_ATTENTION << 4);
    s->status = ERR_STAT;
    s->nsector = 0;
    ide_set_irq(s->bus);
}
/* ATAPI DMA support */

/* XXX: handle read errors */
static void ide_atapi_cmd_read_dma_cb(void *opaque, int ret)
{
    IDEState *s = opaque;
    int data_offset, n;

    if (ret < 0) {
        ide_atapi_io_error(s, ret);
        goto eot;
    }

    if (s->io_buffer_size > 0) {
        /*
         * For a cdrom read sector command (s->lba != -1),
         * adjust the lba for the next s->io_buffer_size chunk
         * and dma the current chunk.
         * For a command != read (s->lba == -1), just transfer
         * the reply data.
         */
        if (s->lba != -1) {
            if (s->cd_sector_size == 2352) {
                n = 1;
                cd_data_to_raw(s->io_buffer, s->lba);
            } else {
                n = s->io_buffer_size >> 11;
            }
            s->lba += n;
        }
        s->packet_transfer_size -= s->io_buffer_size;
        if (s->bus->dma->ops->rw_buf(s->bus->dma, 1) == 0)
            goto eot;
    }

    if (s->packet_transfer_size <= 0) {
        s->status = READY_STAT | SEEK_STAT;
        s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
        ide_set_irq(s->bus);
    eot:
        s->bus->dma->ops->add_status(s->bus->dma, BM_STATUS_INT);
        ide_set_inactive(s);
        return;
    }

    s->io_buffer_index = 0;
    if (s->cd_sector_size == 2352) {
        n = 1;
        s->io_buffer_size = s->cd_sector_size;
        data_offset = 16;
    } else {
        n = s->packet_transfer_size >> 11;
        if (n > (IDE_DMA_BUF_SECTORS / 4))
            n = (IDE_DMA_BUF_SECTORS / 4);
        s->io_buffer_size = n * 2048;
        data_offset = 0;
    }
#ifdef DEBUG_AIO
    printf("aio_read_cd: lba=%u n=%d\n", s->lba, n);
#endif
    s->bus->dma->iov.iov_base = (void *)(s->io_buffer + data_offset);
    s->bus->dma->iov.iov_len = n * 4 * 512;
    qemu_iovec_init_external(&s->bus->dma->qiov, &s->bus->dma->iov, 1);
    s->bus->dma->aiocb = bdrv_aio_readv(s->bs, (int64_t)s->lba << 2,
                                       &s->bus->dma->qiov, n * 4,
                                       ide_atapi_cmd_read_dma_cb, s);
    if (!s->bus->dma->aiocb) {
        /* Note: media not present is the most likely case */
        ide_atapi_cmd_error(s, SENSE_NOT_READY,
                            ASC_MEDIUM_NOT_PRESENT);
        goto eot;
    }
}

/* start a CD-CDROM read command with DMA */
/* XXX: test if DMA is available */
static void ide_atapi_cmd_read_dma(IDEState *s, int lba, int nb_sectors,
                                   int sector_size)
{
    s->lba = lba;
    s->packet_transfer_size = nb_sectors * sector_size;
    s->io_buffer_index = 0;
    s->io_buffer_size = 0;
    s->cd_sector_size = sector_size;

    /* XXX: check if BUSY_STAT should be set */
    s->status = READY_STAT | SEEK_STAT | DRQ_STAT | BUSY_STAT;
    s->bus->dma->ops->start_dma(s->bus->dma, s,
                               ide_atapi_cmd_read_dma_cb);
}

static void ide_atapi_cmd_read(IDEState *s, int lba, int nb_sectors,
                               int sector_size)
{
#ifdef DEBUG_IDE_ATAPI
    printf("read %s: LBA=%d nb_sectors=%d\n", s->atapi_dma ? "dma" : "pio",
        lba, nb_sectors);
#endif
    if (s->atapi_dma) {
        ide_atapi_cmd_read_dma(s, lba, nb_sectors, sector_size);
    } else {
        ide_atapi_cmd_read_pio(s, lba, nb_sectors, sector_size);
    }
}

static inline uint8_t ide_atapi_set_profile(uint8_t *buf, uint8_t *index,
                                            uint16_t profile)
{
    uint8_t *buf_profile = buf + 12; /* start of profiles */

    buf_profile += ((*index) * 4); /* start of indexed profile */
    cpu_to_ube16 (buf_profile, profile);
    buf_profile[2] = ((buf_profile[0] == buf[6]) && (buf_profile[1] == buf[7]));

    /* each profile adds 4 bytes to the response */
    (*index)++;
    buf[11] += 4; /* Additional Length */

    return 4;
}

static int ide_dvd_read_structure(IDEState *s, int format,
                                  const uint8_t *packet, uint8_t *buf)
{
    switch (format) {
        case 0x0: /* Physical format information */
            {
                int layer = packet[6];
                uint64_t total_sectors;

                if (layer != 0)
                    return -ASC_INV_FIELD_IN_CMD_PACKET;

                total_sectors = s->nb_sectors >> 2;
                if (total_sectors == 0) {
                    return -ASC_MEDIUM_NOT_PRESENT;
                }

                buf[4] = 1;   /* DVD-ROM, part version 1 */
                buf[5] = 0xf; /* 120mm disc, minimum rate unspecified */
                buf[6] = 1;   /* one layer, read-only (per MMC-2 spec) */
                buf[7] = 0;   /* default densities */

                /* FIXME: 0x30000 per spec? */
                cpu_to_ube32(buf + 8, 0); /* start sector */
                cpu_to_ube32(buf + 12, total_sectors - 1); /* end sector */
                cpu_to_ube32(buf + 16, total_sectors - 1); /* l0 end sector */

                /* Size of buffer, not including 2 byte size field */
                cpu_to_be16wu((uint16_t *)buf, 2048 + 2);

                /* 2k data + 4 byte header */
                return (2048 + 4);
            }

        case 0x01: /* DVD copyright information */
            buf[4] = 0; /* no copyright data */
            buf[5] = 0; /* no region restrictions */

            /* Size of buffer, not including 2 byte size field */
            cpu_to_be16wu((uint16_t *)buf, 4 + 2);

            /* 4 byte header + 4 byte data */
            return (4 + 4);

        case 0x03: /* BCA information - invalid field for no BCA info */
            return -ASC_INV_FIELD_IN_CMD_PACKET;

        case 0x04: /* DVD disc manufacturing information */
            /* Size of buffer, not including 2 byte size field */
            cpu_to_be16wu((uint16_t *)buf, 2048 + 2);

            /* 2k data + 4 byte header */
            return (2048 + 4);

        case 0xff:
            /*
             * This lists all the command capabilities above.  Add new ones
             * in order and update the length and buffer return values.
             */

            buf[4] = 0x00; /* Physical format */
            buf[5] = 0x40; /* Not writable, is readable */
            cpu_to_be16wu((uint16_t *)(buf + 6), 2048 + 4);

            buf[8] = 0x01; /* Copyright info */
            buf[9] = 0x40; /* Not writable, is readable */
            cpu_to_be16wu((uint16_t *)(buf + 10), 4 + 4);

            buf[12] = 0x03; /* BCA info */
            buf[13] = 0x40; /* Not writable, is readable */
            cpu_to_be16wu((uint16_t *)(buf + 14), 188 + 4);

            buf[16] = 0x04; /* Manufacturing info */
            buf[17] = 0x40; /* Not writable, is readable */
            cpu_to_be16wu((uint16_t *)(buf + 18), 2048 + 4);

            /* Size of buffer, not including 2 byte size field */
            cpu_to_be16wu((uint16_t *)buf, 16 + 2);

            /* data written + 4 byte header */
            return (16 + 4);

        default: /* TODO: formats beyond DVD-ROM requires */
            return -ASC_INV_FIELD_IN_CMD_PACKET;
    }
}

static unsigned int event_status_media(IDEState *s,
                                       uint8_t *buf)
{
    enum media_event_code {
        MEC_NO_CHANGE = 0,       /* Status unchanged */
        MEC_EJECT_REQUESTED,     /* received a request from user to eject */
        MEC_NEW_MEDIA,           /* new media inserted and ready for access */
        MEC_MEDIA_REMOVAL,       /* only for media changers */
        MEC_MEDIA_CHANGED,       /* only for media changers */
        MEC_BG_FORMAT_COMPLETED, /* MRW or DVD+RW b/g format completed */
        MEC_BG_FORMAT_RESTARTED, /* MRW or DVD+RW b/g format restarted */
    };
    enum media_status {
        MS_TRAY_OPEN = 1,
        MS_MEDIA_PRESENT = 2,
    };
    uint8_t event_code, media_status;

    media_status = 0;
    if (s->bs->tray_open) {
        media_status = MS_TRAY_OPEN;
    } else if (bdrv_is_inserted(s->bs)) {
        media_status = MS_MEDIA_PRESENT;
    }

    /* Event notification descriptor */
    event_code = MEC_NO_CHANGE;
    if (media_status != MS_TRAY_OPEN && s->events.new_media) {
        event_code = MEC_NEW_MEDIA;
        s->events.new_media = false;
    }

    buf[4] = event_code;
    buf[5] = media_status;

    /* These fields are reserved, just clear them. */
    buf[6] = 0;
    buf[7] = 0;

    return 8; /* We wrote to 4 extra bytes from the header */
}

static void cmd_get_event_status_notification(IDEState *s,
                                              uint8_t *buf)
{
    const uint8_t *packet = buf;

    struct {
        uint8_t opcode;
        uint8_t polled;        /* lsb bit is polled; others are reserved */
        uint8_t reserved2[2];
        uint8_t class;
        uint8_t reserved3[2];
        uint16_t len;
        uint8_t control;
    } __attribute__((packed)) *gesn_cdb;

    struct {
        uint16_t len;
        uint8_t notification_class;
        uint8_t supported_events;
    } __attribute((packed)) *gesn_event_header;

    enum notification_class_request_type {
        NCR_RESERVED1 = 1 << 0,
        NCR_OPERATIONAL_CHANGE = 1 << 1,
        NCR_POWER_MANAGEMENT = 1 << 2,
        NCR_EXTERNAL_REQUEST = 1 << 3,
        NCR_MEDIA = 1 << 4,
        NCR_MULTI_HOST = 1 << 5,
        NCR_DEVICE_BUSY = 1 << 6,
        NCR_RESERVED2 = 1 << 7,
    };
    enum event_notification_class_field {
        ENC_NO_EVENTS = 0,
        ENC_OPERATIONAL_CHANGE,
        ENC_POWER_MANAGEMENT,
        ENC_EXTERNAL_REQUEST,
        ENC_MEDIA,
        ENC_MULTIPLE_HOSTS,
        ENC_DEVICE_BUSY,
        ENC_RESERVED,
    };
    unsigned int max_len, used_len;

    gesn_cdb = (void *)packet;
    gesn_event_header = (void *)buf;

    max_len = be16_to_cpu(gesn_cdb->len);

    /* It is fine by the MMC spec to not support async mode operations */
    if (!(gesn_cdb->polled & 0x01)) { /* asynchronous mode */
        /* Only polling is supported, asynchronous mode is not. */
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
        return;
    }

    /* polling mode operation */

    /*
     * These are the supported events.
     *
     * We currently only support requests of the 'media' type.
     */
    gesn_event_header->supported_events = NCR_MEDIA;

    /*
     * We use |= below to set the class field; other bits in this byte
     * are reserved now but this is useful to do if we have to use the
     * reserved fields later.
     */
    gesn_event_header->notification_class = 0;

    /*
     * Responses to requests are to be based on request priority.  The
     * notification_class_request_type enum above specifies the
     * priority: upper elements are higher prio than lower ones.
     */
    if (gesn_cdb->class & NCR_MEDIA) {
        gesn_event_header->notification_class |= ENC_MEDIA;
        used_len = event_status_media(s, buf);
    } else {
        gesn_event_header->notification_class = 0x80; /* No event available */
        used_len = sizeof(*gesn_event_header);
    }
    gesn_event_header->len = cpu_to_be16(used_len
                                         - sizeof(*gesn_event_header));
    ide_atapi_cmd_reply(s, used_len, max_len);
}

static void cmd_request_sense(IDEState *s, uint8_t *buf)
{
    int max_len = buf[4];

    memset(buf, 0, 18);
    buf[0] = 0x70 | (1 << 7);
    buf[2] = s->sense_key;
    buf[7] = 10;
    buf[12] = s->asc;

    if (s->sense_key == SENSE_UNIT_ATTENTION) {
        s->sense_key = SENSE_NONE;
    }

    ide_atapi_cmd_reply(s, 18, max_len);
}

static void cmd_inquiry(IDEState *s, uint8_t *buf)
{
    int max_len = buf[4];

    buf[0] = 0x05; /* CD-ROM */
    buf[1] = 0x80; /* removable */
    buf[2] = 0x00; /* ISO */
    buf[3] = 0x21; /* ATAPI-2 (XXX: put ATAPI-4 ?) */
    buf[4] = 31; /* additional length */
    buf[5] = 0; /* reserved */
    buf[6] = 0; /* reserved */
    buf[7] = 0; /* reserved */
    padstr8(buf + 8, 8, "QEMU");
    padstr8(buf + 16, 16, "QEMU DVD-ROM");
    padstr8(buf + 32, 4, s->version);
    ide_atapi_cmd_reply(s, 36, max_len);
}

static void cmd_get_configuration(IDEState *s, uint8_t *buf)
{
    uint32_t len;
    uint8_t index = 0;
    int max_len;

    /* only feature 0 is supported */
    if (buf[2] != 0 || buf[3] != 0) {
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
        return;
    }

    /* XXX: could result in alignment problems in some architectures */
    max_len = ube16_to_cpu(buf + 7);

    /*
     * XXX: avoid overflow for io_buffer if max_len is bigger than
     *      the size of that buffer (dimensioned to max number of
     *      sectors to transfer at once)
     *
     *      Only a problem if the feature/profiles grow.
     */
    if (max_len > 512) {
        /* XXX: assume 1 sector */
        max_len = 512;
    }

    memset(buf, 0, max_len);
    /*
     * the number of sectors from the media tells us which profile
     * to use as current.  0 means there is no media
     */
    if (media_is_dvd(s)) {
        cpu_to_ube16(buf + 6, MMC_PROFILE_DVD_ROM);
    } else if (media_is_cd(s)) {
        cpu_to_ube16(buf + 6, MMC_PROFILE_CD_ROM);
    }

    buf[10] = 0x02 | 0x01; /* persistent and current */
    len = 12; /* headers: 8 + 4 */
    len += ide_atapi_set_profile(buf, &index, MMC_PROFILE_DVD_ROM);
    len += ide_atapi_set_profile(buf, &index, MMC_PROFILE_CD_ROM);
    cpu_to_ube32(buf, len - 4); /* data length */

    ide_atapi_cmd_reply(s, len, max_len);
}

static void cmd_mode_sense(IDEState *s, uint8_t *buf)
{
    int action, code;
    int max_len;

    if (buf[0] == GPCMD_MODE_SENSE_10) {
        max_len = ube16_to_cpu(buf + 7);
    } else {
        max_len = buf[4];
    }

    action = buf[2] >> 6;
    code = buf[2] & 0x3f;

    switch(action) {
    case 0: /* current values */
        switch(code) {
        case GPMODE_R_W_ERROR_PAGE: /* error recovery */
            cpu_to_ube16(&buf[0], 16 + 6);
            buf[2] = 0x70;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = 0;
            buf[7] = 0;

            buf[8] = 0x01;
            buf[9] = 0x06;
            buf[10] = 0x00;
            buf[11] = 0x05;
            buf[12] = 0x00;
            buf[13] = 0x00;
            buf[14] = 0x00;
            buf[15] = 0x00;
            ide_atapi_cmd_reply(s, 16, max_len);
            break;
        case GPMODE_AUDIO_CTL_PAGE:
            cpu_to_ube16(&buf[0], 24 + 6);
            buf[2] = 0x70;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = 0;
            buf[7] = 0;

            /* Fill with CDROM audio volume */
            buf[17] = 0;
            buf[19] = 0;
            buf[21] = 0;
            buf[23] = 0;

            ide_atapi_cmd_reply(s, 24, max_len);
            break;
        case GPMODE_CAPABILITIES_PAGE:
            cpu_to_ube16(&buf[0], 28 + 6);
            buf[2] = 0x70;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = 0;
            buf[7] = 0;

            buf[8] = 0x2a;
            buf[9] = 0x12;
            buf[10] = 0x00;
            buf[11] = 0x00;

            /* Claim PLAY_AUDIO capability (0x01) since some Linux
               code checks for this to automount media. */
            buf[12] = 0x71;
            buf[13] = 3 << 5;
            buf[14] = (1 << 0) | (1 << 3) | (1 << 5);
            if (bdrv_is_locked(s->bs))
                buf[6] |= 1 << 1;
            buf[15] = 0x00;
            cpu_to_ube16(&buf[16], 706);
            buf[18] = 0;
            buf[19] = 2;
            cpu_to_ube16(&buf[20], 512);
            cpu_to_ube16(&buf[22], 706);
            buf[24] = 0;
            buf[25] = 0;
            buf[26] = 0;
            buf[27] = 0;
            ide_atapi_cmd_reply(s, 28, max_len);
            break;
        default:
            goto error_cmd;
        }
        break;
    case 1: /* changeable values */
        goto error_cmd;
    case 2: /* default values */
        goto error_cmd;
    default:
    case 3: /* saved values */
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                            ASC_SAVING_PARAMETERS_NOT_SUPPORTED);
        break;
    }
    return;

error_cmd:
    ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET);
}

static void cmd_test_unit_ready(IDEState *s, uint8_t *buf)
{
    /* Not Ready Conditions are already handled in ide_atapi_cmd(), so if we
     * come here, we know that it's ready. */
    ide_atapi_cmd_ok(s);
}

static void cmd_prevent_allow_medium_removal(IDEState *s, uint8_t* buf)
{
    bdrv_set_locked(s->bs, buf[4] & 1);
    ide_atapi_cmd_ok(s);
}

static void cmd_read(IDEState *s, uint8_t* buf)
{
    int nb_sectors, lba;

    if (buf[0] == GPCMD_READ_10) {
        nb_sectors = ube16_to_cpu(buf + 7);
    } else {
        nb_sectors = ube32_to_cpu(buf + 6);
    }

    lba = ube32_to_cpu(buf + 2);
    if (nb_sectors == 0) {
        ide_atapi_cmd_ok(s);
        return;
    }

    ide_atapi_cmd_read(s, lba, nb_sectors, 2048);
}

static void cmd_read_cd(IDEState *s, uint8_t* buf)
{
    int nb_sectors, lba, transfer_request;

    nb_sectors = (buf[6] << 16) | (buf[7] << 8) | buf[8];
    lba = ube32_to_cpu(buf + 2);

    if (nb_sectors == 0) {
        ide_atapi_cmd_ok(s);
        return;
    }

    transfer_request = buf[9];
    switch(transfer_request & 0xf8) {
    case 0x00:
        /* nothing */
        ide_atapi_cmd_ok(s);
        break;
    case 0x10:
        /* normal read */
        ide_atapi_cmd_read(s, lba, nb_sectors, 2048);
        break;
    case 0xf8:
        /* read all data */
        ide_atapi_cmd_read(s, lba, nb_sectors, 2352);
        break;
    default:
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
        break;
    }
}

static void cmd_seek(IDEState *s, uint8_t* buf)
{
    unsigned int lba;
    uint64_t total_sectors = s->nb_sectors >> 2;

    lba = ube32_to_cpu(buf + 2);
    if (lba >= total_sectors) {
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST, ASC_LOGICAL_BLOCK_OOR);
        return;
    }

    ide_atapi_cmd_ok(s);
}

static void cmd_start_stop_unit(IDEState *s, uint8_t* buf)
{
    int start, eject, sense, err = 0;
    start = buf[4] & 1;
    eject = (buf[4] >> 1) & 1;

    if (eject) {
        err = bdrv_eject(s->bs, !start);
    }

    switch (err) {
    case 0:
        ide_atapi_cmd_ok(s);
        break;
    case -EBUSY:
        sense = SENSE_NOT_READY;
        if (bdrv_is_inserted(s->bs)) {
            sense = SENSE_ILLEGAL_REQUEST;
        }
        ide_atapi_cmd_error(s, sense, ASC_MEDIA_REMOVAL_PREVENTED);
        break;
    default:
        ide_atapi_cmd_error(s, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
        break;
    }
}

static void cmd_mechanism_status(IDEState *s, uint8_t* buf)
{
    int max_len = ube16_to_cpu(buf + 8);

    cpu_to_ube16(buf, 0);
    /* no current LBA */
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 0;
    buf[5] = 1;
    cpu_to_ube16(buf + 6, 0);
    ide_atapi_cmd_reply(s, 8, max_len);
}

static void cmd_read_toc_pma_atip(IDEState *s, uint8_t* buf)
{
    int format, msf, start_track, len;
    int max_len;
    uint64_t total_sectors = s->nb_sectors >> 2;

    max_len = ube16_to_cpu(buf + 7);
    format = buf[9] >> 6;
    msf = (buf[1] >> 1) & 1;
    start_track = buf[6];

    switch(format) {
    case 0:
        len = cdrom_read_toc(total_sectors, buf, msf, start_track);
        if (len < 0)
            goto error_cmd;
        ide_atapi_cmd_reply(s, len, max_len);
        break;
    case 1:
        /* multi session : only a single session defined */
        memset(buf, 0, 12);
        buf[1] = 0x0a;
        buf[2] = 0x01;
        buf[3] = 0x01;
        ide_atapi_cmd_reply(s, 12, max_len);
        break;
    case 2:
        len = cdrom_read_toc_raw(total_sectors, buf, msf, start_track);
        if (len < 0)
            goto error_cmd;
        ide_atapi_cmd_reply(s, len, max_len);
        break;
    default:
    error_cmd:
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                            ASC_INV_FIELD_IN_CMD_PACKET);
    }
}

static void cmd_read_cdvd_capacity(IDEState *s, uint8_t* buf)
{
    uint64_t total_sectors = s->nb_sectors >> 2;

    /* NOTE: it is really the number of sectors minus 1 */
    cpu_to_ube32(buf, total_sectors - 1);
    cpu_to_ube32(buf + 4, 2048);
    ide_atapi_cmd_reply(s, 8, 8);
}

static void cmd_read_dvd_structure(IDEState *s, uint8_t* buf)
{
    int max_len;
    int media = buf[1];
    int format = buf[7];
    int ret;

    max_len = ube16_to_cpu(buf + 8);

    if (format < 0xff) {
        if (media_is_cd(s)) {
            ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                                ASC_INCOMPATIBLE_FORMAT);
            return;
        } else if (!media_present(s)) {
            ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                                ASC_INV_FIELD_IN_CMD_PACKET);
            return;
        }
    }

    memset(buf, 0, max_len > IDE_DMA_BUF_SECTORS * 512 + 4 ?
           IDE_DMA_BUF_SECTORS * 512 + 4 : max_len);

    switch (format) {
        case 0x00 ... 0x7f:
        case 0xff:
            if (media == 0) {
                ret = ide_dvd_read_structure(s, format, buf, buf);

                if (ret < 0) {
                    ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST, -ret);
                } else {
                    ide_atapi_cmd_reply(s, ret, max_len);
                }

                break;
            }
            /* TODO: BD support, fall through for now */

        /* Generic disk structures */
        case 0x80: /* TODO: AACS volume identifier */
        case 0x81: /* TODO: AACS media serial number */
        case 0x82: /* TODO: AACS media identifier */
        case 0x83: /* TODO: AACS media key block */
        case 0x90: /* TODO: List of recognized format layers */
        case 0xc0: /* TODO: Write protection status */
        default:
            ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                                ASC_INV_FIELD_IN_CMD_PACKET);
            break;
    }
}

static void cmd_set_speed(IDEState *s, uint8_t* buf)
{
    ide_atapi_cmd_ok(s);
}

enum {
    /*
     * Only commands flagged as ALLOW_UA are allowed to run under a
     * unit attention condition. (See MMC-5, section 4.1.6.1)
     */
    ALLOW_UA = 0x01,

    /*
     * Commands flagged with CHECK_READY can only execute if a medium is present.
     * Otherwise they report the Not Ready Condition. (See MMC-5, section
     * 4.1.8)
     */
    CHECK_READY = 0x02,
};

static const struct {
    void (*handler)(IDEState *s, uint8_t *buf);
    int flags;
} atapi_cmd_table[0x100] = {
    [ 0x00 ] = { cmd_test_unit_ready,               CHECK_READY },
    [ 0x03 ] = { cmd_request_sense,                 ALLOW_UA },
    [ 0x12 ] = { cmd_inquiry,                       ALLOW_UA },
    [ 0x1a ] = { cmd_mode_sense, /* (6) */          0 },
    [ 0x1b ] = { cmd_start_stop_unit,               0 },
    [ 0x1e ] = { cmd_prevent_allow_medium_removal,  0 },
    [ 0x25 ] = { cmd_read_cdvd_capacity,            CHECK_READY },
    [ 0x28 ] = { cmd_read, /* (10) */               0 },
    [ 0x2b ] = { cmd_seek,                          CHECK_READY },
    [ 0x43 ] = { cmd_read_toc_pma_atip,             CHECK_READY },
    [ 0x46 ] = { cmd_get_configuration,             ALLOW_UA },
    [ 0x4a ] = { cmd_get_event_status_notification, ALLOW_UA },
    [ 0x5a ] = { cmd_mode_sense, /* (10) */         0 },
    [ 0xa8 ] = { cmd_read, /* (12) */               0 },
    [ 0xad ] = { cmd_read_dvd_structure,            0 },
    [ 0xbb ] = { cmd_set_speed,                     0 },
    [ 0xbd ] = { cmd_mechanism_status,              0 },
    [ 0xbe ] = { cmd_read_cd,                       0 },
};

void ide_atapi_cmd(IDEState *s)
{
    const uint8_t *packet;
    uint8_t *buf;

    packet = s->io_buffer;
    buf = s->io_buffer;
#ifdef DEBUG_IDE_ATAPI
    {
        int i;
        printf("ATAPI limit=0x%x packet:", s->lcyl | (s->hcyl << 8));
        for(i = 0; i < ATAPI_PACKET_SIZE; i++) {
            printf(" %02x", packet[i]);
        }
        printf("\n");
    }
#endif
    /*
     * If there's a UNIT_ATTENTION condition pending, only command flagged with
     * ALLOW_UA are allowed to complete. with other commands getting a CHECK
     * condition response unless a higher priority status, defined by the drive
     * here, is pending.
     */
    if (s->sense_key == SENSE_UNIT_ATTENTION &&
        !(atapi_cmd_table[s->io_buffer[0]].flags & ALLOW_UA)) {
        ide_atapi_cmd_check_status(s);
        return;
    }
    /*
     * When a CD gets changed, we have to report an ejected state and
     * then a loaded state to guests so that they detect tray
     * open/close and media change events.  Guests that do not use
     * GET_EVENT_STATUS_NOTIFICATION to detect such tray open/close
     * states rely on this behavior.
     */
    if (bdrv_is_inserted(s->bs) && s->cdrom_changed) {
        ide_atapi_cmd_error(s, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);

        s->cdrom_changed = 0;
        s->sense_key = SENSE_UNIT_ATTENTION;
        s->asc = ASC_MEDIUM_MAY_HAVE_CHANGED;
        return;
    }

    /* Report a Not Ready condition if appropriate for the command */
    if ((atapi_cmd_table[s->io_buffer[0]].flags & CHECK_READY) &&
        (!media_present(s) || !bdrv_is_inserted(s->bs)))
    {
        ide_atapi_cmd_error(s, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
        return;
    }

    /* Execute the command */
    if (atapi_cmd_table[s->io_buffer[0]].handler) {
        atapi_cmd_table[s->io_buffer[0]].handler(s, buf);
        return;
    }

    ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE);
}
