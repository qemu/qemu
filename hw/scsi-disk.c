/*
 * SCSI Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Based on code by Fabrice Bellard
 *
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 */

//#define DEBUG_SCSI

#ifdef DEBUG_SCSI
#define DPRINTF(fmt, args...) \
do { printf("scsi-disk: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

#define BADF(fmt, args...) \
do { fprintf(stderr, "scsi-disk: " fmt , ##args); } while (0)

#include "vl.h"

#define SENSE_NO_SENSE        0
#define SENSE_ILLEGAL_REQUEST 5

struct SCSIDevice
{
    int command;
    uint32_t tag;
    BlockDriverState *bdrv;
    /* The qemu block layer uses a fixed 512 byte sector size.
       This is the number of 512 byte blocks in a single scsi sector.  */
    int cluster_size;
    /* When transfering data buf_pos and buf_len contain a partially
       transferred block of data (or response to a command), and
       sector/sector_count identify any remaining sectors.
       Both sector and sector_count are in terms of qemu 512 byte blocks.  */
    /* ??? We should probably keep track of whether the data trasfer is
       a read or a write.  Currently we rely on the host getting it right.  */
    int sector;
    int sector_count;
    int buf_pos;
    int buf_len;
    int sense;
    char buf[512];
    scsi_completionfn completion;
    void *opaque;
};

static void scsi_command_complete(SCSIDevice *s, int sense)
{
    s->sense = sense;
    s->completion(s->opaque, s->tag, sense);
}

/* Read data from a scsi device.  Returns nonzero on failure.  */
int scsi_read_data(SCSIDevice *s, uint8_t *data, uint32_t len)
{
    uint32_t n;

    DPRINTF("Read %d (%d/%d)\n", len, s->buf_len, s->sector_count);
    if (s->buf_len == 0 && s->sector_count == 0)
        return 1;

    if (s->buf_len) {
        n = s->buf_len;
        if (n > len)
            n = len;
        memcpy(data, s->buf + s->buf_pos, n);
        s->buf_pos += n;
        s->buf_len -= n;
        data += n;
        len -= n;
        if (s->buf_len == 0)
            s->buf_pos = 0;
    }

    n = len / 512;
    if (n > s->sector_count)
      n = s->sector_count;

    if (n != 0) {
        bdrv_read(s->bdrv, s->sector, data, n);
        data += n * 512;
        len -= n * 512;
        s->sector += n;
        s->sector_count -= n;
    }

    if (len && s->sector_count) {
        bdrv_read(s->bdrv, s->sector, s->buf, 1);
        s->sector++;
        s->sector_count--;
        s->buf_pos = 0;
        s->buf_len = 512;
        /* Recurse to complete the partial read.  */
        return scsi_read_data(s, data, len);
    }

    if (len != 0)
        return 1;

    if (s->buf_len == 0 && s->sector_count == 0)
        scsi_command_complete(s, SENSE_NO_SENSE);

    return 0;
}

/* Read data to a scsi device.  Returns nonzero on failure.  */
int scsi_write_data(SCSIDevice *s, uint8_t *data, uint32_t len)
{
    uint32_t n;

    DPRINTF("Write %d (%d/%d)\n", len, s->buf_len, s->sector_count);
    if (s->buf_pos != 0) {
        BADF("Bad state on write\n");
        return 1;
    }

    if (s->sector_count == 0)
        return 1;

    if (s->buf_len != 0 || len < 512) {
        n = 512 - s->buf_len;
        if (n > len)
            n = len;

        memcpy(s->buf + s->buf_len, data, n);
        data += n;
        s->buf_len += n;
        len -= n;
        if (s->buf_len == 512) {
            /* A full sector has been accumulated. Write it to disk.  */
            bdrv_write(s->bdrv, s->sector, s->buf, 1);
            s->buf_len = 0;
            s->sector++;
            s->sector_count--;
        }
    }

    n = len / 512;
    if (n > s->sector_count)
        n = s->sector_count;

    if (n != 0) {
        bdrv_write(s->bdrv, s->sector, data, n);
        data += n * 512;
        len -= n * 512;
        s->sector += n;
        s->sector_count -= n;
    }

    if (len >= 512)
        return 1;

    if (len && s->sector_count) {
        /* Recurse to complete the partial write.  */
        return scsi_write_data(s, data, len);
    }

    if (len != 0)
        return 1;

    if (s->sector_count == 0)
        scsi_command_complete(s, SENSE_NO_SENSE);

    return 0;
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

int32_t scsi_send_command(SCSIDevice *s, uint32_t tag, uint8_t *buf, int lun)
{
    int64_t nb_sectors;
    uint32_t lba;
    uint32_t len;
    int cmdlen;
    int is_write;

    s->command = buf[0];
    s->tag = tag;
    s->sector_count = 0;
    s->buf_pos = 0;
    s->buf_len = 0;
    is_write = 0;
    DPRINTF("Command: 0x%02x", buf[0]);
    switch (s->command >> 5) {
    case 0:
        lba = buf[3] | (buf[2] << 8) | ((buf[1] & 0x1f) << 16);
        len = buf[4];
        cmdlen = 6;
        break;
    case 1:
    case 2:
        lba = buf[5] | (buf[4] << 8) | (buf[3] << 16) | (buf[2] << 24);
        len = buf[8] | (buf[7] << 8);
        cmdlen = 10;
        break;
    case 4:
        lba = buf[5] | (buf[4] << 8) | (buf[3] << 16) | (buf[2] << 24);
        len = buf[13] | (buf[12] << 8) | (buf[11] << 16) | (buf[10] << 24);
        cmdlen = 16;
        break;
    case 5:
        lba = buf[5] | (buf[4] << 8) | (buf[3] << 16) | (buf[2] << 24);
        len = buf[9] | (buf[8] << 8) | (buf[7] << 16) | (buf[6] << 24);
        cmdlen = 12;
        break;
    default:
        BADF("Unsupported command length, command %x\n", s->command);
        goto fail;
    }
#ifdef DEBUG_SCSI
    {
        int i;
        for (i = 1; i < cmdlen; i++) {
            printf(" 0x%02x", buf[i]);
        }
        printf("\n");
    }
#endif
    if (lun || buf[1] >> 5) {
        /* Only LUN 0 supported.  */
        DPRINTF("Unimplemented LUN %d\n", lun ? lun : buf[1] >> 5);
        goto fail;
    }
    switch (s->command) {
    case 0x0:
	DPRINTF("Test Unit Ready\n");
	break;
    case 0x03:
        DPRINTF("Request Sense (len %d)\n", len);
        if (len < 4)
            goto fail;
        memset(buf, 0, 4);
        s->buf[0] = 0xf0;
        s->buf[1] = 0;
        s->buf[2] = s->sense;
        s->buf_len = 4;
        break;
    case 0x12:
        DPRINTF("Inquiry (len %d)\n", len);
        if (len < 36) {
            BADF("Inquiry buffer too small (%d)\n", len);
        }
	memset(s->buf, 0, 36);
	if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
	    s->buf[0] = 5;
            s->buf[1] = 0x80;
	    memcpy(&s->buf[16], "QEMU CD-ROM    ", 16);
	} else {
	    s->buf[0] = 0;
	    memcpy(&s->buf[16], "QEMU HARDDISK  ", 16);
	}
	memcpy(&s->buf[8], "QEMU   ", 8);
        memcpy(&s->buf[32], QEMU_VERSION, 4);
        /* Identify device as SCSI-3 rev 1.
           Some later commands are also implemented. */
	s->buf[2] = 3;
	s->buf[3] = 2; /* Format 2 */
	s->buf[4] = 32;
	s->buf_len = 36;
	break;
    case 0x16:
        DPRINTF("Reserve(6)\n");
        if (buf[1] & 1)
            goto fail;
        break;
    case 0x17:
        DPRINTF("Release(6)\n");
        if (buf[1] & 1)
            goto fail;
        break;
    case 0x1a:
    case 0x5a:
        {
            char *p;
            int page;

            page = buf[2] & 0x3f;
            DPRINTF("Mode Sense (page %d, len %d)\n", page, len);
            p = s->buf;
            memset(p, 0, 4);
            s->buf[1] = 0; /* Default media type.  */
            s->buf[3] = 0; /* Block descriptor length.  */
            if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
                s->buf[2] = 0x80; /* Readonly.  */
            }
            p += 4;
            if ((page == 8 || page == 0x3f)) {
                /* Caching page.  */
                p[0] = 8;
                p[1] = 0x12;
                p[2] = 4; /* WCE */
                p += 19;
            }
            if ((page == 0x3f || page == 0x2a)
                    && (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM)) {
                /* CD Capabilities and Mechanical Status page. */
                p[0] = 0x2a;
                p[1] = 0x14;
                p[2] = 3; // CD-R & CD-RW read
                p[3] = 0; // Writing not supported
                p[4] = 0x7f; /* Audio, composite, digital out,
                                         mode 2 form 1&2, multi session */
                p[5] = 0xff; /* CD DA, DA accurate, RW supported,
                                         RW corrected, C2 errors, ISRC,
                                         UPC, Bar code */
                p[6] = 0x2d | (bdrv_is_locked(s->bdrv)? 2 : 0);
                /* Locking supported, jumper present, eject, tray */
                p[7] = 0; /* no volume & mute control, no
                                      changer */
                p[8] = (50 * 176) >> 8; // 50x read speed
                p[9] = (50 * 176) & 0xff;
                p[10] = 0 >> 8; // No volume
                p[11] = 0 & 0xff;
                p[12] = 2048 >> 8; // 2M buffer
                p[13] = 2048 & 0xff;
                p[14] = (16 * 176) >> 8; // 16x read speed current
                p[15] = (16 * 176) & 0xff;
                p[18] = (16 * 176) >> 8; // 16x write speed
                p[19] = (16 * 176) & 0xff;
                p[20] = (16 * 176) >> 8; // 16x write speed current
                p[21] = (16 * 176) & 0xff;
                p += 21;
            }
            s->buf_len = p - s->buf;
            s->buf[0] = s->buf_len - 4;
            if (s->buf_len > len)
                s->buf_len = len;
        }
        break;
    case 0x1b:
        DPRINTF("Start Stop Unit\n");
	break;
    case 0x1e:
        DPRINTF("Prevent Allow Medium Removal (prevent = %d)\n", buf[4] & 3);
        bdrv_set_locked(s->bdrv, buf[4] & 1);
	break;
    case 0x25:
	DPRINTF("Read Capacity\n");
        /* The normal LEN field for this command is zero.  */
	memset(s->buf, 0, 8);
	bdrv_get_geometry(s->bdrv, &nb_sectors);
	s->buf[0] = (nb_sectors >> 24) & 0xff;
	s->buf[1] = (nb_sectors >> 16) & 0xff;
	s->buf[2] = (nb_sectors >> 8) & 0xff;
	s->buf[3] = nb_sectors & 0xff;
	s->buf[4] = 0;
	s->buf[5] = 0;
        s->buf[6] = s->cluster_size * 2;
	s->buf[7] = 0;
	s->buf_len = 8;
	break;
    case 0x08:
    case 0x28:
        DPRINTF("Read (sector %d, count %d)\n", lba, len);
        s->sector = lba * s->cluster_size;
        s->sector_count = len * s->cluster_size;
        break;
    case 0x0a:
    case 0x2a:
        DPRINTF("Write (sector %d, count %d)\n", lba, len);
        s->sector = lba * s->cluster_size;
        s->sector_count = len * s->cluster_size;
        is_write = 1;
        break;
    case 0x35:
        DPRINTF("Syncronise cache (sector %d, count %d)\n", lba, len);
        bdrv_flush(s->bdrv);
        break;
    case 0x43:
        {
            int start_track, format, msf, toclen;

            msf = buf[1] & 2;
            format = buf[2] & 0xf;
            start_track = buf[6];
            bdrv_get_geometry(s->bdrv, &nb_sectors);
            DPRINTF("Read TOC (track %d format %d msf %d)\n", start_track, format, msf >> 1);
            switch(format) {
            case 0:
                toclen = cdrom_read_toc(nb_sectors, s->buf, msf, start_track);
                break;
            case 1:
                /* multi session : only a single session defined */
                toclen = 12;
                memset(s->buf, 0, 12);
                s->buf[1] = 0x0a;
                s->buf[2] = 0x01;
                s->buf[3] = 0x01;
                break;
            case 2:
                toclen = cdrom_read_toc_raw(nb_sectors, s->buf, msf, start_track);
                break;
            default:
                goto error_cmd;
            }
            if (toclen > 0) {
                if (len > toclen)
                  len = toclen;
                s->buf_len = len;
                break;
            }
        error_cmd:
            DPRINTF("Read TOC error\n");
            goto fail;
        }
    case 0x46:
        DPRINTF("Get Configuration (rt %d, maxlen %d)\n", buf[1] & 3, len);
        memset(s->buf, 0, 8);
        /* ??? This shoud probably return much more information.  For now
           just return the basic header indicating the CD-ROM profile.  */
        s->buf[7] = 8; // CD-ROM
        s->buf_len = 8;
        break;
    case 0x56:
        DPRINTF("Reserve(10)\n");
        if (buf[1] & 3)
            goto fail;
        break;
    case 0x57:
        DPRINTF("Release(10)\n");
        if (buf[1] & 3)
            goto fail;
        break;
    case 0xa0:
        DPRINTF("Report LUNs (len %d)\n", len);
        if (len < 16)
            goto fail;
        memset(s->buf, 0, 16);
        s->buf[3] = 8;
        s->buf_len = 16;
        break;
    default:
	DPRINTF("Unknown SCSI command (%2.2x)\n", buf[0]);
    fail:
        scsi_command_complete(s, SENSE_ILLEGAL_REQUEST);
	return 0;
    }
    if (s->sector_count == 0 && s->buf_len == 0) {
        scsi_command_complete(s, SENSE_NO_SENSE);
    }
    len = s->sector_count * 512 + s->buf_len;
    return is_write ? -len : len;
}

void scsi_disk_destroy(SCSIDevice *s)
{
    bdrv_close(s->bdrv);
    qemu_free(s);
}

SCSIDevice *scsi_disk_init(BlockDriverState *bdrv,
                           scsi_completionfn completion,
                           void *opaque)
{
    SCSIDevice *s;

    s = (SCSIDevice *)qemu_mallocz(sizeof(SCSIDevice));
    s->bdrv = bdrv;
    s->completion = completion;
    s->opaque = opaque;
    if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
        s->cluster_size = 4;
    } else {
        s->cluster_size = 1;
    }

    return s;
}

