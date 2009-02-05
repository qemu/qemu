/*
 * QEMU IDE disk and CD/DVD-ROM Emulator
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
#include "hw.h"
#include "pc.h"
#include "pci.h"
#include "scsi-disk.h"
#include "pcmcia.h"
#include "block.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "ppc_mac.h"
#include "mac_dbdma.h"
#include "sh.h"
#include "dma.h"

/* debug IDE devices */
//#define DEBUG_IDE
//#define DEBUG_IDE_ATAPI
//#define DEBUG_AIO
#define USE_DMA_CDROM

/* Bits of HD_STATUS */
#define ERR_STAT		0x01
#define INDEX_STAT		0x02
#define ECC_STAT		0x04	/* Corrected error */
#define DRQ_STAT		0x08
#define SEEK_STAT		0x10
#define SRV_STAT		0x10
#define WRERR_STAT		0x20
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits for HD_ERROR */
#define MARK_ERR		0x01	/* Bad address mark */
#define TRK0_ERR		0x02	/* couldn't find track 0 */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR			0x08	/* media change request */
#define ID_ERR			0x10	/* ID field not found */
#define MC_ERR			0x20	/* media changed */
#define ECC_ERR			0x40	/* Uncorrectable ECC error */
#define BBD_ERR			0x80	/* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR		0x80	/* new meaning:  CRC error during transfer */

/* Bits of HD_NSECTOR */
#define CD			0x01
#define IO			0x02
#define REL			0x04
#define TAG_MASK		0xf8

#define IDE_CMD_RESET           0x04
#define IDE_CMD_DISABLE_IRQ     0x02

/* ATA/ATAPI Commands pre T13 Spec */
#define WIN_NOP				0x00
/*
 *	0x01->0x02 Reserved
 */
#define CFA_REQ_EXT_ERROR_CODE		0x03 /* CFA Request Extended Error Code */
/*
 *	0x04->0x07 Reserved
 */
#define WIN_SRST			0x08 /* ATAPI soft reset command */
#define WIN_DEVICE_RESET		0x08
/*
 *	0x09->0x0F Reserved
 */
#define WIN_RECAL			0x10
#define WIN_RESTORE			WIN_RECAL
/*
 *	0x10->0x1F Reserved
 */
#define WIN_READ			0x20 /* 28-Bit */
#define WIN_READ_ONCE			0x21 /* 28-Bit without retries */
#define WIN_READ_LONG			0x22 /* 28-Bit */
#define WIN_READ_LONG_ONCE		0x23 /* 28-Bit without retries */
#define WIN_READ_EXT			0x24 /* 48-Bit */
#define WIN_READDMA_EXT			0x25 /* 48-Bit */
#define WIN_READDMA_QUEUED_EXT		0x26 /* 48-Bit */
#define WIN_READ_NATIVE_MAX_EXT		0x27 /* 48-Bit */
/*
 *	0x28
 */
#define WIN_MULTREAD_EXT		0x29 /* 48-Bit */
/*
 *	0x2A->0x2F Reserved
 */
#define WIN_WRITE			0x30 /* 28-Bit */
#define WIN_WRITE_ONCE			0x31 /* 28-Bit without retries */
#define WIN_WRITE_LONG			0x32 /* 28-Bit */
#define WIN_WRITE_LONG_ONCE		0x33 /* 28-Bit without retries */
#define WIN_WRITE_EXT			0x34 /* 48-Bit */
#define WIN_WRITEDMA_EXT		0x35 /* 48-Bit */
#define WIN_WRITEDMA_QUEUED_EXT		0x36 /* 48-Bit */
#define WIN_SET_MAX_EXT			0x37 /* 48-Bit */
#define CFA_WRITE_SECT_WO_ERASE		0x38 /* CFA Write Sectors without erase */
#define WIN_MULTWRITE_EXT		0x39 /* 48-Bit */
/*
 *	0x3A->0x3B Reserved
 */
#define WIN_WRITE_VERIFY		0x3C /* 28-Bit */
/*
 *	0x3D->0x3F Reserved
 */
#define WIN_VERIFY			0x40 /* 28-Bit - Read Verify Sectors */
#define WIN_VERIFY_ONCE			0x41 /* 28-Bit - without retries */
#define WIN_VERIFY_EXT			0x42 /* 48-Bit */
/*
 *	0x43->0x4F Reserved
 */
#define WIN_FORMAT			0x50
/*
 *	0x51->0x5F Reserved
 */
#define WIN_INIT			0x60
/*
 *	0x61->0x5F Reserved
 */
#define WIN_SEEK			0x70 /* 0x70-0x7F Reserved */
#define CFA_TRANSLATE_SECTOR		0x87 /* CFA Translate Sector */
#define WIN_DIAGNOSE			0x90
#define WIN_SPECIFY			0x91 /* set drive geometry translation */
#define WIN_DOWNLOAD_MICROCODE		0x92
#define WIN_STANDBYNOW2			0x94
#define CFA_IDLEIMMEDIATE		0x95 /* force drive to become "ready" */
#define WIN_STANDBY2			0x96
#define WIN_SETIDLE2			0x97
#define WIN_CHECKPOWERMODE2		0x98
#define WIN_SLEEPNOW2			0x99
/*
 *	0x9A VENDOR
 */
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* identify ATAPI device	*/
#define WIN_QUEUED_SERVICE		0xA2
#define WIN_SMART			0xB0 /* self-monitoring and reporting */
#define CFA_ACCESS_METADATA_STORAGE	0xB8
#define CFA_ERASE_SECTORS       	0xC0 /* microdrives implement as NOP */
#define WIN_MULTREAD			0xC4 /* read sectors using multiple mode*/
#define WIN_MULTWRITE			0xC5 /* write sectors using multiple mode */
#define WIN_SETMULT			0xC6 /* enable/disable multiple mode */
#define WIN_READDMA_QUEUED		0xC7 /* read sectors using Queued DMA transfers */
#define WIN_READDMA			0xC8 /* read sectors using DMA transfers */
#define WIN_READDMA_ONCE		0xC9 /* 28-Bit - without retries */
#define WIN_WRITEDMA			0xCA /* write sectors using DMA transfers */
#define WIN_WRITEDMA_ONCE		0xCB /* 28-Bit - without retries */
#define WIN_WRITEDMA_QUEUED		0xCC /* write sectors using Queued DMA transfers */
#define CFA_WRITE_MULTI_WO_ERASE	0xCD /* CFA Write multiple without erase */
#define WIN_GETMEDIASTATUS		0xDA
#define WIN_ACKMEDIACHANGE		0xDB /* ATA-1, ATA-2 vendor */
#define WIN_POSTBOOT			0xDC
#define WIN_PREBOOT			0xDD
#define WIN_DOORLOCK			0xDE /* lock door on removable drives */
#define WIN_DOORUNLOCK			0xDF /* unlock door on removable drives */
#define WIN_STANDBYNOW1			0xE0
#define WIN_IDLEIMMEDIATE		0xE1 /* force drive to become "ready" */
#define WIN_STANDBY             	0xE2 /* Set device in Standby Mode */
#define WIN_SETIDLE1			0xE3
#define WIN_READ_BUFFER			0xE4 /* force read only 1 sector */
#define WIN_CHECKPOWERMODE1		0xE5
#define WIN_SLEEPNOW1			0xE6
#define WIN_FLUSH_CACHE			0xE7
#define WIN_WRITE_BUFFER		0xE8 /* force write only 1 sector */
#define WIN_WRITE_SAME			0xE9 /* read ata-2 to use */
	/* SET_FEATURES 0x22 or 0xDD */
#define WIN_FLUSH_CACHE_EXT		0xEA /* 48-Bit */
#define WIN_IDENTIFY			0xEC /* ask drive to identify itself	*/
#define WIN_MEDIAEJECT			0xED
#define WIN_IDENTIFY_DMA		0xEE /* same as WIN_IDENTIFY, but DMA */
#define WIN_SETFEATURES			0xEF /* set special drive features */
#define EXABYTE_ENABLE_NEST		0xF0
#define IBM_SENSE_CONDITION		0xF0 /* measure disk temperature */
#define WIN_SECURITY_SET_PASS		0xF1
#define WIN_SECURITY_UNLOCK		0xF2
#define WIN_SECURITY_ERASE_PREPARE	0xF3
#define WIN_SECURITY_ERASE_UNIT		0xF4
#define WIN_SECURITY_FREEZE_LOCK	0xF5
#define CFA_WEAR_LEVEL			0xF5 /* microdrives implement as NOP */
#define WIN_SECURITY_DISABLE		0xF6
#define WIN_READ_NATIVE_MAX		0xF8 /* return the native maximum address */
#define WIN_SET_MAX			0xF9
#define DISABLE_SEAGATE			0xFB

/* set to 1 set disable mult support */
#define MAX_MULT_SECTORS 16

#define IDE_DMA_BUF_SECTORS 256

#if (IDE_DMA_BUF_SECTORS < MAX_MULT_SECTORS)
#error "IDE_DMA_BUF_SECTORS must be bigger or equal to MAX_MULT_SECTORS"
#endif

/* ATAPI defines */

#define ATAPI_PACKET_SIZE 12

/* The generic packet command opcodes for CD/DVD Logical Units,
 * From Table 57 of the SFF8090 Ver. 3 (Mt. Fuji) draft standard. */
#define GPCMD_BLANK			    0xa1
#define GPCMD_CLOSE_TRACK		    0x5b
#define GPCMD_FLUSH_CACHE		    0x35
#define GPCMD_FORMAT_UNIT		    0x04
#define GPCMD_GET_CONFIGURATION		    0x46
#define GPCMD_GET_EVENT_STATUS_NOTIFICATION 0x4a
#define GPCMD_GET_PERFORMANCE		    0xac
#define GPCMD_INQUIRY			    0x12
#define GPCMD_LOAD_UNLOAD		    0xa6
#define GPCMD_MECHANISM_STATUS		    0xbd
#define GPCMD_MODE_SELECT_10		    0x55
#define GPCMD_MODE_SENSE_10		    0x5a
#define GPCMD_PAUSE_RESUME		    0x4b
#define GPCMD_PLAY_AUDIO_10		    0x45
#define GPCMD_PLAY_AUDIO_MSF		    0x47
#define GPCMD_PLAY_AUDIO_TI		    0x48
#define GPCMD_PLAY_CD			    0xbc
#define GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL  0x1e
#define GPCMD_READ_10			    0x28
#define GPCMD_READ_12			    0xa8
#define GPCMD_READ_CDVD_CAPACITY	    0x25
#define GPCMD_READ_CD			    0xbe
#define GPCMD_READ_CD_MSF		    0xb9
#define GPCMD_READ_DISC_INFO		    0x51
#define GPCMD_READ_DVD_STRUCTURE	    0xad
#define GPCMD_READ_FORMAT_CAPACITIES	    0x23
#define GPCMD_READ_HEADER		    0x44
#define GPCMD_READ_TRACK_RZONE_INFO	    0x52
#define GPCMD_READ_SUBCHANNEL		    0x42
#define GPCMD_READ_TOC_PMA_ATIP		    0x43
#define GPCMD_REPAIR_RZONE_TRACK	    0x58
#define GPCMD_REPORT_KEY		    0xa4
#define GPCMD_REQUEST_SENSE		    0x03
#define GPCMD_RESERVE_RZONE_TRACK	    0x53
#define GPCMD_SCAN			    0xba
#define GPCMD_SEEK			    0x2b
#define GPCMD_SEND_DVD_STRUCTURE	    0xad
#define GPCMD_SEND_EVENT		    0xa2
#define GPCMD_SEND_KEY			    0xa3
#define GPCMD_SEND_OPC			    0x54
#define GPCMD_SET_READ_AHEAD		    0xa7
#define GPCMD_SET_STREAMING		    0xb6
#define GPCMD_START_STOP_UNIT		    0x1b
#define GPCMD_STOP_PLAY_SCAN		    0x4e
#define GPCMD_TEST_UNIT_READY		    0x00
#define GPCMD_VERIFY_10			    0x2f
#define GPCMD_WRITE_10			    0x2a
#define GPCMD_WRITE_AND_VERIFY_10	    0x2e
/* This is listed as optional in ATAPI 2.6, but is (curiously)
 * missing from Mt. Fuji, Table 57.  It _is_ mentioned in Mt. Fuji
 * Table 377 as an MMC command for SCSi devices though...  Most ATAPI
 * drives support it. */
#define GPCMD_SET_SPEED			    0xbb
/* This seems to be a SCSI specific CD-ROM opcode
 * to play data at track/index */
#define GPCMD_PLAYAUDIO_TI		    0x48
/*
 * From MS Media Status Notification Support Specification. For
 * older drives only.
 */
#define GPCMD_GET_MEDIA_STATUS		    0xda
#define GPCMD_MODE_SENSE_6		    0x1a

/* Mode page codes for mode sense/set */
#define GPMODE_R_W_ERROR_PAGE		0x01
#define GPMODE_WRITE_PARMS_PAGE		0x05
#define GPMODE_AUDIO_CTL_PAGE		0x0e
#define GPMODE_POWER_PAGE		0x1a
#define GPMODE_FAULT_FAIL_PAGE		0x1c
#define GPMODE_TO_PROTECT_PAGE		0x1d
#define GPMODE_CAPABILITIES_PAGE	0x2a
#define GPMODE_ALL_PAGES		0x3f
/* Not in Mt. Fuji, but in ATAPI 2.6 -- depricated now in favor
 * of MODE_SENSE_POWER_PAGE */
#define GPMODE_CDROM_PAGE		0x0d

/*
 * Based on values from <linux/cdrom.h> but extending CD_MINS
 * to the maximum common size allowed by the Orange's Book ATIP
 *
 * 90 and 99 min CDs are also available but using them as the
 * upper limit reduces the effectiveness of the heuristic to
 * detect DVDs burned to less than 25% of their maximum capacity
 */

/* Some generally useful CD-ROM information */
#define CD_MINS                       80 /* max. minutes per CD */
#define CD_SECS                       60 /* seconds per minute */
#define CD_FRAMES                     75 /* frames per second */
#define CD_FRAMESIZE                2048 /* bytes per frame, "cooked" mode */
#define CD_MAX_BYTES       (CD_MINS * CD_SECS * CD_FRAMES * CD_FRAMESIZE)
#define CD_MAX_SECTORS     (CD_MAX_BYTES / 512)

/*
 * The MMC values are not IDE specific and might need to be moved
 * to a common header if they are also needed for the SCSI emulation
 */

/* Profile list from MMC-6 revision 1 table 91 */
#define MMC_PROFILE_NONE                0x0000
#define MMC_PROFILE_CD_ROM              0x0008
#define MMC_PROFILE_CD_R                0x0009
#define MMC_PROFILE_CD_RW               0x000A
#define MMC_PROFILE_DVD_ROM             0x0010
#define MMC_PROFILE_DVD_R_SR            0x0011
#define MMC_PROFILE_DVD_RAM             0x0012
#define MMC_PROFILE_DVD_RW_RO           0x0013
#define MMC_PROFILE_DVD_RW_SR           0x0014
#define MMC_PROFILE_DVD_R_DL_SR         0x0015
#define MMC_PROFILE_DVD_R_DL_JR         0x0016
#define MMC_PROFILE_DVD_RW_DL           0x0017
#define MMC_PROFILE_DVD_DDR             0x0018
#define MMC_PROFILE_DVD_PLUS_RW         0x001A
#define MMC_PROFILE_DVD_PLUS_R          0x001B
#define MMC_PROFILE_DVD_PLUS_RW_DL      0x002A
#define MMC_PROFILE_DVD_PLUS_R_DL       0x002B
#define MMC_PROFILE_BD_ROM              0x0040
#define MMC_PROFILE_BD_R_SRM            0x0041
#define MMC_PROFILE_BD_R_RRM            0x0042
#define MMC_PROFILE_BD_RE               0x0043
#define MMC_PROFILE_HDDVD_ROM           0x0050
#define MMC_PROFILE_HDDVD_R             0x0051
#define MMC_PROFILE_HDDVD_RAM           0x0052
#define MMC_PROFILE_HDDVD_RW            0x0053
#define MMC_PROFILE_HDDVD_R_DL          0x0058
#define MMC_PROFILE_HDDVD_RW_DL         0x005A
#define MMC_PROFILE_INVALID             0xFFFF

#define ATAPI_INT_REASON_CD             0x01 /* 0 = data transfer */
#define ATAPI_INT_REASON_IO             0x02 /* 1 = transfer to the host */
#define ATAPI_INT_REASON_REL            0x04
#define ATAPI_INT_REASON_TAG            0xf8

/* same constants as bochs */
#define ASC_ILLEGAL_OPCODE                   0x20
#define ASC_LOGICAL_BLOCK_OOR                0x21
#define ASC_INV_FIELD_IN_CMD_PACKET          0x24
#define ASC_MEDIUM_MAY_HAVE_CHANGED          0x28
#define ASC_INCOMPATIBLE_FORMAT              0x30
#define ASC_MEDIUM_NOT_PRESENT               0x3a
#define ASC_SAVING_PARAMETERS_NOT_SUPPORTED  0x39

#define CFA_NO_ERROR            0x00
#define CFA_MISC_ERROR          0x09
#define CFA_INVALID_COMMAND     0x20
#define CFA_INVALID_ADDRESS     0x21
#define CFA_ADDRESS_OVERFLOW    0x2f

#define SENSE_NONE            0
#define SENSE_NOT_READY       2
#define SENSE_ILLEGAL_REQUEST 5
#define SENSE_UNIT_ATTENTION  6

struct IDEState;

typedef void EndTransferFunc(struct IDEState *);

/* NOTE: IDEState represents in fact one drive */
typedef struct IDEState {
    /* ide config */
    int is_cdrom;
    int is_cf;
    int cylinders, heads, sectors;
    int64_t nb_sectors;
    int mult_sectors;
    int identify_set;
    uint16_t identify_data[256];
    qemu_irq irq;
    PCIDevice *pci_dev;
    struct BMDMAState *bmdma;
    int drive_serial;
    char drive_serial_str[21];
    /* ide regs */
    uint8_t feature;
    uint8_t error;
    uint32_t nsector;
    uint8_t sector;
    uint8_t lcyl;
    uint8_t hcyl;
    /* other part of tf for lba48 support */
    uint8_t hob_feature;
    uint8_t hob_nsector;
    uint8_t hob_sector;
    uint8_t hob_lcyl;
    uint8_t hob_hcyl;

    uint8_t select;
    uint8_t status;

    /* 0x3f6 command, only meaningful for drive 0 */
    uint8_t cmd;
    /* set for lba48 access */
    uint8_t lba48;
    /* depends on bit 4 in select, only meaningful for drive 0 */
    struct IDEState *cur_drive;
    BlockDriverState *bs;
    /* ATAPI specific */
    uint8_t sense_key;
    uint8_t asc;
    int packet_transfer_size;
    int elementary_transfer_size;
    int io_buffer_index;
    int lba;
    int cd_sector_size;
    int atapi_dma; /* true if dma is requested for the packet cmd */
    /* ATA DMA state */
    int io_buffer_size;
    QEMUSGList sg;
    /* PIO transfer handling */
    int req_nb_sectors; /* number of sectors per interrupt */
    EndTransferFunc *end_transfer_func;
    uint8_t *data_ptr;
    uint8_t *data_end;
    uint8_t *io_buffer;
    QEMUTimer *sector_write_timer; /* only used for win2k install hack */
    uint32_t irq_count; /* counts IRQs when using win2k install hack */
    /* CF-ATA extended error */
    uint8_t ext_error;
    /* CF-ATA metadata storage */
    uint32_t mdata_size;
    uint8_t *mdata_storage;
    int media_changed;
    /* for pmac */
    int is_read;
} IDEState;

/* XXX: DVDs that could fit on a CD will be reported as a CD */
static inline int media_present(IDEState *s)
{
    return (s->nb_sectors > 0);
}

static inline int media_is_dvd(IDEState *s)
{
    return (media_present(s) && s->nb_sectors > CD_MAX_SECTORS);
}

static inline int media_is_cd(IDEState *s)
{
    return (media_present(s) && s->nb_sectors <= CD_MAX_SECTORS);
}

#define BM_STATUS_DMAING 0x01
#define BM_STATUS_ERROR  0x02
#define BM_STATUS_INT    0x04
#define BM_STATUS_DMA_RETRY  0x08
#define BM_STATUS_PIO_RETRY  0x10

#define BM_CMD_START     0x01
#define BM_CMD_READ      0x08

#define IDE_TYPE_PIIX3   0
#define IDE_TYPE_CMD646  1
#define IDE_TYPE_PIIX4   2

/* CMD646 specific */
#define MRDMODE		0x71
#define   MRDMODE_INTR_CH0	0x04
#define   MRDMODE_INTR_CH1	0x08
#define   MRDMODE_BLK_CH0	0x10
#define   MRDMODE_BLK_CH1	0x20
#define UDIDETCR0	0x73
#define UDIDETCR1	0x7B

typedef struct BMDMAState {
    uint8_t cmd;
    uint8_t status;
    uint32_t addr;

    struct PCIIDEState *pci_dev;
    /* current transfer state */
    uint32_t cur_addr;
    uint32_t cur_prd_last;
    uint32_t cur_prd_addr;
    uint32_t cur_prd_len;
    IDEState *ide_if;
    BlockDriverCompletionFunc *dma_cb;
    BlockDriverAIOCB *aiocb;
    int64_t sector_num;
    uint32_t nsector;
} BMDMAState;

typedef struct PCIIDEState {
    PCIDevice dev;
    IDEState ide_if[4];
    BMDMAState bmdma[2];
    int type; /* see IDE_TYPE_xxx */
} PCIIDEState;

static void ide_dma_start(IDEState *s, BlockDriverCompletionFunc *dma_cb);
static void ide_dma_restart(IDEState *s);
static void ide_atapi_cmd_read_dma_cb(void *opaque, int ret);

static void padstr(char *str, const char *src, int len)
{
    int i, v;
    for(i = 0; i < len; i++) {
        if (*src)
            v = *src++;
        else
            v = ' ';
        str[i^1] = v;
    }
}

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

static void put_le16(uint16_t *p, unsigned int v)
{
    *p = cpu_to_le16(v);
}

static void ide_identify(IDEState *s)
{
    uint16_t *p;
    unsigned int oldsize;

    if (s->identify_set) {
	memcpy(s->io_buffer, s->identify_data, sizeof(s->identify_data));
	return;
    }

    memset(s->io_buffer, 0, 512);
    p = (uint16_t *)s->io_buffer;
    put_le16(p + 0, 0x0040);
    put_le16(p + 1, s->cylinders);
    put_le16(p + 3, s->heads);
    put_le16(p + 4, 512 * s->sectors); /* XXX: retired, remove ? */
    put_le16(p + 5, 512); /* XXX: retired, remove ? */
    put_le16(p + 6, s->sectors);
    padstr((char *)(p + 10), s->drive_serial_str, 20); /* serial number */
    put_le16(p + 20, 3); /* XXX: retired, remove ? */
    put_le16(p + 21, 512); /* cache size in sectors */
    put_le16(p + 22, 4); /* ecc bytes */
    padstr((char *)(p + 23), QEMU_VERSION, 8); /* firmware version */
    padstr((char *)(p + 27), "QEMU HARDDISK", 40); /* model */
#if MAX_MULT_SECTORS > 1
    put_le16(p + 47, 0x8000 | MAX_MULT_SECTORS);
#endif
    put_le16(p + 48, 1); /* dword I/O */
    put_le16(p + 49, (1 << 11) | (1 << 9) | (1 << 8)); /* DMA and LBA supported */
    put_le16(p + 51, 0x200); /* PIO transfer cycle */
    put_le16(p + 52, 0x200); /* DMA transfer cycle */
    put_le16(p + 53, 1 | (1 << 1) | (1 << 2)); /* words 54-58,64-70,88 are valid */
    put_le16(p + 54, s->cylinders);
    put_le16(p + 55, s->heads);
    put_le16(p + 56, s->sectors);
    oldsize = s->cylinders * s->heads * s->sectors;
    put_le16(p + 57, oldsize);
    put_le16(p + 58, oldsize >> 16);
    if (s->mult_sectors)
        put_le16(p + 59, 0x100 | s->mult_sectors);
    put_le16(p + 60, s->nb_sectors);
    put_le16(p + 61, s->nb_sectors >> 16);
    put_le16(p + 62, 0x07); /* single word dma0-2 supported */
    put_le16(p + 63, 0x07); /* mdma0-2 supported */
    put_le16(p + 65, 120);
    put_le16(p + 66, 120);
    put_le16(p + 67, 120);
    put_le16(p + 68, 120);
    put_le16(p + 80, 0xf0); /* ata3 -> ata6 supported */
    put_le16(p + 81, 0x16); /* conforms to ata5 */
    put_le16(p + 82, (1 << 14));
    /* 13=flush_cache_ext,12=flush_cache,10=lba48 */
    put_le16(p + 83, (1 << 14) | (1 << 13) | (1 <<12) | (1 << 10));
    put_le16(p + 84, (1 << 14));
    put_le16(p + 85, (1 << 14));
    /* 13=flush_cache_ext,12=flush_cache,10=lba48 */
    put_le16(p + 86, (1 << 14) | (1 << 13) | (1 <<12) | (1 << 10));
    put_le16(p + 87, (1 << 14));
    put_le16(p + 88, 0x3f | (1 << 13)); /* udma5 set and supported */
    put_le16(p + 93, 1 | (1 << 14) | 0x2000);
    put_le16(p + 100, s->nb_sectors);
    put_le16(p + 101, s->nb_sectors >> 16);
    put_le16(p + 102, s->nb_sectors >> 32);
    put_le16(p + 103, s->nb_sectors >> 48);

    memcpy(s->identify_data, p, sizeof(s->identify_data));
    s->identify_set = 1;
}

static void ide_atapi_identify(IDEState *s)
{
    uint16_t *p;

    if (s->identify_set) {
	memcpy(s->io_buffer, s->identify_data, sizeof(s->identify_data));
	return;
    }

    memset(s->io_buffer, 0, 512);
    p = (uint16_t *)s->io_buffer;
    /* Removable CDROM, 50us response, 12 byte packets */
    put_le16(p + 0, (2 << 14) | (5 << 8) | (1 << 7) | (2 << 5) | (0 << 0));
    padstr((char *)(p + 10), s->drive_serial_str, 20); /* serial number */
    put_le16(p + 20, 3); /* buffer type */
    put_le16(p + 21, 512); /* cache size in sectors */
    put_le16(p + 22, 4); /* ecc bytes */
    padstr((char *)(p + 23), QEMU_VERSION, 8); /* firmware version */
    padstr((char *)(p + 27), "QEMU DVD-ROM", 40); /* model */
    put_le16(p + 48, 1); /* dword I/O (XXX: should not be set on CDROM) */
#ifdef USE_DMA_CDROM
    put_le16(p + 49, 1 << 9 | 1 << 8); /* DMA and LBA supported */
    put_le16(p + 53, 7); /* words 64-70, 54-58, 88 valid */
    put_le16(p + 62, 7);  /* single word dma0-2 supported */
    put_le16(p + 63, 7);  /* mdma0-2 supported */
    put_le16(p + 64, 0x3f); /* PIO modes supported */
#else
    put_le16(p + 49, 1 << 9); /* LBA supported, no DMA */
    put_le16(p + 53, 3); /* words 64-70, 54-58 valid */
    put_le16(p + 63, 0x103); /* DMA modes XXX: may be incorrect */
    put_le16(p + 64, 1); /* PIO modes */
#endif
    put_le16(p + 65, 0xb4); /* minimum DMA multiword tx cycle time */
    put_le16(p + 66, 0xb4); /* recommended DMA multiword tx cycle time */
    put_le16(p + 67, 0x12c); /* minimum PIO cycle time without flow control */
    put_le16(p + 68, 0xb4); /* minimum PIO cycle time with IORDY flow control */

    put_le16(p + 71, 30); /* in ns */
    put_le16(p + 72, 30); /* in ns */

    put_le16(p + 80, 0x1e); /* support up to ATA/ATAPI-4 */
#ifdef USE_DMA_CDROM
    put_le16(p + 88, 0x3f | (1 << 13)); /* udma5 set and supported */
#endif
    memcpy(s->identify_data, p, sizeof(s->identify_data));
    s->identify_set = 1;
}

static void ide_cfata_identify(IDEState *s)
{
    uint16_t *p;
    uint32_t cur_sec;

    p = (uint16_t *) s->identify_data;
    if (s->identify_set)
        goto fill_buffer;

    memset(p, 0, sizeof(s->identify_data));

    cur_sec = s->cylinders * s->heads * s->sectors;

    put_le16(p + 0, 0x848a);			/* CF Storage Card signature */
    put_le16(p + 1, s->cylinders);		/* Default cylinders */
    put_le16(p + 3, s->heads);			/* Default heads */
    put_le16(p + 6, s->sectors);		/* Default sectors per track */
    put_le16(p + 7, s->nb_sectors >> 16);	/* Sectors per card */
    put_le16(p + 8, s->nb_sectors);		/* Sectors per card */
    padstr((char *)(p + 10), s->drive_serial_str, 20); /* serial number */
    put_le16(p + 22, 0x0004);			/* ECC bytes */
    padstr((char *) (p + 23), QEMU_VERSION, 8);	/* Firmware Revision */
    padstr((char *) (p + 27), "QEMU MICRODRIVE", 40);/* Model number */
#if MAX_MULT_SECTORS > 1
    put_le16(p + 47, 0x8000 | MAX_MULT_SECTORS);
#else
    put_le16(p + 47, 0x0000);
#endif
    put_le16(p + 49, 0x0f00);			/* Capabilities */
    put_le16(p + 51, 0x0002);			/* PIO cycle timing mode */
    put_le16(p + 52, 0x0001);			/* DMA cycle timing mode */
    put_le16(p + 53, 0x0003);			/* Translation params valid */
    put_le16(p + 54, s->cylinders);		/* Current cylinders */
    put_le16(p + 55, s->heads);			/* Current heads */
    put_le16(p + 56, s->sectors);		/* Current sectors */
    put_le16(p + 57, cur_sec);			/* Current capacity */
    put_le16(p + 58, cur_sec >> 16);		/* Current capacity */
    if (s->mult_sectors)			/* Multiple sector setting */
        put_le16(p + 59, 0x100 | s->mult_sectors);
    put_le16(p + 60, s->nb_sectors);		/* Total LBA sectors */
    put_le16(p + 61, s->nb_sectors >> 16);	/* Total LBA sectors */
    put_le16(p + 63, 0x0203);			/* Multiword DMA capability */
    put_le16(p + 64, 0x0001);			/* Flow Control PIO support */
    put_le16(p + 65, 0x0096);			/* Min. Multiword DMA cycle */
    put_le16(p + 66, 0x0096);			/* Rec. Multiword DMA cycle */
    put_le16(p + 68, 0x00b4);			/* Min. PIO cycle time */
    put_le16(p + 82, 0x400c);			/* Command Set supported */
    put_le16(p + 83, 0x7068);			/* Command Set supported */
    put_le16(p + 84, 0x4000);			/* Features supported */
    put_le16(p + 85, 0x000c);			/* Command Set enabled */
    put_le16(p + 86, 0x7044);			/* Command Set enabled */
    put_le16(p + 87, 0x4000);			/* Features enabled */
    put_le16(p + 91, 0x4060);			/* Current APM level */
    put_le16(p + 129, 0x0002);			/* Current features option */
    put_le16(p + 130, 0x0005);			/* Reassigned sectors */
    put_le16(p + 131, 0x0001);			/* Initial power mode */
    put_le16(p + 132, 0x0000);			/* User signature */
    put_le16(p + 160, 0x8100);			/* Power requirement */
    put_le16(p + 161, 0x8001);			/* CF command set */

    s->identify_set = 1;

fill_buffer:
    memcpy(s->io_buffer, p, sizeof(s->identify_data));
}

static void ide_set_signature(IDEState *s)
{
    s->select &= 0xf0; /* clear head */
    /* put signature */
    s->nsector = 1;
    s->sector = 1;
    if (s->is_cdrom) {
        s->lcyl = 0x14;
        s->hcyl = 0xeb;
    } else if (s->bs) {
        s->lcyl = 0;
        s->hcyl = 0;
    } else {
        s->lcyl = 0xff;
        s->hcyl = 0xff;
    }
}

static inline void ide_abort_command(IDEState *s)
{
    s->status = READY_STAT | ERR_STAT;
    s->error = ABRT_ERR;
}

static inline void ide_dma_submit_check(IDEState *s,
          BlockDriverCompletionFunc *dma_cb, BMDMAState *bm)
{
    if (bm->aiocb)
	return;
    dma_cb(bm, -1);
}

static inline void ide_set_irq(IDEState *s)
{
    BMDMAState *bm = s->bmdma;
    if (!(s->cmd & IDE_CMD_DISABLE_IRQ)) {
        if (bm) {
            bm->status |= BM_STATUS_INT;
        }
        qemu_irq_raise(s->irq);
    }
}

/* prepare data transfer and tell what to do after */
static void ide_transfer_start(IDEState *s, uint8_t *buf, int size,
                               EndTransferFunc *end_transfer_func)
{
    s->end_transfer_func = end_transfer_func;
    s->data_ptr = buf;
    s->data_end = buf + size;
    if (!(s->status & ERR_STAT))
        s->status |= DRQ_STAT;
}

static void ide_transfer_stop(IDEState *s)
{
    s->end_transfer_func = ide_transfer_stop;
    s->data_ptr = s->io_buffer;
    s->data_end = s->io_buffer;
    s->status &= ~DRQ_STAT;
}

static int64_t ide_get_sector(IDEState *s)
{
    int64_t sector_num;
    if (s->select & 0x40) {
        /* lba */
	if (!s->lba48) {
	    sector_num = ((s->select & 0x0f) << 24) | (s->hcyl << 16) |
		(s->lcyl << 8) | s->sector;
	} else {
	    sector_num = ((int64_t)s->hob_hcyl << 40) |
		((int64_t) s->hob_lcyl << 32) |
		((int64_t) s->hob_sector << 24) |
		((int64_t) s->hcyl << 16) |
		((int64_t) s->lcyl << 8) | s->sector;
	}
    } else {
        sector_num = ((s->hcyl << 8) | s->lcyl) * s->heads * s->sectors +
            (s->select & 0x0f) * s->sectors + (s->sector - 1);
    }
    return sector_num;
}

static void ide_set_sector(IDEState *s, int64_t sector_num)
{
    unsigned int cyl, r;
    if (s->select & 0x40) {
	if (!s->lba48) {
            s->select = (s->select & 0xf0) | (sector_num >> 24);
            s->hcyl = (sector_num >> 16);
            s->lcyl = (sector_num >> 8);
            s->sector = (sector_num);
	} else {
	    s->sector = sector_num;
	    s->lcyl = sector_num >> 8;
	    s->hcyl = sector_num >> 16;
	    s->hob_sector = sector_num >> 24;
	    s->hob_lcyl = sector_num >> 32;
	    s->hob_hcyl = sector_num >> 40;
	}
    } else {
        cyl = sector_num / (s->heads * s->sectors);
        r = sector_num % (s->heads * s->sectors);
        s->hcyl = cyl >> 8;
        s->lcyl = cyl;
        s->select = (s->select & 0xf0) | ((r / s->sectors) & 0x0f);
        s->sector = (r % s->sectors) + 1;
    }
}

static void ide_rw_error(IDEState *s) {
    ide_abort_command(s);
    ide_set_irq(s);
}

static void ide_sector_read(IDEState *s)
{
    int64_t sector_num;
    int ret, n;

    s->status = READY_STAT | SEEK_STAT;
    s->error = 0; /* not needed by IDE spec, but needed by Windows */
    sector_num = ide_get_sector(s);
    n = s->nsector;
    if (n == 0) {
        /* no more sector to read from disk */
        ide_transfer_stop(s);
    } else {
#if defined(DEBUG_IDE)
        printf("read sector=%" PRId64 "\n", sector_num);
#endif
        if (n > s->req_nb_sectors)
            n = s->req_nb_sectors;
        ret = bdrv_read(s->bs, sector_num, s->io_buffer, n);
        if (ret != 0) {
            ide_rw_error(s);
            return;
        }
        ide_transfer_start(s, s->io_buffer, 512 * n, ide_sector_read);
        ide_set_irq(s);
        ide_set_sector(s, sector_num + n);
        s->nsector -= n;
    }
}


/* return 0 if buffer completed */
static int dma_buf_prepare(BMDMAState *bm, int is_write)
{
    IDEState *s = bm->ide_if;
    struct {
        uint32_t addr;
        uint32_t size;
    } prd;
    int l, len;

    qemu_sglist_init(&s->sg, s->nsector / (TARGET_PAGE_SIZE/512) + 1);
    s->io_buffer_size = 0;
    for(;;) {
        if (bm->cur_prd_len == 0) {
            /* end of table (with a fail safe of one page) */
            if (bm->cur_prd_last ||
                (bm->cur_addr - bm->addr) >= 4096)
                return s->io_buffer_size != 0;
            cpu_physical_memory_read(bm->cur_addr, (uint8_t *)&prd, 8);
            bm->cur_addr += 8;
            prd.addr = le32_to_cpu(prd.addr);
            prd.size = le32_to_cpu(prd.size);
            len = prd.size & 0xfffe;
            if (len == 0)
                len = 0x10000;
            bm->cur_prd_len = len;
            bm->cur_prd_addr = prd.addr;
            bm->cur_prd_last = (prd.size & 0x80000000);
        }
        l = bm->cur_prd_len;
        if (l > 0) {
            qemu_sglist_add(&s->sg, bm->cur_prd_addr, l);
            bm->cur_prd_addr += l;
            bm->cur_prd_len -= l;
            s->io_buffer_size += l;
        }
    }
    return 1;
}

static void dma_buf_commit(IDEState *s, int is_write)
{
    qemu_sglist_destroy(&s->sg);
}

static void ide_dma_error(IDEState *s)
{
    ide_transfer_stop(s);
    s->error = ABRT_ERR;
    s->status = READY_STAT | ERR_STAT;
    ide_set_irq(s);
}

static int ide_handle_write_error(IDEState *s, int error, int op)
{
    BlockInterfaceErrorAction action = drive_get_onerror(s->bs);

    if (action == BLOCK_ERR_IGNORE)
        return 0;

    if ((error == ENOSPC && action == BLOCK_ERR_STOP_ENOSPC)
            || action == BLOCK_ERR_STOP_ANY) {
        s->bmdma->ide_if = s;
        s->bmdma->status |= op;
        vm_stop(0);
    } else {
        if (op == BM_STATUS_DMA_RETRY) {
            dma_buf_commit(s, 0);
            ide_dma_error(s);
        } else {
            ide_rw_error(s);
        }
    }

    return 1;
}

/* return 0 if buffer completed */
static int dma_buf_rw(BMDMAState *bm, int is_write)
{
    IDEState *s = bm->ide_if;
    struct {
        uint32_t addr;
        uint32_t size;
    } prd;
    int l, len;

    for(;;) {
        l = s->io_buffer_size - s->io_buffer_index;
        if (l <= 0)
            break;
        if (bm->cur_prd_len == 0) {
            /* end of table (with a fail safe of one page) */
            if (bm->cur_prd_last ||
                (bm->cur_addr - bm->addr) >= 4096)
                return 0;
            cpu_physical_memory_read(bm->cur_addr, (uint8_t *)&prd, 8);
            bm->cur_addr += 8;
            prd.addr = le32_to_cpu(prd.addr);
            prd.size = le32_to_cpu(prd.size);
            len = prd.size & 0xfffe;
            if (len == 0)
                len = 0x10000;
            bm->cur_prd_len = len;
            bm->cur_prd_addr = prd.addr;
            bm->cur_prd_last = (prd.size & 0x80000000);
        }
        if (l > bm->cur_prd_len)
            l = bm->cur_prd_len;
        if (l > 0) {
            if (is_write) {
                cpu_physical_memory_write(bm->cur_prd_addr,
                                          s->io_buffer + s->io_buffer_index, l);
            } else {
                cpu_physical_memory_read(bm->cur_prd_addr,
                                          s->io_buffer + s->io_buffer_index, l);
            }
            bm->cur_prd_addr += l;
            bm->cur_prd_len -= l;
            s->io_buffer_index += l;
        }
    }
    return 1;
}

static void ide_read_dma_cb(void *opaque, int ret)
{
    BMDMAState *bm = opaque;
    IDEState *s = bm->ide_if;
    int n;
    int64_t sector_num;

    if (ret < 0) {
        dma_buf_commit(s, 1);
	ide_dma_error(s);
	return;
    }

    n = s->io_buffer_size >> 9;
    sector_num = ide_get_sector(s);
    if (n > 0) {
        dma_buf_commit(s, 1);
        sector_num += n;
        ide_set_sector(s, sector_num);
        s->nsector -= n;
    }

    /* end of transfer ? */
    if (s->nsector == 0) {
        s->status = READY_STAT | SEEK_STAT;
        ide_set_irq(s);
    eot:
        bm->status &= ~BM_STATUS_DMAING;
        bm->status |= BM_STATUS_INT;
        bm->dma_cb = NULL;
        bm->ide_if = NULL;
        bm->aiocb = NULL;
        return;
    }

    /* launch next transfer */
    n = s->nsector;
    s->io_buffer_index = 0;
    s->io_buffer_size = n * 512;
    if (dma_buf_prepare(bm, 1) == 0)
        goto eot;
#ifdef DEBUG_AIO
    printf("aio_read: sector_num=%" PRId64 " n=%d\n", sector_num, n);
#endif
    bm->aiocb = dma_bdrv_read(s->bs, &s->sg, sector_num, ide_read_dma_cb, bm);
    ide_dma_submit_check(s, ide_read_dma_cb, bm);
}

static void ide_sector_read_dma(IDEState *s)
{
    s->status = READY_STAT | SEEK_STAT | DRQ_STAT | BUSY_STAT;
    s->io_buffer_index = 0;
    s->io_buffer_size = 0;
    s->is_read = 1;
    ide_dma_start(s, ide_read_dma_cb);
}

static void ide_sector_write_timer_cb(void *opaque)
{
    IDEState *s = opaque;
    ide_set_irq(s);
}

static void ide_sector_write(IDEState *s)
{
    int64_t sector_num;
    int ret, n, n1;

    s->status = READY_STAT | SEEK_STAT;
    sector_num = ide_get_sector(s);
#if defined(DEBUG_IDE)
    printf("write sector=%" PRId64 "\n", sector_num);
#endif
    n = s->nsector;
    if (n > s->req_nb_sectors)
        n = s->req_nb_sectors;
    ret = bdrv_write(s->bs, sector_num, s->io_buffer, n);

    if (ret != 0) {
        if (ide_handle_write_error(s, -ret, BM_STATUS_PIO_RETRY))
            return;
    }

    s->nsector -= n;
    if (s->nsector == 0) {
        /* no more sectors to write */
        ide_transfer_stop(s);
    } else {
        n1 = s->nsector;
        if (n1 > s->req_nb_sectors)
            n1 = s->req_nb_sectors;
        ide_transfer_start(s, s->io_buffer, 512 * n1, ide_sector_write);
    }
    ide_set_sector(s, sector_num + n);

#ifdef TARGET_I386
    if (win2k_install_hack && ((++s->irq_count % 16) == 0)) {
        /* It seems there is a bug in the Windows 2000 installer HDD
           IDE driver which fills the disk with empty logs when the
           IDE write IRQ comes too early. This hack tries to correct
           that at the expense of slower write performances. Use this
           option _only_ to install Windows 2000. You must disable it
           for normal use. */
        qemu_mod_timer(s->sector_write_timer, 
                       qemu_get_clock(vm_clock) + (ticks_per_sec / 1000));
    } else 
#endif
    {
        ide_set_irq(s);
    }
}

static void ide_dma_restart_cb(void *opaque, int running, int reason)
{
    BMDMAState *bm = opaque;
    if (!running)
        return;
    if (bm->status & BM_STATUS_DMA_RETRY) {
        bm->status &= ~BM_STATUS_DMA_RETRY;
        ide_dma_restart(bm->ide_if);
    } else if (bm->status & BM_STATUS_PIO_RETRY) {
        bm->status &= ~BM_STATUS_PIO_RETRY;
        ide_sector_write(bm->ide_if);
    }
}

static void ide_write_dma_cb(void *opaque, int ret)
{
    BMDMAState *bm = opaque;
    IDEState *s = bm->ide_if;
    int n;
    int64_t sector_num;

    if (ret < 0) {
        if (ide_handle_write_error(s, -ret,  BM_STATUS_DMA_RETRY))
            return;
    }

    n = s->io_buffer_size >> 9;
    sector_num = ide_get_sector(s);
    if (n > 0) {
        dma_buf_commit(s, 0);
        sector_num += n;
        ide_set_sector(s, sector_num);
        s->nsector -= n;
    }

    /* end of transfer ? */
    if (s->nsector == 0) {
        s->status = READY_STAT | SEEK_STAT;
        ide_set_irq(s);
    eot:
        bm->status &= ~BM_STATUS_DMAING;
        bm->status |= BM_STATUS_INT;
        bm->dma_cb = NULL;
        bm->ide_if = NULL;
        bm->aiocb = NULL;
        return;
    }

    n = s->nsector;
    s->io_buffer_size = n * 512;
    /* launch next transfer */
    if (dma_buf_prepare(bm, 0) == 0)
        goto eot;
#ifdef DEBUG_AIO
    printf("aio_write: sector_num=%" PRId64 " n=%d\n", sector_num, n);
#endif
    bm->aiocb = dma_bdrv_write(s->bs, &s->sg, sector_num, ide_write_dma_cb, bm);
    ide_dma_submit_check(s, ide_write_dma_cb, bm);
}

static void ide_sector_write_dma(IDEState *s)
{
    s->status = READY_STAT | SEEK_STAT | DRQ_STAT | BUSY_STAT;
    s->io_buffer_index = 0;
    s->io_buffer_size = 0;
    s->is_read = 0;
    ide_dma_start(s, ide_write_dma_cb);
}

static void ide_atapi_cmd_ok(IDEState *s)
{
    s->error = 0;
    s->status = READY_STAT | SEEK_STAT;
    s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    ide_set_irq(s);
}

static void ide_atapi_cmd_error(IDEState *s, int sense_key, int asc)
{
#ifdef DEBUG_IDE_ATAPI
    printf("atapi_cmd_error: sense=0x%x asc=0x%x\n", sense_key, asc);
#endif
    s->error = sense_key << 4;
    s->status = READY_STAT | ERR_STAT;
    s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    s->sense_key = sense_key;
    s->asc = asc;
    ide_set_irq(s);
}

static void ide_atapi_cmd_check_status(IDEState *s)
{
#ifdef DEBUG_IDE_ATAPI
    printf("atapi_cmd_check_status\n");
#endif
    s->error = MC_ERR | (SENSE_UNIT_ATTENTION << 4);
    s->status = ERR_STAT;
    s->nsector = 0;
    ide_set_irq(s);
}

static inline void cpu_to_ube16(uint8_t *buf, int val)
{
    buf[0] = val >> 8;
    buf[1] = val;
}

static inline void cpu_to_ube32(uint8_t *buf, unsigned int val)
{
    buf[0] = val >> 24;
    buf[1] = val >> 16;
    buf[2] = val >> 8;
    buf[3] = val;
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

static void ide_atapi_io_error(IDEState *s, int ret)
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
static void ide_atapi_cmd_reply_end(IDEState *s)
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
        ide_set_irq(s);
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
            ide_transfer_start(s, s->io_buffer + s->io_buffer_index,
                               size, ide_atapi_cmd_reply_end);
            s->packet_transfer_size -= size;
            s->elementary_transfer_size -= size;
            s->io_buffer_index += size;
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
            ide_transfer_start(s, s->io_buffer + s->io_buffer_index,
                               size, ide_atapi_cmd_reply_end);
            s->packet_transfer_size -= size;
            s->elementary_transfer_size -= size;
            s->io_buffer_index += size;
            ide_set_irq(s);
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
	ide_dma_start(s, ide_atapi_cmd_read_dma_cb);
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

/* ATAPI DMA support */

/* XXX: handle read errors */
static void ide_atapi_cmd_read_dma_cb(void *opaque, int ret)
{
    BMDMAState *bm = opaque;
    IDEState *s = bm->ide_if;
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
        if (dma_buf_rw(bm, 1) == 0)
            goto eot;
    }

    if (s->packet_transfer_size <= 0) {
        s->status = READY_STAT | SEEK_STAT;
        s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
        ide_set_irq(s);
    eot:
        bm->status &= ~BM_STATUS_DMAING;
        bm->status |= BM_STATUS_INT;
        bm->dma_cb = NULL;
        bm->ide_if = NULL;
        bm->aiocb = NULL;
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
    bm->aiocb = bdrv_aio_read(s->bs, (int64_t)s->lba << 2,
                              s->io_buffer + data_offset, n * 4,
                              ide_atapi_cmd_read_dma_cb, bm);
    if (!bm->aiocb) {
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
    ide_dma_start(s, ide_atapi_cmd_read_dma_cb);
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

                bdrv_get_geometry(s->bs, &total_sectors);
                total_sectors >>= 2;
                if (total_sectors == 0)
                    return -ASC_MEDIUM_NOT_PRESENT;

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

static void ide_atapi_cmd(IDEState *s)
{
    const uint8_t *packet;
    uint8_t *buf;
    int max_len;

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
    /* If there's a UNIT_ATTENTION condition pending, only
       REQUEST_SENSE and INQUIRY commands are allowed to complete. */
    if (s->sense_key == SENSE_UNIT_ATTENTION &&
	s->io_buffer[0] != GPCMD_REQUEST_SENSE &&
	s->io_buffer[0] != GPCMD_INQUIRY) {
	ide_atapi_cmd_check_status(s);
	return;
    }
    switch(s->io_buffer[0]) {
    case GPCMD_TEST_UNIT_READY:
        if (bdrv_is_inserted(s->bs)) {
            ide_atapi_cmd_ok(s);
        } else {
            ide_atapi_cmd_error(s, SENSE_NOT_READY,
                                ASC_MEDIUM_NOT_PRESENT);
        }
        break;
    case GPCMD_MODE_SENSE_6:
    case GPCMD_MODE_SENSE_10:
        {
            int action, code;
            if (packet[0] == GPCMD_MODE_SENSE_10)
                max_len = ube16_to_cpu(packet + 7);
            else
                max_len = packet[4];
            action = packet[2] >> 6;
            code = packet[2] & 0x3f;
            switch(action) {
            case 0: /* current values */
                switch(code) {
                case 0x01: /* error recovery */
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
                case 0x2a:
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
        }
        break;
    case GPCMD_REQUEST_SENSE:
        max_len = packet[4];
        memset(buf, 0, 18);
        buf[0] = 0x70 | (1 << 7);
        buf[2] = s->sense_key;
        buf[7] = 10;
        buf[12] = s->asc;
        if (s->sense_key == SENSE_UNIT_ATTENTION)
            s->sense_key = SENSE_NONE;
        ide_atapi_cmd_reply(s, 18, max_len);
        break;
    case GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        if (bdrv_is_inserted(s->bs)) {
            bdrv_set_locked(s->bs, packet[4] & 1);
            ide_atapi_cmd_ok(s);
        } else {
            ide_atapi_cmd_error(s, SENSE_NOT_READY,
                                ASC_MEDIUM_NOT_PRESENT);
        }
        break;
    case GPCMD_READ_10:
    case GPCMD_READ_12:
        {
            int nb_sectors, lba;

            if (packet[0] == GPCMD_READ_10)
                nb_sectors = ube16_to_cpu(packet + 7);
            else
                nb_sectors = ube32_to_cpu(packet + 6);
            lba = ube32_to_cpu(packet + 2);
            if (nb_sectors == 0) {
                ide_atapi_cmd_ok(s);
                break;
            }
            ide_atapi_cmd_read(s, lba, nb_sectors, 2048);
        }
        break;
    case GPCMD_READ_CD:
        {
            int nb_sectors, lba, transfer_request;

            nb_sectors = (packet[6] << 16) | (packet[7] << 8) | packet[8];
            lba = ube32_to_cpu(packet + 2);
            if (nb_sectors == 0) {
                ide_atapi_cmd_ok(s);
                break;
            }
            transfer_request = packet[9];
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
        break;
    case GPCMD_SEEK:
        {
            unsigned int lba;
            uint64_t total_sectors;

            bdrv_get_geometry(s->bs, &total_sectors);
            total_sectors >>= 2;
            if (total_sectors == 0) {
                ide_atapi_cmd_error(s, SENSE_NOT_READY,
                                    ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            lba = ube32_to_cpu(packet + 2);
            if (lba >= total_sectors) {
                ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                                    ASC_LOGICAL_BLOCK_OOR);
                break;
            }
            ide_atapi_cmd_ok(s);
        }
        break;
    case GPCMD_START_STOP_UNIT:
        {
            int start, eject;
            start = packet[4] & 1;
            eject = (packet[4] >> 1) & 1;

            if (eject && !start) {
                /* eject the disk */
                bdrv_eject(s->bs, 1);
            } else if (eject && start) {
                /* close the tray */
                bdrv_eject(s->bs, 0);
            }
            ide_atapi_cmd_ok(s);
        }
        break;
    case GPCMD_MECHANISM_STATUS:
        {
            max_len = ube16_to_cpu(packet + 8);
            cpu_to_ube16(buf, 0);
            /* no current LBA */
            buf[2] = 0;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 1;
            cpu_to_ube16(buf + 6, 0);
            ide_atapi_cmd_reply(s, 8, max_len);
        }
        break;
    case GPCMD_READ_TOC_PMA_ATIP:
        {
            int format, msf, start_track, len;
            uint64_t total_sectors;

            bdrv_get_geometry(s->bs, &total_sectors);
            total_sectors >>= 2;
            if (total_sectors == 0) {
                ide_atapi_cmd_error(s, SENSE_NOT_READY,
                                    ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            max_len = ube16_to_cpu(packet + 7);
            format = packet[9] >> 6;
            msf = (packet[1] >> 1) & 1;
            start_track = packet[6];
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
                break;
            }
        }
        break;
    case GPCMD_READ_CDVD_CAPACITY:
        {
            uint64_t total_sectors;

            bdrv_get_geometry(s->bs, &total_sectors);
            total_sectors >>= 2;
            if (total_sectors == 0) {
                ide_atapi_cmd_error(s, SENSE_NOT_READY,
                                    ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            /* NOTE: it is really the number of sectors minus 1 */
            cpu_to_ube32(buf, total_sectors - 1);
            cpu_to_ube32(buf + 4, 2048);
            ide_atapi_cmd_reply(s, 8, 8);
        }
        break;
    case GPCMD_READ_DVD_STRUCTURE:
        {
            int media = packet[1];
            int format = packet[7];
            int ret;

            max_len = ube16_to_cpu(packet + 8);

            if (format < 0xff) {
                if (media_is_cd(s)) {
                    ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                                        ASC_INCOMPATIBLE_FORMAT);
                    break;
                } else if (!media_present(s)) {
                    ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                                        ASC_INV_FIELD_IN_CMD_PACKET);
                    break;
                }
            }

            memset(buf, 0, max_len > IDE_DMA_BUF_SECTORS * 512 + 4 ?
                   IDE_DMA_BUF_SECTORS * 512 + 4 : max_len);

            switch (format) {
                case 0x00 ... 0x7f:
                case 0xff:
                    if (media == 0) {
                        ret = ide_dvd_read_structure(s, format, packet, buf);

                        if (ret < 0)
                            ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST, -ret);
                        else
                            ide_atapi_cmd_reply(s, ret, max_len);

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
        break;
    case GPCMD_SET_SPEED:
        ide_atapi_cmd_ok(s);
        break;
    case GPCMD_INQUIRY:
        max_len = packet[4];
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
        padstr8(buf + 32, 4, QEMU_VERSION);
        ide_atapi_cmd_reply(s, 36, max_len);
        break;
    case GPCMD_GET_CONFIGURATION:
        {
            uint32_t len;
            uint8_t index = 0;

            /* only feature 0 is supported */
            if (packet[2] != 0 || packet[3] != 0) {
                ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                                    ASC_INV_FIELD_IN_CMD_PACKET);
                break;
            }

            /* XXX: could result in alignment problems in some architectures */
            max_len = ube16_to_cpu(packet + 7);

            /*
             * XXX: avoid overflow for io_buffer if max_len is bigger than
             *      the size of that buffer (dimensioned to max number of
             *      sectors to transfer at once)
             *
             *      Only a problem if the feature/profiles grow.
             */
            if (max_len > 512) /* XXX: assume 1 sector */
                max_len = 512;

            memset(buf, 0, max_len);
            /* 
             * the number of sectors from the media tells us which profile
             * to use as current.  0 means there is no media
             */
            if (media_is_dvd(s))
                cpu_to_ube16(buf + 6, MMC_PROFILE_DVD_ROM);
            else if (media_is_cd(s))
                cpu_to_ube16(buf + 6, MMC_PROFILE_CD_ROM);

            buf[10] = 0x02 | 0x01; /* persistent and current */
            len = 12; /* headers: 8 + 4 */
            len += ide_atapi_set_profile(buf, &index, MMC_PROFILE_DVD_ROM);
            len += ide_atapi_set_profile(buf, &index, MMC_PROFILE_CD_ROM);
            cpu_to_ube32(buf, len - 4); /* data length */

            ide_atapi_cmd_reply(s, len, max_len);
            break;
        }
    default:
        ide_atapi_cmd_error(s, SENSE_ILLEGAL_REQUEST,
                            ASC_ILLEGAL_OPCODE);
        break;
    }
}

static void ide_cfata_metadata_inquiry(IDEState *s)
{
    uint16_t *p;
    uint32_t spd;

    p = (uint16_t *) s->io_buffer;
    memset(p, 0, 0x200);
    spd = ((s->mdata_size - 1) >> 9) + 1;

    put_le16(p + 0, 0x0001);			/* Data format revision */
    put_le16(p + 1, 0x0000);			/* Media property: silicon */
    put_le16(p + 2, s->media_changed);		/* Media status */
    put_le16(p + 3, s->mdata_size & 0xffff);	/* Capacity in bytes (low) */
    put_le16(p + 4, s->mdata_size >> 16);	/* Capacity in bytes (high) */
    put_le16(p + 5, spd & 0xffff);		/* Sectors per device (low) */
    put_le16(p + 6, spd >> 16);			/* Sectors per device (high) */
}

static void ide_cfata_metadata_read(IDEState *s)
{
    uint16_t *p;

    if (((s->hcyl << 16) | s->lcyl) << 9 > s->mdata_size + 2) {
        s->status = ERR_STAT;
        s->error = ABRT_ERR;
        return;
    }

    p = (uint16_t *) s->io_buffer;
    memset(p, 0, 0x200);

    put_le16(p + 0, s->media_changed);		/* Media status */
    memcpy(p + 1, s->mdata_storage + (((s->hcyl << 16) | s->lcyl) << 9),
                    MIN(MIN(s->mdata_size - (((s->hcyl << 16) | s->lcyl) << 9),
                                    s->nsector << 9), 0x200 - 2));
}

static void ide_cfata_metadata_write(IDEState *s)
{
    if (((s->hcyl << 16) | s->lcyl) << 9 > s->mdata_size + 2) {
        s->status = ERR_STAT;
        s->error = ABRT_ERR;
        return;
    }

    s->media_changed = 0;

    memcpy(s->mdata_storage + (((s->hcyl << 16) | s->lcyl) << 9),
                    s->io_buffer + 2,
                    MIN(MIN(s->mdata_size - (((s->hcyl << 16) | s->lcyl) << 9),
                                    s->nsector << 9), 0x200 - 2));
}

/* called when the inserted state of the media has changed */
static void cdrom_change_cb(void *opaque)
{
    IDEState *s = opaque;
    uint64_t nb_sectors;

    bdrv_get_geometry(s->bs, &nb_sectors);
    s->nb_sectors = nb_sectors;

    s->sense_key = SENSE_UNIT_ATTENTION;
    s->asc = ASC_MEDIUM_MAY_HAVE_CHANGED;

    ide_set_irq(s);
}

static void ide_cmd_lba48_transform(IDEState *s, int lba48)
{
    s->lba48 = lba48;

    /* handle the 'magic' 0 nsector count conversion here. to avoid
     * fiddling with the rest of the read logic, we just store the
     * full sector count in ->nsector and ignore ->hob_nsector from now
     */
    if (!s->lba48) {
	if (!s->nsector)
	    s->nsector = 256;
    } else {
	if (!s->nsector && !s->hob_nsector)
	    s->nsector = 65536;
	else {
	    int lo = s->nsector;
	    int hi = s->hob_nsector;

	    s->nsector = (hi << 8) | lo;
	}
    }
}

static void ide_clear_hob(IDEState *ide_if)
{
    /* any write clears HOB high bit of device control register */
    ide_if[0].select &= ~(1 << 7);
    ide_if[1].select &= ~(1 << 7);
}

static void ide_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    IDEState *ide_if = opaque;
    IDEState *s;
    int unit, n;
    int lba48 = 0;

#ifdef DEBUG_IDE
    printf("IDE: write addr=0x%x val=0x%02x\n", addr, val);
#endif

    addr &= 7;

    /* ignore writes to command block while busy with previous command */
    if (addr != 7 && (ide_if->cur_drive->status & (BUSY_STAT|DRQ_STAT)))
        return;

    switch(addr) {
    case 0:
        break;
    case 1:
	ide_clear_hob(ide_if);
        /* NOTE: data is written to the two drives */
	ide_if[0].hob_feature = ide_if[0].feature;
	ide_if[1].hob_feature = ide_if[1].feature;
        ide_if[0].feature = val;
        ide_if[1].feature = val;
        break;
    case 2:
	ide_clear_hob(ide_if);
	ide_if[0].hob_nsector = ide_if[0].nsector;
	ide_if[1].hob_nsector = ide_if[1].nsector;
        ide_if[0].nsector = val;
        ide_if[1].nsector = val;
        break;
    case 3:
	ide_clear_hob(ide_if);
	ide_if[0].hob_sector = ide_if[0].sector;
	ide_if[1].hob_sector = ide_if[1].sector;
        ide_if[0].sector = val;
        ide_if[1].sector = val;
        break;
    case 4:
	ide_clear_hob(ide_if);
	ide_if[0].hob_lcyl = ide_if[0].lcyl;
	ide_if[1].hob_lcyl = ide_if[1].lcyl;
        ide_if[0].lcyl = val;
        ide_if[1].lcyl = val;
        break;
    case 5:
	ide_clear_hob(ide_if);
	ide_if[0].hob_hcyl = ide_if[0].hcyl;
	ide_if[1].hob_hcyl = ide_if[1].hcyl;
        ide_if[0].hcyl = val;
        ide_if[1].hcyl = val;
        break;
    case 6:
	/* FIXME: HOB readback uses bit 7 */
        ide_if[0].select = (val & ~0x10) | 0xa0;
        ide_if[1].select = (val | 0x10) | 0xa0;
        /* select drive */
        unit = (val >> 4) & 1;
        s = ide_if + unit;
        ide_if->cur_drive = s;
        break;
    default:
    case 7:
        /* command */
#if defined(DEBUG_IDE)
        printf("ide: CMD=%02x\n", val);
#endif
        s = ide_if->cur_drive;
        /* ignore commands to non existant slave */
        if (s != ide_if && !s->bs)
            break;

        /* Only DEVICE RESET is allowed while BSY or/and DRQ are set */
        if ((s->status & (BUSY_STAT|DRQ_STAT)) && val != WIN_DEVICE_RESET)
            break;

        switch(val) {
        case WIN_IDENTIFY:
            if (s->bs && !s->is_cdrom) {
                if (!s->is_cf)
                    ide_identify(s);
                else
                    ide_cfata_identify(s);
                s->status = READY_STAT | SEEK_STAT;
                ide_transfer_start(s, s->io_buffer, 512, ide_transfer_stop);
            } else {
                if (s->is_cdrom) {
                    ide_set_signature(s);
                }
                ide_abort_command(s);
            }
            ide_set_irq(s);
            break;
        case WIN_SPECIFY:
        case WIN_RECAL:
            s->error = 0;
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
        case WIN_SETMULT:
            if (s->is_cf && s->nsector == 0) {
                /* Disable Read and Write Multiple */
                s->mult_sectors = 0;
                s->status = READY_STAT | SEEK_STAT;
            } else if ((s->nsector & 0xff) != 0 &&
                ((s->nsector & 0xff) > MAX_MULT_SECTORS ||
                 (s->nsector & (s->nsector - 1)) != 0)) {
                ide_abort_command(s);
            } else {
                s->mult_sectors = s->nsector & 0xff;
                s->status = READY_STAT | SEEK_STAT;
            }
            ide_set_irq(s);
            break;
        case WIN_VERIFY_EXT:
	    lba48 = 1;
        case WIN_VERIFY:
        case WIN_VERIFY_ONCE:
            /* do sector number check ? */
	    ide_cmd_lba48_transform(s, lba48);
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
	case WIN_READ_EXT:
	    lba48 = 1;
        case WIN_READ:
        case WIN_READ_ONCE:
            if (!s->bs)
                goto abort_cmd;
	    ide_cmd_lba48_transform(s, lba48);
            s->req_nb_sectors = 1;
            ide_sector_read(s);
            break;
	case WIN_WRITE_EXT:
	    lba48 = 1;
        case WIN_WRITE:
        case WIN_WRITE_ONCE:
        case CFA_WRITE_SECT_WO_ERASE:
        case WIN_WRITE_VERIFY:
	    ide_cmd_lba48_transform(s, lba48);
            s->error = 0;
            s->status = SEEK_STAT | READY_STAT;
            s->req_nb_sectors = 1;
            ide_transfer_start(s, s->io_buffer, 512, ide_sector_write);
            s->media_changed = 1;
            break;
	case WIN_MULTREAD_EXT:
	    lba48 = 1;
        case WIN_MULTREAD:
            if (!s->mult_sectors)
                goto abort_cmd;
	    ide_cmd_lba48_transform(s, lba48);
            s->req_nb_sectors = s->mult_sectors;
            ide_sector_read(s);
            break;
        case WIN_MULTWRITE_EXT:
	    lba48 = 1;
        case WIN_MULTWRITE:
        case CFA_WRITE_MULTI_WO_ERASE:
            if (!s->mult_sectors)
                goto abort_cmd;
	    ide_cmd_lba48_transform(s, lba48);
            s->error = 0;
            s->status = SEEK_STAT | READY_STAT;
            s->req_nb_sectors = s->mult_sectors;
            n = s->nsector;
            if (n > s->req_nb_sectors)
                n = s->req_nb_sectors;
            ide_transfer_start(s, s->io_buffer, 512 * n, ide_sector_write);
            s->media_changed = 1;
            break;
	case WIN_READDMA_EXT:
	    lba48 = 1;
        case WIN_READDMA:
        case WIN_READDMA_ONCE:
            if (!s->bs)
                goto abort_cmd;
	    ide_cmd_lba48_transform(s, lba48);
            ide_sector_read_dma(s);
            break;
	case WIN_WRITEDMA_EXT:
	    lba48 = 1;
        case WIN_WRITEDMA:
        case WIN_WRITEDMA_ONCE:
            if (!s->bs)
                goto abort_cmd;
	    ide_cmd_lba48_transform(s, lba48);
            ide_sector_write_dma(s);
            s->media_changed = 1;
            break;
        case WIN_READ_NATIVE_MAX_EXT:
	    lba48 = 1;
        case WIN_READ_NATIVE_MAX:
	    ide_cmd_lba48_transform(s, lba48);
            ide_set_sector(s, s->nb_sectors - 1);
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
        case WIN_CHECKPOWERMODE1:
        case WIN_CHECKPOWERMODE2:
            s->nsector = 0xff; /* device active or idle */
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
        case WIN_SETFEATURES:
            if (!s->bs)
                goto abort_cmd;
            /* XXX: valid for CDROM ? */
            switch(s->feature) {
            case 0xcc: /* reverting to power-on defaults enable */
            case 0x66: /* reverting to power-on defaults disable */
            case 0x02: /* write cache enable */
            case 0x82: /* write cache disable */
            case 0xaa: /* read look-ahead enable */
            case 0x55: /* read look-ahead disable */
            case 0x05: /* set advanced power management mode */
            case 0x85: /* disable advanced power management mode */
            case 0x69: /* NOP */
            case 0x67: /* NOP */
            case 0x96: /* NOP */
            case 0x9a: /* NOP */
            case 0x42: /* enable Automatic Acoustic Mode */
            case 0xc2: /* disable Automatic Acoustic Mode */
                s->status = READY_STAT | SEEK_STAT;
                ide_set_irq(s);
                break;
            case 0x03: { /* set transfer mode */
		uint8_t val = s->nsector & 0x07;

		switch (s->nsector >> 3) {
		    case 0x00: /* pio default */
		    case 0x01: /* pio mode */
			put_le16(s->identify_data + 62,0x07);
			put_le16(s->identify_data + 63,0x07);
			put_le16(s->identify_data + 88,0x3f);
			break;
                    case 0x02: /* sigle word dma mode*/
			put_le16(s->identify_data + 62,0x07 | (1 << (val + 8)));
			put_le16(s->identify_data + 63,0x07);
			put_le16(s->identify_data + 88,0x3f);
			break;
		    case 0x04: /* mdma mode */
			put_le16(s->identify_data + 62,0x07);
			put_le16(s->identify_data + 63,0x07 | (1 << (val + 8)));
			put_le16(s->identify_data + 88,0x3f);
			break;
		    case 0x08: /* udma mode */
			put_le16(s->identify_data + 62,0x07);
			put_le16(s->identify_data + 63,0x07);
			put_le16(s->identify_data + 88,0x3f | (1 << (val + 8)));
			break;
		    default:
			goto abort_cmd;
		}
                s->status = READY_STAT | SEEK_STAT;
                ide_set_irq(s);
                break;
	    }
            default:
                goto abort_cmd;
            }
            break;
        case WIN_FLUSH_CACHE:
        case WIN_FLUSH_CACHE_EXT:
            if (s->bs)
                bdrv_flush(s->bs);
	    s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
        case WIN_STANDBY:
        case WIN_STANDBY2:
        case WIN_STANDBYNOW1:
        case WIN_STANDBYNOW2:
        case WIN_IDLEIMMEDIATE:
        case CFA_IDLEIMMEDIATE:
        case WIN_SETIDLE1:
        case WIN_SETIDLE2:
        case WIN_SLEEPNOW1:
        case WIN_SLEEPNOW2:
            s->status = READY_STAT;
            ide_set_irq(s);
            break;
        case WIN_SEEK:
            if(s->is_cdrom)
                goto abort_cmd;
            /* XXX: Check that seek is within bounds */
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
            /* ATAPI commands */
        case WIN_PIDENTIFY:
            if (s->is_cdrom) {
                ide_atapi_identify(s);
                s->status = READY_STAT | SEEK_STAT;
                ide_transfer_start(s, s->io_buffer, 512, ide_transfer_stop);
            } else {
                ide_abort_command(s);
            }
            ide_set_irq(s);
            break;
        case WIN_DIAGNOSE:
            ide_set_signature(s);
            if (s->is_cdrom)
                s->status = 0; /* ATAPI spec (v6) section 9.10 defines packet
                                * devices to return a clear status register
                                * with READY_STAT *not* set. */
            else
                s->status = READY_STAT | SEEK_STAT;
            s->error = 0x01; /* Device 0 passed, Device 1 passed or not
                              * present. 
                              */
            ide_set_irq(s);
            break;
        case WIN_SRST:
            if (!s->is_cdrom)
                goto abort_cmd;
            ide_set_signature(s);
            s->status = 0x00; /* NOTE: READY is _not_ set */
            s->error = 0x01;
            break;
        case WIN_PACKETCMD:
            if (!s->is_cdrom)
                goto abort_cmd;
            /* overlapping commands not supported */
            if (s->feature & 0x02)
                goto abort_cmd;
            s->status = READY_STAT | SEEK_STAT;
            s->atapi_dma = s->feature & 1;
            s->nsector = 1;
            ide_transfer_start(s, s->io_buffer, ATAPI_PACKET_SIZE,
                               ide_atapi_cmd);
            break;
        /* CF-ATA commands */
        case CFA_REQ_EXT_ERROR_CODE:
            if (!s->is_cf)
                goto abort_cmd;
            s->error = 0x09;    /* miscellaneous error */
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
        case CFA_ERASE_SECTORS:
        case CFA_WEAR_LEVEL:
            if (!s->is_cf)
                goto abort_cmd;
            if (val == CFA_WEAR_LEVEL)
                s->nsector = 0;
            if (val == CFA_ERASE_SECTORS)
                s->media_changed = 1;
            s->error = 0x00;
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
        case CFA_TRANSLATE_SECTOR:
            if (!s->is_cf)
                goto abort_cmd;
            s->error = 0x00;
            s->status = READY_STAT | SEEK_STAT;
            memset(s->io_buffer, 0, 0x200);
            s->io_buffer[0x00] = s->hcyl;			/* Cyl MSB */
            s->io_buffer[0x01] = s->lcyl;			/* Cyl LSB */
            s->io_buffer[0x02] = s->select;			/* Head */
            s->io_buffer[0x03] = s->sector;			/* Sector */
            s->io_buffer[0x04] = ide_get_sector(s) >> 16;	/* LBA MSB */
            s->io_buffer[0x05] = ide_get_sector(s) >> 8;	/* LBA */
            s->io_buffer[0x06] = ide_get_sector(s) >> 0;	/* LBA LSB */
            s->io_buffer[0x13] = 0x00;				/* Erase flag */
            s->io_buffer[0x18] = 0x00;				/* Hot count */
            s->io_buffer[0x19] = 0x00;				/* Hot count */
            s->io_buffer[0x1a] = 0x01;				/* Hot count */
            ide_transfer_start(s, s->io_buffer, 0x200, ide_transfer_stop);
            ide_set_irq(s);
            break;
        case CFA_ACCESS_METADATA_STORAGE:
            if (!s->is_cf)
                goto abort_cmd;
            switch (s->feature) {
            case 0x02:	/* Inquiry Metadata Storage */
                ide_cfata_metadata_inquiry(s);
                break;
            case 0x03:	/* Read Metadata Storage */
                ide_cfata_metadata_read(s);
                break;
            case 0x04:	/* Write Metadata Storage */
                ide_cfata_metadata_write(s);
                break;
            default:
                goto abort_cmd;
            }
            ide_transfer_start(s, s->io_buffer, 0x200, ide_transfer_stop);
            s->status = 0x00; /* NOTE: READY is _not_ set */
            ide_set_irq(s);
            break;
        case IBM_SENSE_CONDITION:
            if (!s->is_cf)
                goto abort_cmd;
            switch (s->feature) {
            case 0x01:  /* sense temperature in device */
                s->nsector = 0x50;      /* +20 C */
                break;
            default:
                goto abort_cmd;
            }
            s->status = READY_STAT | SEEK_STAT;
            ide_set_irq(s);
            break;
        default:
        abort_cmd:
            ide_abort_command(s);
            ide_set_irq(s);
            break;
        }
    }
}

static uint32_t ide_ioport_read(void *opaque, uint32_t addr1)
{
    IDEState *ide_if = opaque;
    IDEState *s = ide_if->cur_drive;
    uint32_t addr;
    int ret, hob;

    addr = addr1 & 7;
    /* FIXME: HOB readback uses bit 7, but it's always set right now */
    //hob = s->select & (1 << 7);
    hob = 0;
    switch(addr) {
    case 0:
        ret = 0xff;
        break;
    case 1:
        if ((!ide_if[0].bs && !ide_if[1].bs) ||
            (s != ide_if && !s->bs))
            ret = 0;
        else if (!hob)
            ret = s->error;
	else
	    ret = s->hob_feature;
        break;
    case 2:
        if (!ide_if[0].bs && !ide_if[1].bs)
            ret = 0;
        else if (!hob)
            ret = s->nsector & 0xff;
	else
	    ret = s->hob_nsector;
        break;
    case 3:
        if (!ide_if[0].bs && !ide_if[1].bs)
            ret = 0;
        else if (!hob)
            ret = s->sector;
	else
	    ret = s->hob_sector;
        break;
    case 4:
        if (!ide_if[0].bs && !ide_if[1].bs)
            ret = 0;
        else if (!hob)
            ret = s->lcyl;
	else
	    ret = s->hob_lcyl;
        break;
    case 5:
        if (!ide_if[0].bs && !ide_if[1].bs)
            ret = 0;
        else if (!hob)
            ret = s->hcyl;
	else
	    ret = s->hob_hcyl;
        break;
    case 6:
        if (!ide_if[0].bs && !ide_if[1].bs)
            ret = 0;
        else
            ret = s->select;
        break;
    default:
    case 7:
        if ((!ide_if[0].bs && !ide_if[1].bs) ||
            (s != ide_if && !s->bs))
            ret = 0;
        else
            ret = s->status;
        qemu_irq_lower(s->irq);
        break;
    }
#ifdef DEBUG_IDE
    printf("ide: read addr=0x%x val=%02x\n", addr1, ret);
#endif
    return ret;
}

static uint32_t ide_status_read(void *opaque, uint32_t addr)
{
    IDEState *ide_if = opaque;
    IDEState *s = ide_if->cur_drive;
    int ret;

    if ((!ide_if[0].bs && !ide_if[1].bs) ||
        (s != ide_if && !s->bs))
        ret = 0;
    else
        ret = s->status;
#ifdef DEBUG_IDE
    printf("ide: read status addr=0x%x val=%02x\n", addr, ret);
#endif
    return ret;
}

static void ide_cmd_write(void *opaque, uint32_t addr, uint32_t val)
{
    IDEState *ide_if = opaque;
    IDEState *s;
    int i;

#ifdef DEBUG_IDE
    printf("ide: write control addr=0x%x val=%02x\n", addr, val);
#endif
    /* common for both drives */
    if (!(ide_if[0].cmd & IDE_CMD_RESET) &&
        (val & IDE_CMD_RESET)) {
        /* reset low to high */
        for(i = 0;i < 2; i++) {
            s = &ide_if[i];
            s->status = BUSY_STAT | SEEK_STAT;
            s->error = 0x01;
        }
    } else if ((ide_if[0].cmd & IDE_CMD_RESET) &&
               !(val & IDE_CMD_RESET)) {
        /* high to low */
        for(i = 0;i < 2; i++) {
            s = &ide_if[i];
            if (s->is_cdrom)
                s->status = 0x00; /* NOTE: READY is _not_ set */
            else
                s->status = READY_STAT | SEEK_STAT;
            ide_set_signature(s);
        }
    }

    ide_if[0].cmd = val;
    ide_if[1].cmd = val;
}

static void ide_data_writew(void *opaque, uint32_t addr, uint32_t val)
{
    IDEState *s = ((IDEState *)opaque)->cur_drive;
    uint8_t *p;

    /* PIO data access allowed only when DRQ bit is set */
    if (!(s->status & DRQ_STAT))
        return;

    p = s->data_ptr;
    *(uint16_t *)p = le16_to_cpu(val);
    p += 2;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
}

static uint32_t ide_data_readw(void *opaque, uint32_t addr)
{
    IDEState *s = ((IDEState *)opaque)->cur_drive;
    uint8_t *p;
    int ret;

    /* PIO data access allowed only when DRQ bit is set */
    if (!(s->status & DRQ_STAT))
        return 0;

    p = s->data_ptr;
    ret = cpu_to_le16(*(uint16_t *)p);
    p += 2;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
    return ret;
}

static void ide_data_writel(void *opaque, uint32_t addr, uint32_t val)
{
    IDEState *s = ((IDEState *)opaque)->cur_drive;
    uint8_t *p;

    /* PIO data access allowed only when DRQ bit is set */
    if (!(s->status & DRQ_STAT))
        return;

    p = s->data_ptr;
    *(uint32_t *)p = le32_to_cpu(val);
    p += 4;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
}

static uint32_t ide_data_readl(void *opaque, uint32_t addr)
{
    IDEState *s = ((IDEState *)opaque)->cur_drive;
    uint8_t *p;
    int ret;

    /* PIO data access allowed only when DRQ bit is set */
    if (!(s->status & DRQ_STAT))
        return 0;

    p = s->data_ptr;
    ret = cpu_to_le32(*(uint32_t *)p);
    p += 4;
    s->data_ptr = p;
    if (p >= s->data_end)
        s->end_transfer_func(s);
    return ret;
}

static void ide_dummy_transfer_stop(IDEState *s)
{
    s->data_ptr = s->io_buffer;
    s->data_end = s->io_buffer;
    s->io_buffer[0] = 0xff;
    s->io_buffer[1] = 0xff;
    s->io_buffer[2] = 0xff;
    s->io_buffer[3] = 0xff;
}

static void ide_reset(IDEState *s)
{
    if (s->is_cf)
        s->mult_sectors = 0;
    else
        s->mult_sectors = MAX_MULT_SECTORS;
    s->cur_drive = s;
    s->select = 0xa0;
    s->status = READY_STAT | SEEK_STAT;
    ide_set_signature(s);
    /* init the transfer handler so that 0xffff is returned on data
       accesses */
    s->end_transfer_func = ide_dummy_transfer_stop;
    ide_dummy_transfer_stop(s);
    s->media_changed = 0;
}

static void ide_init2(IDEState *ide_state,
                      BlockDriverState *hd0, BlockDriverState *hd1,
                      qemu_irq irq)
{
    IDEState *s;
    static int drive_serial = 1;
    int i, cylinders, heads, secs;
    uint64_t nb_sectors;

    for(i = 0; i < 2; i++) {
        s = ide_state + i;
        s->io_buffer = qemu_memalign(512, IDE_DMA_BUF_SECTORS*512 + 4);
        if (i == 0)
            s->bs = hd0;
        else
            s->bs = hd1;
        if (s->bs) {
            bdrv_get_geometry(s->bs, &nb_sectors);
            bdrv_guess_geometry(s->bs, &cylinders, &heads, &secs);
            s->cylinders = cylinders;
            s->heads = heads;
            s->sectors = secs;
            s->nb_sectors = nb_sectors;

            if (bdrv_get_type_hint(s->bs) == BDRV_TYPE_CDROM) {
                s->is_cdrom = 1;
		bdrv_set_change_cb(s->bs, cdrom_change_cb, s);
            }
        }
        s->drive_serial = drive_serial++;
        strncpy(s->drive_serial_str, drive_get_serial(s->bs),
                sizeof(s->drive_serial_str));
        if (strlen(s->drive_serial_str) == 0)
            snprintf(s->drive_serial_str, sizeof(s->drive_serial_str),
                    "QM%05d", s->drive_serial);
        s->irq = irq;
        s->sector_write_timer = qemu_new_timer(vm_clock,
                                               ide_sector_write_timer_cb, s);
        ide_reset(s);
    }
}

static void ide_init_ioport(IDEState *ide_state, int iobase, int iobase2)
{
    register_ioport_write(iobase, 8, 1, ide_ioport_write, ide_state);
    register_ioport_read(iobase, 8, 1, ide_ioport_read, ide_state);
    if (iobase2) {
        register_ioport_read(iobase2, 1, 1, ide_status_read, ide_state);
        register_ioport_write(iobase2, 1, 1, ide_cmd_write, ide_state);
    }

    /* data ports */
    register_ioport_write(iobase, 2, 2, ide_data_writew, ide_state);
    register_ioport_read(iobase, 2, 2, ide_data_readw, ide_state);
    register_ioport_write(iobase, 4, 4, ide_data_writel, ide_state);
    register_ioport_read(iobase, 4, 4, ide_data_readl, ide_state);
}

/* save per IDE drive data */
static void ide_save(QEMUFile* f, IDEState *s)
{
    qemu_put_be32(f, s->mult_sectors);
    qemu_put_be32(f, s->identify_set);
    if (s->identify_set) {
        qemu_put_buffer(f, (const uint8_t *)s->identify_data, 512);
    }
    qemu_put_8s(f, &s->feature);
    qemu_put_8s(f, &s->error);
    qemu_put_be32s(f, &s->nsector);
    qemu_put_8s(f, &s->sector);
    qemu_put_8s(f, &s->lcyl);
    qemu_put_8s(f, &s->hcyl);
    qemu_put_8s(f, &s->hob_feature);
    qemu_put_8s(f, &s->hob_nsector);
    qemu_put_8s(f, &s->hob_sector);
    qemu_put_8s(f, &s->hob_lcyl);
    qemu_put_8s(f, &s->hob_hcyl);
    qemu_put_8s(f, &s->select);
    qemu_put_8s(f, &s->status);
    qemu_put_8s(f, &s->lba48);

    qemu_put_8s(f, &s->sense_key);
    qemu_put_8s(f, &s->asc);
    /* XXX: if a transfer is pending, we do not save it yet */
}

/* load per IDE drive data */
static void ide_load(QEMUFile* f, IDEState *s)
{
    s->mult_sectors=qemu_get_be32(f);
    s->identify_set=qemu_get_be32(f);
    if (s->identify_set) {
        qemu_get_buffer(f, (uint8_t *)s->identify_data, 512);
    }
    qemu_get_8s(f, &s->feature);
    qemu_get_8s(f, &s->error);
    qemu_get_be32s(f, &s->nsector);
    qemu_get_8s(f, &s->sector);
    qemu_get_8s(f, &s->lcyl);
    qemu_get_8s(f, &s->hcyl);
    qemu_get_8s(f, &s->hob_feature);
    qemu_get_8s(f, &s->hob_nsector);
    qemu_get_8s(f, &s->hob_sector);
    qemu_get_8s(f, &s->hob_lcyl);
    qemu_get_8s(f, &s->hob_hcyl);
    qemu_get_8s(f, &s->select);
    qemu_get_8s(f, &s->status);
    qemu_get_8s(f, &s->lba48);

    qemu_get_8s(f, &s->sense_key);
    qemu_get_8s(f, &s->asc);
    /* XXX: if a transfer is pending, we do not save it yet */
}

/***********************************************************/
/* ISA IDE definitions */

void isa_ide_init(int iobase, int iobase2, qemu_irq irq,
                  BlockDriverState *hd0, BlockDriverState *hd1)
{
    IDEState *ide_state;

    ide_state = qemu_mallocz(sizeof(IDEState) * 2);
    if (!ide_state)
        return;

    ide_init2(ide_state, hd0, hd1, irq);
    ide_init_ioport(ide_state, iobase, iobase2);
}

/***********************************************************/
/* PCI IDE definitions */

static void cmd646_update_irq(PCIIDEState *d);

static void ide_map(PCIDevice *pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIIDEState *d = (PCIIDEState *)pci_dev;
    IDEState *ide_state;

    if (region_num <= 3) {
        ide_state = &d->ide_if[(region_num >> 1) * 2];
        if (region_num & 1) {
            register_ioport_read(addr + 2, 1, 1, ide_status_read, ide_state);
            register_ioport_write(addr + 2, 1, 1, ide_cmd_write, ide_state);
        } else {
            register_ioport_write(addr, 8, 1, ide_ioport_write, ide_state);
            register_ioport_read(addr, 8, 1, ide_ioport_read, ide_state);

            /* data ports */
            register_ioport_write(addr, 2, 2, ide_data_writew, ide_state);
            register_ioport_read(addr, 2, 2, ide_data_readw, ide_state);
            register_ioport_write(addr, 4, 4, ide_data_writel, ide_state);
            register_ioport_read(addr, 4, 4, ide_data_readl, ide_state);
        }
    }
}

static void ide_dma_start(IDEState *s, BlockDriverCompletionFunc *dma_cb)
{
    BMDMAState *bm = s->bmdma;
    if(!bm)
        return;
    bm->ide_if = s;
    bm->dma_cb = dma_cb;
    bm->cur_prd_last = 0;
    bm->cur_prd_addr = 0;
    bm->cur_prd_len = 0;
    bm->sector_num = ide_get_sector(s);
    bm->nsector = s->nsector;
    if (bm->status & BM_STATUS_DMAING) {
        bm->dma_cb(bm, 0);
    }
}

static void ide_dma_restart(IDEState *s)
{
    BMDMAState *bm = s->bmdma;
    ide_set_sector(s, bm->sector_num);
    s->io_buffer_index = 0;
    s->io_buffer_size = 0;
    s->nsector = bm->nsector;
    bm->cur_addr = bm->addr;
    bm->dma_cb = ide_write_dma_cb;
    ide_dma_start(s, bm->dma_cb);
}

static void ide_dma_cancel(BMDMAState *bm)
{
    if (bm->status & BM_STATUS_DMAING) {
        bm->status &= ~BM_STATUS_DMAING;
        /* cancel DMA request */
        bm->ide_if = NULL;
        bm->dma_cb = NULL;
        if (bm->aiocb) {
#ifdef DEBUG_AIO
            printf("aio_cancel\n");
#endif
            bdrv_aio_cancel(bm->aiocb);
            bm->aiocb = NULL;
        }
    }
}

static void bmdma_cmd_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    if (!(val & BM_CMD_START)) {
        /* XXX: do it better */
        ide_dma_cancel(bm);
        bm->cmd = val & 0x09;
    } else {
        if (!(bm->status & BM_STATUS_DMAING)) {
            bm->status |= BM_STATUS_DMAING;
            /* start dma transfer if possible */
            if (bm->dma_cb)
                bm->dma_cb(bm, 0);
        }
        bm->cmd = val & 0x09;
    }
}

static uint32_t bmdma_readb(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    PCIIDEState *pci_dev;
    uint32_t val;

    switch(addr & 3) {
    case 0:
        val = bm->cmd;
        break;
    case 1:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            val = pci_dev->dev.config[MRDMODE];
        } else {
            val = 0xff;
        }
        break;
    case 2:
        val = bm->status;
        break;
    case 3:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            if (bm == &pci_dev->bmdma[0])
                val = pci_dev->dev.config[UDIDETCR0];
            else
                val = pci_dev->dev.config[UDIDETCR1];
        } else {
            val = 0xff;
        }
        break;
    default:
        val = 0xff;
        break;
    }
#ifdef DEBUG_IDE
    printf("bmdma: readb 0x%02x : 0x%02x\n", addr, val);
#endif
    return val;
}

static void bmdma_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    PCIIDEState *pci_dev;
#ifdef DEBUG_IDE
    printf("bmdma: writeb 0x%02x : 0x%02x\n", addr, val);
#endif
    switch(addr & 3) {
    case 1:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            pci_dev->dev.config[MRDMODE] =
                (pci_dev->dev.config[MRDMODE] & ~0x30) | (val & 0x30);
            cmd646_update_irq(pci_dev);
        }
        break;
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    case 3:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            if (bm == &pci_dev->bmdma[0])
                pci_dev->dev.config[UDIDETCR0] = val;
            else
                pci_dev->dev.config[UDIDETCR1] = val;
        }
        break;
    }
}

static uint32_t bmdma_addr_readb(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = (bm->addr >> ((addr & 3) * 8)) & 0xff;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

static void bmdma_addr_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    int shift = (addr & 3) * 8;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr &= ~(0xFF << shift);
    bm->addr |= ((val & 0xFF) << shift) & ~3;
    bm->cur_addr = bm->addr;
}

static uint32_t bmdma_addr_readw(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = (bm->addr >> ((addr & 3) * 8)) & 0xffff;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

static void bmdma_addr_writew(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    int shift = (addr & 3) * 8;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr &= ~(0xFFFF << shift);
    bm->addr |= ((val & 0xFFFF) << shift) & ~3;
    bm->cur_addr = bm->addr;
}

static uint32_t bmdma_addr_readl(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = bm->addr;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

static void bmdma_addr_writel(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr = val & ~3;
    bm->cur_addr = bm->addr;
}

static void bmdma_map(PCIDevice *pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIIDEState *d = (PCIIDEState *)pci_dev;
    int i;

    for(i = 0;i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        d->ide_if[2 * i].bmdma = bm;
        d->ide_if[2 * i + 1].bmdma = bm;
        bm->pci_dev = (PCIIDEState *)pci_dev;
        qemu_add_vm_change_state_handler(ide_dma_restart_cb, bm);

        register_ioport_write(addr, 1, 1, bmdma_cmd_writeb, bm);

        register_ioport_write(addr + 1, 3, 1, bmdma_writeb, bm);
        register_ioport_read(addr, 4, 1, bmdma_readb, bm);

        register_ioport_write(addr + 4, 4, 1, bmdma_addr_writeb, bm);
        register_ioport_read(addr + 4, 4, 1, bmdma_addr_readb, bm);
        register_ioport_write(addr + 4, 4, 2, bmdma_addr_writew, bm);
        register_ioport_read(addr + 4, 4, 2, bmdma_addr_readw, bm);
        register_ioport_write(addr + 4, 4, 4, bmdma_addr_writel, bm);
        register_ioport_read(addr + 4, 4, 4, bmdma_addr_readl, bm);
        addr += 8;
    }
}

static void pci_ide_save(QEMUFile* f, void *opaque)
{
    PCIIDEState *d = opaque;
    int i;

    pci_device_save(&d->dev, f);

    for(i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        uint8_t ifidx;
        qemu_put_8s(f, &bm->cmd);
        qemu_put_8s(f, &bm->status);
        qemu_put_be32s(f, &bm->addr);
        qemu_put_sbe64s(f, &bm->sector_num);
        qemu_put_be32s(f, &bm->nsector);
        ifidx = bm->ide_if ? bm->ide_if - d->ide_if : 0;
        qemu_put_8s(f, &ifidx);
        /* XXX: if a transfer is pending, we do not save it yet */
    }

    /* per IDE interface data */
    for(i = 0; i < 2; i++) {
        IDEState *s = &d->ide_if[i * 2];
        uint8_t drive1_selected;
        qemu_put_8s(f, &s->cmd);
        drive1_selected = (s->cur_drive != s);
        qemu_put_8s(f, &drive1_selected);
    }

    /* per IDE drive data */
    for(i = 0; i < 4; i++) {
        ide_save(f, &d->ide_if[i]);
    }
}

static int pci_ide_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIIDEState *d = opaque;
    int ret, i;

    if (version_id != 2)
        return -EINVAL;
    ret = pci_device_load(&d->dev, f);
    if (ret < 0)
        return ret;

    for(i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        uint8_t ifidx;
        qemu_get_8s(f, &bm->cmd);
        qemu_get_8s(f, &bm->status);
        qemu_get_be32s(f, &bm->addr);
        qemu_get_sbe64s(f, &bm->sector_num);
        qemu_get_be32s(f, &bm->nsector);
        qemu_get_8s(f, &ifidx);
        bm->ide_if = &d->ide_if[ifidx];
        /* XXX: if a transfer is pending, we do not save it yet */
    }

    /* per IDE interface data */
    for(i = 0; i < 2; i++) {
        IDEState *s = &d->ide_if[i * 2];
        uint8_t drive1_selected;
        qemu_get_8s(f, &s->cmd);
        qemu_get_8s(f, &drive1_selected);
        s->cur_drive = &d->ide_if[i * 2 + (drive1_selected != 0)];
    }

    /* per IDE drive data */
    for(i = 0; i < 4; i++) {
        ide_load(f, &d->ide_if[i]);
    }
    return 0;
}

/* XXX: call it also when the MRDMODE is changed from the PCI config
   registers */
static void cmd646_update_irq(PCIIDEState *d)
{
    int pci_level;
    pci_level = ((d->dev.config[MRDMODE] & MRDMODE_INTR_CH0) &&
                 !(d->dev.config[MRDMODE] & MRDMODE_BLK_CH0)) ||
        ((d->dev.config[MRDMODE] & MRDMODE_INTR_CH1) &&
         !(d->dev.config[MRDMODE] & MRDMODE_BLK_CH1));
    qemu_set_irq(d->dev.irq[0], pci_level);
}

/* the PCI irq level is the logical OR of the two channels */
static void cmd646_set_irq(void *opaque, int channel, int level)
{
    PCIIDEState *d = opaque;
    int irq_mask;

    irq_mask = MRDMODE_INTR_CH0 << channel;
    if (level)
        d->dev.config[MRDMODE] |= irq_mask;
    else
        d->dev.config[MRDMODE] &= ~irq_mask;
    cmd646_update_irq(d);
}

static void cmd646_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    unsigned int i;

    for (i = 0; i < 2; i++)
        ide_dma_cancel(&d->bmdma[i]);
}

/* CMD646 PCI IDE controller */
void pci_cmd646_ide_init(PCIBus *bus, BlockDriverState **hd_table,
                         int secondary_ide_enabled)
{
    PCIIDEState *d;
    uint8_t *pci_conf;
    int i;
    qemu_irq *irq;

    d = (PCIIDEState *)pci_register_device(bus, "CMD646 IDE",
                                           sizeof(PCIIDEState),
                                           -1,
                                           NULL, NULL);
    d->type = IDE_TYPE_CMD646;
    pci_conf = d->dev.config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_CMD);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_CMD_646);

    pci_conf[0x08] = 0x07; // IDE controller revision
    pci_conf[0x09] = 0x8f;

    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);
    pci_conf[0x0e] = 0x00; // header_type

    pci_conf[0x51] = 0x04; // enable IDE0
    if (secondary_ide_enabled) {
        /* XXX: if not enabled, really disable the seconday IDE controller */
        pci_conf[0x51] |= 0x08; /* enable IDE1 */
    }

    pci_register_io_region((PCIDevice *)d, 0, 0x8,
                           PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_io_region((PCIDevice *)d, 1, 0x4,
                           PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_io_region((PCIDevice *)d, 2, 0x8,
                           PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_io_region((PCIDevice *)d, 3, 0x4,
                           PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_io_region((PCIDevice *)d, 4, 0x10,
                           PCI_ADDRESS_SPACE_IO, bmdma_map);

    pci_conf[0x3d] = 0x01; // interrupt on pin 1

    for(i = 0; i < 4; i++)
        d->ide_if[i].pci_dev = (PCIDevice *)d;

    irq = qemu_allocate_irqs(cmd646_set_irq, d, 2);
    ide_init2(&d->ide_if[0], hd_table[0], hd_table[1], irq[0]);
    ide_init2(&d->ide_if[2], hd_table[2], hd_table[3], irq[1]);

    register_savevm("ide", 0, 2, pci_ide_save, pci_ide_load, d);
    qemu_register_reset(cmd646_reset, d);
    cmd646_reset(d);
}

static void piix3_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    uint8_t *pci_conf = d->dev.config;
    int i;

    for (i = 0; i < 2; i++)
        ide_dma_cancel(&d->bmdma[i]);

    pci_conf[0x04] = 0x00;
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x80; /* FBC */
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x20] = 0x01; /* BMIBA: 20-23h */
}

/* hd_table must contain 4 block drivers */
/* NOTE: for the PIIX3, the IRQs and IOports are hardcoded */
void pci_piix3_ide_init(PCIBus *bus, BlockDriverState **hd_table, int devfn,
                        qemu_irq *pic)
{
    PCIIDEState *d;
    uint8_t *pci_conf;

    /* register a function 1 of PIIX3 */
    d = (PCIIDEState *)pci_register_device(bus, "PIIX3 IDE",
                                           sizeof(PCIIDEState),
                                           devfn,
                                           NULL, NULL);
    d->type = IDE_TYPE_PIIX3;

    pci_conf = d->dev.config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82371SB_1);
    pci_conf[0x09] = 0x80; // legacy ATA mode
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);
    pci_conf[0x0e] = 0x00; // header_type

    qemu_register_reset(piix3_reset, d);
    piix3_reset(d);

    pci_register_io_region((PCIDevice *)d, 4, 0x10,
                           PCI_ADDRESS_SPACE_IO, bmdma_map);

    ide_init2(&d->ide_if[0], hd_table[0], hd_table[1], pic[14]);
    ide_init2(&d->ide_if[2], hd_table[2], hd_table[3], pic[15]);
    ide_init_ioport(&d->ide_if[0], 0x1f0, 0x3f6);
    ide_init_ioport(&d->ide_if[2], 0x170, 0x376);

    register_savevm("ide", 0, 2, pci_ide_save, pci_ide_load, d);
}

/* hd_table must contain 4 block drivers */
/* NOTE: for the PIIX4, the IRQs and IOports are hardcoded */
void pci_piix4_ide_init(PCIBus *bus, BlockDriverState **hd_table, int devfn,
                        qemu_irq *pic)
{
    PCIIDEState *d;
    uint8_t *pci_conf;

    /* register a function 1 of PIIX4 */
    d = (PCIIDEState *)pci_register_device(bus, "PIIX4 IDE",
                                           sizeof(PCIIDEState),
                                           devfn,
                                           NULL, NULL);
    d->type = IDE_TYPE_PIIX4;

    pci_conf = d->dev.config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_INTEL_82371AB);
    pci_conf[0x09] = 0x80; // legacy ATA mode
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);
    pci_conf[0x0e] = 0x00; // header_type

    qemu_register_reset(piix3_reset, d);
    piix3_reset(d);

    pci_register_io_region((PCIDevice *)d, 4, 0x10,
                           PCI_ADDRESS_SPACE_IO, bmdma_map);

    ide_init2(&d->ide_if[0], hd_table[0], hd_table[1], pic[14]);
    ide_init2(&d->ide_if[2], hd_table[2], hd_table[3], pic[15]);
    ide_init_ioport(&d->ide_if[0], 0x1f0, 0x3f6);
    ide_init_ioport(&d->ide_if[2], 0x170, 0x376);

    register_savevm("ide", 0, 2, pci_ide_save, pci_ide_load, d);
}

#if defined(TARGET_PPC)
/***********************************************************/
/* MacIO based PowerPC IDE */

typedef struct MACIOIDEState {
    IDEState ide_if[2];
    void *dbdma;
    int stream_index;
} MACIOIDEState;

static int pmac_atapi_read(DBDMA_transfer *info, DBDMA_transfer_cb cb)
{
    MACIOIDEState *m = info->opaque;
    IDEState *s = m->ide_if->cur_drive;
    int ret;

    if (s->lba == -1)
        return 0;

    info->buf_pos = 0;

    while (info->buf_pos < info->len && s->packet_transfer_size > 0) {

        ret = cd_read_sector(s->bs, s->lba, s->io_buffer, s->cd_sector_size);
        if (ret < 0) {
            ide_transfer_stop(s);
            ide_atapi_io_error(s, ret);
            return info->buf_pos;
        }

        info->buf = s->io_buffer + m->stream_index;

        info->buf_len = s->cd_sector_size;
        if (info->buf_pos + info->buf_len > info->len)
            info->buf_len = info->len - info->buf_pos;

        cb(info);

	/* db-dma can ask for 512 bytes whereas block size is 2048... */

        m->stream_index += info->buf_len;
        s->lba += m->stream_index / s->cd_sector_size;
        m->stream_index %= s->cd_sector_size;

        info->buf_pos += info->buf_len;
        s->packet_transfer_size -= info->buf_len;
    }
    if (s->packet_transfer_size <= 0) {
        s->status = READY_STAT | SEEK_STAT;
        s->nsector = (s->nsector & ~7) | ATAPI_INT_REASON_IO
                                       | ATAPI_INT_REASON_CD;
        ide_set_irq(s);
    }

    return info->buf_pos;
}

static int pmac_ide_transfer(DBDMA_transfer *info,
                             DBDMA_transfer_cb cb)
{
    MACIOIDEState *m = info->opaque;
    IDEState *s = m->ide_if->cur_drive;
    int64_t sector_num;
    int ret, n;

    if (s->is_cdrom)
        return pmac_atapi_read(info, cb);

    info->buf = s->io_buffer;
    info->buf_pos = 0;
    while (info->buf_pos < info->len && s->nsector > 0) {

        sector_num = ide_get_sector(s);

        n = s->nsector;
        if (n > IDE_DMA_BUF_SECTORS)
            n = IDE_DMA_BUF_SECTORS;

        info->buf_len = n << 9;
        if (info->buf_pos + info->buf_len > info->len)
            info->buf_len = info->len - info->buf_pos;
        n = info->buf_len >> 9;

        if (s->is_read) {
            ret = bdrv_read(s->bs, sector_num, s->io_buffer, n);
            if (ret == 0)
                cb(info);
        } else {
            cb(info);
            ret = bdrv_write(s->bs, sector_num, s->io_buffer, n);
        }

        if (ret != 0) {
            ide_rw_error(s);
            return info->buf_pos;
        }

        info->buf_pos += n << 9;
        ide_set_sector(s, sector_num + n);
        s->nsector -= n;
    }

    if (s->nsector <= 0) {
        s->status = READY_STAT | SEEK_STAT;
        ide_set_irq(s);
    }

    return info->buf_pos;
}

/* PowerMac IDE memory IO */
static void pmac_ide_writeb (void *opaque,
                             target_phys_addr_t addr, uint32_t val)
{
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    switch (addr) {
    case 1 ... 7:
        ide_ioport_write(d->ide_if, addr, val);
        break;
    case 8:
    case 22:
        ide_cmd_write(d->ide_if, 0, val);
        break;
    default:
        break;
    }
}

static uint32_t pmac_ide_readb (void *opaque,target_phys_addr_t addr)
{
    uint8_t retval;
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    switch (addr) {
    case 1 ... 7:
        retval = ide_ioport_read(d->ide_if, addr);
        break;
    case 8:
    case 22:
        retval = ide_status_read(d->ide_if, 0);
        break;
    default:
        retval = 0xFF;
        break;
    }
    return retval;
}

static void pmac_ide_writew (void *opaque,
                             target_phys_addr_t addr, uint32_t val)
{
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    if (addr == 0) {
        ide_data_writew(d->ide_if, 0, val);
    }
}

static uint32_t pmac_ide_readw (void *opaque,target_phys_addr_t addr)
{
    uint16_t retval;
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    if (addr == 0) {
        retval = ide_data_readw(d->ide_if, 0);
    } else {
        retval = 0xFFFF;
    }
#ifdef TARGET_WORDS_BIGENDIAN
    retval = bswap16(retval);
#endif
    return retval;
}

static void pmac_ide_writel (void *opaque,
                             target_phys_addr_t addr, uint32_t val)
{
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    if (addr == 0) {
        ide_data_writel(d->ide_if, 0, val);
    }
}

static uint32_t pmac_ide_readl (void *opaque,target_phys_addr_t addr)
{
    uint32_t retval;
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    if (addr == 0) {
        retval = ide_data_readl(d->ide_if, 0);
    } else {
        retval = 0xFFFFFFFF;
    }
#ifdef TARGET_WORDS_BIGENDIAN
    retval = bswap32(retval);
#endif
    return retval;
}

static CPUWriteMemoryFunc *pmac_ide_write[] = {
    pmac_ide_writeb,
    pmac_ide_writew,
    pmac_ide_writel,
};

static CPUReadMemoryFunc *pmac_ide_read[] = {
    pmac_ide_readb,
    pmac_ide_readw,
    pmac_ide_readl,
};

static void pmac_ide_save(QEMUFile *f, void *opaque)
{
    MACIOIDEState *d = opaque;
    IDEState *s = d->ide_if;
    uint8_t drive1_selected;
    unsigned int i;

    /* per IDE interface data */
    qemu_put_8s(f, &s->cmd);
    drive1_selected = (s->cur_drive != s);
    qemu_put_8s(f, &drive1_selected);

    /* per IDE drive data */
    for(i = 0; i < 2; i++) {
        ide_save(f, &s[i]);
    }
}

static int pmac_ide_load(QEMUFile *f, void *opaque, int version_id)
{
    MACIOIDEState *d = opaque;
    IDEState *s = d->ide_if;
    uint8_t drive1_selected;
    unsigned int i;

    if (version_id != 1)
        return -EINVAL;

    /* per IDE interface data */
    qemu_get_8s(f, &s->cmd);
    qemu_get_8s(f, &drive1_selected);
    s->cur_drive = &s[(drive1_selected != 0)];

    /* per IDE drive data */
    for(i = 0; i < 2; i++) {
        ide_load(f, &s[i]);
    }
    return 0;
}

static void pmac_ide_reset(void *opaque)
{
    MACIOIDEState *d = opaque;
    IDEState *s = d->ide_if;

    ide_reset(&s[0]);
    ide_reset(&s[1]);
}

/* hd_table must contain 4 block drivers */
/* PowerMac uses memory mapped registers, not I/O. Return the memory
   I/O index to access the ide. */
int pmac_ide_init (BlockDriverState **hd_table, qemu_irq irq,
		   void *dbdma, int channel, qemu_irq dma_irq)
{
    MACIOIDEState *d;
    int pmac_ide_memory;

    d = qemu_mallocz(sizeof(MACIOIDEState));
    ide_init2(d->ide_if, hd_table[0], hd_table[1], irq);

    if (dbdma) {
        d->dbdma = dbdma;
        DBDMA_register_channel(dbdma, channel, dma_irq, pmac_ide_transfer, d);
    }

    pmac_ide_memory = cpu_register_io_memory(0, pmac_ide_read,
                                             pmac_ide_write, d);
    register_savevm("ide", 0, 1, pmac_ide_save, pmac_ide_load, d);
    qemu_register_reset(pmac_ide_reset, d);
    pmac_ide_reset(d);

    return pmac_ide_memory;
}
#endif /* TARGET_PPC */

/***********************************************************/
/* MMIO based ide port
 * This emulates IDE device connected directly to the CPU bus without
 * dedicated ide controller, which is often seen on embedded boards.
 */

typedef struct {
    void *dev;
    int shift;
} MMIOState;

static uint32_t mmio_ide_read (void *opaque, target_phys_addr_t addr)
{
    MMIOState *s = (MMIOState*)opaque;
    IDEState *ide = (IDEState*)s->dev;
    addr >>= s->shift;
    if (addr & 7)
        return ide_ioport_read(ide, addr);
    else
        return ide_data_readw(ide, 0);
}

static void mmio_ide_write (void *opaque, target_phys_addr_t addr,
	uint32_t val)
{
    MMIOState *s = (MMIOState*)opaque;
    IDEState *ide = (IDEState*)s->dev;
    addr >>= s->shift;
    if (addr & 7)
        ide_ioport_write(ide, addr, val);
    else
        ide_data_writew(ide, 0, val);
}

static CPUReadMemoryFunc *mmio_ide_reads[] = {
    mmio_ide_read,
    mmio_ide_read,
    mmio_ide_read,
};

static CPUWriteMemoryFunc *mmio_ide_writes[] = {
    mmio_ide_write,
    mmio_ide_write,
    mmio_ide_write,
};

static uint32_t mmio_ide_status_read (void *opaque, target_phys_addr_t addr)
{
    MMIOState *s= (MMIOState*)opaque;
    IDEState *ide = (IDEState*)s->dev;
    return ide_status_read(ide, 0);
}

static void mmio_ide_cmd_write (void *opaque, target_phys_addr_t addr,
	uint32_t val)
{
    MMIOState *s = (MMIOState*)opaque;
    IDEState *ide = (IDEState*)s->dev;
    ide_cmd_write(ide, 0, val);
}

static CPUReadMemoryFunc *mmio_ide_status[] = {
    mmio_ide_status_read,
    mmio_ide_status_read,
    mmio_ide_status_read,
};

static CPUWriteMemoryFunc *mmio_ide_cmd[] = {
    mmio_ide_cmd_write,
    mmio_ide_cmd_write,
    mmio_ide_cmd_write,
};

void mmio_ide_init (target_phys_addr_t membase, target_phys_addr_t membase2,
                    qemu_irq irq, int shift,
                    BlockDriverState *hd0, BlockDriverState *hd1)
{
    MMIOState *s = qemu_mallocz(sizeof(MMIOState));
    IDEState *ide = qemu_mallocz(sizeof(IDEState) * 2);
    int mem1, mem2;

    ide_init2(ide, hd0, hd1, irq);

    s->dev = ide;
    s->shift = shift;

    mem1 = cpu_register_io_memory(0, mmio_ide_reads, mmio_ide_writes, s);
    mem2 = cpu_register_io_memory(0, mmio_ide_status, mmio_ide_cmd, s);
    cpu_register_physical_memory(membase, 16 << shift, mem1);
    cpu_register_physical_memory(membase2, 2 << shift, mem2);
}

/***********************************************************/
/* CF-ATA Microdrive */

#define METADATA_SIZE	0x20

/* DSCM-1XXXX Microdrive hard disk with CF+ II / PCMCIA interface.  */
struct md_s {
    IDEState ide[2];
    struct pcmcia_card_s card;
    uint32_t attr_base;
    uint32_t io_base;

    /* Card state */
    uint8_t opt;
    uint8_t stat;
    uint8_t pins;

    uint8_t ctrl;
    uint16_t io;
    int cycle;
};

/* Register bitfields */
enum md_opt {
    OPT_MODE_MMAP	= 0,
    OPT_MODE_IOMAP16	= 1,
    OPT_MODE_IOMAP1	= 2,
    OPT_MODE_IOMAP2	= 3,
    OPT_MODE		= 0x3f,
    OPT_LEVIREQ		= 0x40,
    OPT_SRESET		= 0x80,
};
enum md_cstat {
    STAT_INT		= 0x02,
    STAT_PWRDWN		= 0x04,
    STAT_XE		= 0x10,
    STAT_IOIS8		= 0x20,
    STAT_SIGCHG		= 0x40,
    STAT_CHANGED	= 0x80,
};
enum md_pins {
    PINS_MRDY		= 0x02,
    PINS_CRDY		= 0x20,
};
enum md_ctrl {
    CTRL_IEN		= 0x02,
    CTRL_SRST		= 0x04,
};

static inline void md_interrupt_update(struct md_s *s)
{
    if (!s->card.slot)
        return;

    qemu_set_irq(s->card.slot->irq,
                    !(s->stat & STAT_INT) &&	/* Inverted */
                    !(s->ctrl & (CTRL_IEN | CTRL_SRST)) &&
                    !(s->opt & OPT_SRESET));
}

static void md_set_irq(void *opaque, int irq, int level)
{
    struct md_s *s = (struct md_s *) opaque;
    if (level)
        s->stat |= STAT_INT;
    else
        s->stat &= ~STAT_INT;

    md_interrupt_update(s);
}

static void md_reset(struct md_s *s)
{
    s->opt = OPT_MODE_MMAP;
    s->stat = 0;
    s->pins = 0;
    s->cycle = 0;
    s->ctrl = 0;
    ide_reset(s->ide);
}

static uint8_t md_attr_read(void *opaque, uint32_t at)
{
    struct md_s *s = (struct md_s *) opaque;
    if (at < s->attr_base) {
        if (at < s->card.cis_len)
            return s->card.cis[at];
        else
            return 0x00;
    }

    at -= s->attr_base;

    switch (at) {
    case 0x00:	/* Configuration Option Register */
        return s->opt;
    case 0x02:	/* Card Configuration Status Register */
        if (s->ctrl & CTRL_IEN)
            return s->stat & ~STAT_INT;
        else
            return s->stat;
    case 0x04:	/* Pin Replacement Register */
        return (s->pins & PINS_CRDY) | 0x0c;
    case 0x06:	/* Socket and Copy Register */
        return 0x00;
#ifdef VERBOSE
    default:
        printf("%s: Bad attribute space register %02x\n", __FUNCTION__, at);
#endif
    }

    return 0;
}

static void md_attr_write(void *opaque, uint32_t at, uint8_t value)
{
    struct md_s *s = (struct md_s *) opaque;
    at -= s->attr_base;

    switch (at) {
    case 0x00:	/* Configuration Option Register */
        s->opt = value & 0xcf;
        if (value & OPT_SRESET)
            md_reset(s);
        md_interrupt_update(s);
        break;
    case 0x02:	/* Card Configuration Status Register */
        if ((s->stat ^ value) & STAT_PWRDWN)
            s->pins |= PINS_CRDY;
        s->stat &= 0x82;
        s->stat |= value & 0x74;
        md_interrupt_update(s);
        /* Word 170 in Identify Device must be equal to STAT_XE */
        break;
    case 0x04:	/* Pin Replacement Register */
        s->pins &= PINS_CRDY;
        s->pins |= value & PINS_MRDY;
        break;
    case 0x06:	/* Socket and Copy Register */
        break;
    default:
        printf("%s: Bad attribute space register %02x\n", __FUNCTION__, at);
    }
}

static uint16_t md_common_read(void *opaque, uint32_t at)
{
    struct md_s *s = (struct md_s *) opaque;
    uint16_t ret;
    at -= s->io_base;

    switch (s->opt & OPT_MODE) {
    case OPT_MODE_MMAP:
        if ((at & ~0x3ff) == 0x400)
            at = 0;
        break;
    case OPT_MODE_IOMAP16:
        at &= 0xf;
        break;
    case OPT_MODE_IOMAP1:
        if ((at & ~0xf) == 0x3f0)
            at -= 0x3e8;
        else if ((at & ~0xf) == 0x1f0)
            at -= 0x1f0;
        break;
    case OPT_MODE_IOMAP2:
        if ((at & ~0xf) == 0x370)
            at -= 0x368;
        else if ((at & ~0xf) == 0x170)
            at -= 0x170;
    }

    switch (at) {
    case 0x0:	/* Even RD Data */
    case 0x8:
        return ide_data_readw(s->ide, 0);

        /* TODO: 8-bit accesses */
        if (s->cycle)
            ret = s->io >> 8;
        else {
            s->io = ide_data_readw(s->ide, 0);
            ret = s->io & 0xff;
        }
        s->cycle = !s->cycle;
        return ret;
    case 0x9:	/* Odd RD Data */
        return s->io >> 8;
    case 0xd:	/* Error */
        return ide_ioport_read(s->ide, 0x1);
    case 0xe:	/* Alternate Status */
        if (s->ide->cur_drive->bs)
            return s->ide->cur_drive->status;
        else
            return 0;
    case 0xf:	/* Device Address */
        return 0xc2 | ((~s->ide->select << 2) & 0x3c);
    default:
        return ide_ioport_read(s->ide, at);
    }

    return 0;
}

static void md_common_write(void *opaque, uint32_t at, uint16_t value)
{
    struct md_s *s = (struct md_s *) opaque;
    at -= s->io_base;

    switch (s->opt & OPT_MODE) {
    case OPT_MODE_MMAP:
        if ((at & ~0x3ff) == 0x400)
            at = 0;
        break;
    case OPT_MODE_IOMAP16:
        at &= 0xf;
        break;
    case OPT_MODE_IOMAP1:
        if ((at & ~0xf) == 0x3f0)
            at -= 0x3e8;
        else if ((at & ~0xf) == 0x1f0)
            at -= 0x1f0;
        break;
    case OPT_MODE_IOMAP2:
        if ((at & ~0xf) == 0x370)
            at -= 0x368;
        else if ((at & ~0xf) == 0x170)
            at -= 0x170;
    }

    switch (at) {
    case 0x0:	/* Even WR Data */
    case 0x8:
        ide_data_writew(s->ide, 0, value);
        break;

        /* TODO: 8-bit accesses */
        if (s->cycle)
            ide_data_writew(s->ide, 0, s->io | (value << 8));
        else
            s->io = value & 0xff;
        s->cycle = !s->cycle;
        break;
    case 0x9:
        s->io = value & 0xff;
        s->cycle = !s->cycle;
        break;
    case 0xd:	/* Features */
        ide_ioport_write(s->ide, 0x1, value);
        break;
    case 0xe:	/* Device Control */
        s->ctrl = value;
        if (value & CTRL_SRST)
            md_reset(s);
        md_interrupt_update(s);
        break;
    default:
        if (s->stat & STAT_PWRDWN) {
            s->pins |= PINS_CRDY;
            s->stat &= ~STAT_PWRDWN;
        }
        ide_ioport_write(s->ide, at, value);
    }
}

static void md_save(QEMUFile *f, void *opaque)
{
    struct md_s *s = (struct md_s *) opaque;
    int i;
    uint8_t drive1_selected;

    qemu_put_8s(f, &s->opt);
    qemu_put_8s(f, &s->stat);
    qemu_put_8s(f, &s->pins);

    qemu_put_8s(f, &s->ctrl);
    qemu_put_be16s(f, &s->io);
    qemu_put_byte(f, s->cycle);

    drive1_selected = (s->ide->cur_drive != s->ide);
    qemu_put_8s(f, &s->ide->cmd);
    qemu_put_8s(f, &drive1_selected);

    for (i = 0; i < 2; i ++)
        ide_save(f, &s->ide[i]);
}

static int md_load(QEMUFile *f, void *opaque, int version_id)
{
    struct md_s *s = (struct md_s *) opaque;
    int i;
    uint8_t drive1_selected;

    qemu_get_8s(f, &s->opt);
    qemu_get_8s(f, &s->stat);
    qemu_get_8s(f, &s->pins);

    qemu_get_8s(f, &s->ctrl);
    qemu_get_be16s(f, &s->io);
    s->cycle = qemu_get_byte(f);

    qemu_get_8s(f, &s->ide->cmd);
    qemu_get_8s(f, &drive1_selected);
    s->ide->cur_drive = &s->ide[(drive1_selected != 0)];

    for (i = 0; i < 2; i ++)
        ide_load(f, &s->ide[i]);

    return 0;
}

static const uint8_t dscm1xxxx_cis[0x14a] = {
    [0x000] = CISTPL_DEVICE,	/* 5V Device Information */
    [0x002] = 0x03,		/* Tuple length = 4 bytes */
    [0x004] = 0xdb,		/* ID: DTYPE_FUNCSPEC, non WP, DSPEED_150NS */
    [0x006] = 0x01,		/* Size = 2K bytes */
    [0x008] = CISTPL_ENDMARK,

    [0x00a] = CISTPL_DEVICE_OC,	/* Additional Device Information */
    [0x00c] = 0x04,		/* Tuple length = 4 byest */
    [0x00e] = 0x03,		/* Conditions: Ext = 0, Vcc 3.3V, MWAIT = 1 */
    [0x010] = 0xdb,		/* ID: DTYPE_FUNCSPEC, non WP, DSPEED_150NS */
    [0x012] = 0x01,		/* Size = 2K bytes */
    [0x014] = CISTPL_ENDMARK,

    [0x016] = CISTPL_JEDEC_C,	/* JEDEC ID */
    [0x018] = 0x02,		/* Tuple length = 2 bytes */
    [0x01a] = 0xdf,		/* PC Card ATA with no Vpp required */
    [0x01c] = 0x01,

    [0x01e] = CISTPL_MANFID,	/* Manufacture ID */
    [0x020] = 0x04,		/* Tuple length = 4 bytes */
    [0x022] = 0xa4,		/* TPLMID_MANF = 00a4 (IBM) */
    [0x024] = 0x00,
    [0x026] = 0x00,		/* PLMID_CARD = 0000 */
    [0x028] = 0x00,

    [0x02a] = CISTPL_VERS_1,	/* Level 1 Version */
    [0x02c] = 0x12,		/* Tuple length = 23 bytes */
    [0x02e] = 0x04,		/* Major Version = JEIDA 4.2 / PCMCIA 2.1 */
    [0x030] = 0x01,		/* Minor Version = 1 */
    [0x032] = 'I',
    [0x034] = 'B',
    [0x036] = 'M',
    [0x038] = 0x00,
    [0x03a] = 'm',
    [0x03c] = 'i',
    [0x03e] = 'c',
    [0x040] = 'r',
    [0x042] = 'o',
    [0x044] = 'd',
    [0x046] = 'r',
    [0x048] = 'i',
    [0x04a] = 'v',
    [0x04c] = 'e',
    [0x04e] = 0x00,
    [0x050] = CISTPL_ENDMARK,

    [0x052] = CISTPL_FUNCID,	/* Function ID */
    [0x054] = 0x02,		/* Tuple length = 2 bytes */
    [0x056] = 0x04,		/* TPLFID_FUNCTION = Fixed Disk */
    [0x058] = 0x01,		/* TPLFID_SYSINIT: POST = 1, ROM = 0 */

    [0x05a] = CISTPL_FUNCE,	/* Function Extension */
    [0x05c] = 0x02,		/* Tuple length = 2 bytes */
    [0x05e] = 0x01,		/* TPLFE_TYPE = Disk Device Interface */
    [0x060] = 0x01,		/* TPLFE_DATA = PC Card ATA Interface */

    [0x062] = CISTPL_FUNCE,	/* Function Extension */
    [0x064] = 0x03,		/* Tuple length = 3 bytes */
    [0x066] = 0x02,		/* TPLFE_TYPE = Basic PC Card ATA Interface */
    [0x068] = 0x08,		/* TPLFE_DATA: Rotating, Unique, Single */
    [0x06a] = 0x0f,		/* TPLFE_DATA: Sleep, Standby, Idle, Auto */

    [0x06c] = CISTPL_CONFIG,	/* Configuration */
    [0x06e] = 0x05,		/* Tuple length = 5 bytes */
    [0x070] = 0x01,		/* TPCC_RASZ = 2 bytes, TPCC_RMSZ = 1 byte */
    [0x072] = 0x07,		/* TPCC_LAST = 7 */
    [0x074] = 0x00,		/* TPCC_RADR = 0200 */
    [0x076] = 0x02,
    [0x078] = 0x0f,		/* TPCC_RMSK = 200, 202, 204, 206 */

    [0x07a] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x07c] = 0x0b,		/* Tuple length = 11 bytes */
    [0x07e] = 0xc0,		/* TPCE_INDX = Memory Mode, Default, Iface */
    [0x080] = 0xc0,		/* TPCE_IF = Memory, no BVDs, no WP, READY */
    [0x082] = 0xa1,		/* TPCE_FS = Vcc only, no I/O, Memory, Misc */
    [0x084] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x086] = 0x55,		/* NomV: 5.0 V */
    [0x088] = 0x4d,		/* MinV: 4.5 V */
    [0x08a] = 0x5d,		/* MaxV: 5.5 V */
    [0x08c] = 0x4e,		/* Peakl: 450 mA */
    [0x08e] = 0x08,		/* TPCE_MS = 1 window, 1 byte, Host address */
    [0x090] = 0x00,		/* Window descriptor: Window length = 0 */
    [0x092] = 0x20,		/* TPCE_MI: support power down mode, RW */

    [0x094] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x096] = 0x06,		/* Tuple length = 6 bytes */
    [0x098] = 0x00,		/* TPCE_INDX = Memory Mode, no Default */
    [0x09a] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x09c] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x09e] = 0xb5,		/* NomV: 3.3 V */
    [0x0a0] = 0x1e,
    [0x0a2] = 0x3e,		/* Peakl: 350 mA */

    [0x0a4] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0a6] = 0x0d,		/* Tuple length = 13 bytes */
    [0x0a8] = 0xc1,		/* TPCE_INDX = I/O and Memory Mode, Default */
    [0x0aa] = 0x41,		/* TPCE_IF = I/O and Memory, no BVD, no WP */
    [0x0ac] = 0x99,		/* TPCE_FS = Vcc only, I/O, Interrupt, Misc */
    [0x0ae] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x0b0] = 0x55,		/* NomV: 5.0 V */
    [0x0b2] = 0x4d,		/* MinV: 4.5 V */
    [0x0b4] = 0x5d,		/* MaxV: 5.5 V */
    [0x0b6] = 0x4e,		/* Peakl: 450 mA */
    [0x0b8] = 0x64,		/* TPCE_IO = 16-byte boundary, 16/8 accesses */
    [0x0ba] = 0xf0,		/* TPCE_IR =  MASK, Level, Pulse, Share */
    [0x0bc] = 0xff,		/* IRQ0..IRQ7 supported */
    [0x0be] = 0xff,		/* IRQ8..IRQ15 supported */
    [0x0c0] = 0x20,		/* TPCE_MI = support power down mode */

    [0x0c2] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0c4] = 0x06,		/* Tuple length = 6 bytes */
    [0x0c6] = 0x01,		/* TPCE_INDX = I/O and Memory Mode */
    [0x0c8] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x0ca] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x0cc] = 0xb5,		/* NomV: 3.3 V */
    [0x0ce] = 0x1e,
    [0x0d0] = 0x3e,		/* Peakl: 350 mA */

    [0x0d2] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0d4] = 0x12,		/* Tuple length = 18 bytes */
    [0x0d6] = 0xc2,		/* TPCE_INDX = I/O Primary Mode */
    [0x0d8] = 0x41,		/* TPCE_IF = I/O and Memory, no BVD, no WP */
    [0x0da] = 0x99,		/* TPCE_FS = Vcc only, I/O, Interrupt, Misc */
    [0x0dc] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x0de] = 0x55,		/* NomV: 5.0 V */
    [0x0e0] = 0x4d,		/* MinV: 4.5 V */
    [0x0e2] = 0x5d,		/* MaxV: 5.5 V */
    [0x0e4] = 0x4e,		/* Peakl: 450 mA */
    [0x0e6] = 0xea,		/* TPCE_IO = 1K boundary, 16/8 access, Range */
    [0x0e8] = 0x61,		/* Range: 2 fields, 2 bytes addr, 1 byte len */
    [0x0ea] = 0xf0,		/* Field 1 address = 0x01f0 */
    [0x0ec] = 0x01,
    [0x0ee] = 0x07,		/* Address block length = 8 */
    [0x0f0] = 0xf6,		/* Field 2 address = 0x03f6 */
    [0x0f2] = 0x03,
    [0x0f4] = 0x01,		/* Address block length = 2 */
    [0x0f6] = 0xee,		/* TPCE_IR = IRQ E, Level, Pulse, Share */
    [0x0f8] = 0x20,		/* TPCE_MI = support power down mode */

    [0x0fa] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x0fc] = 0x06,		/* Tuple length = 6 bytes */
    [0x0fe] = 0x02,		/* TPCE_INDX = I/O Primary Mode, no Default */
    [0x100] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x102] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x104] = 0xb5,		/* NomV: 3.3 V */
    [0x106] = 0x1e,
    [0x108] = 0x3e,		/* Peakl: 350 mA */

    [0x10a] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x10c] = 0x12,		/* Tuple length = 18 bytes */
    [0x10e] = 0xc3,		/* TPCE_INDX = I/O Secondary Mode, Default */
    [0x110] = 0x41,		/* TPCE_IF = I/O and Memory, no BVD, no WP */
    [0x112] = 0x99,		/* TPCE_FS = Vcc only, I/O, Interrupt, Misc */
    [0x114] = 0x27,		/* NomV = 1, MinV = 1, MaxV = 1, Peakl = 1 */
    [0x116] = 0x55,		/* NomV: 5.0 V */
    [0x118] = 0x4d,		/* MinV: 4.5 V */
    [0x11a] = 0x5d,		/* MaxV: 5.5 V */
    [0x11c] = 0x4e,		/* Peakl: 450 mA */
    [0x11e] = 0xea,		/* TPCE_IO = 1K boundary, 16/8 access, Range */
    [0x120] = 0x61,		/* Range: 2 fields, 2 byte addr, 1 byte len */
    [0x122] = 0x70,		/* Field 1 address = 0x0170 */
    [0x124] = 0x01,
    [0x126] = 0x07,		/* Address block length = 8 */
    [0x128] = 0x76,		/* Field 2 address = 0x0376 */
    [0x12a] = 0x03,
    [0x12c] = 0x01,		/* Address block length = 2 */
    [0x12e] = 0xee,		/* TPCE_IR = IRQ E, Level, Pulse, Share */
    [0x130] = 0x20,		/* TPCE_MI = support power down mode */

    [0x132] = CISTPL_CFTABLE_ENTRY,	/* 16-bit PC Card Configuration */
    [0x134] = 0x06,		/* Tuple length = 6 bytes */
    [0x136] = 0x03,		/* TPCE_INDX = I/O Secondary Mode */
    [0x138] = 0x01,		/* TPCE_FS = Vcc only, no I/O, no Memory */
    [0x13a] = 0x21,		/* NomV = 1, MinV = 0, MaxV = 0, Peakl = 1 */
    [0x13c] = 0xb5,		/* NomV: 3.3 V */
    [0x13e] = 0x1e,
    [0x140] = 0x3e,		/* Peakl: 350 mA */

    [0x142] = CISTPL_NO_LINK,	/* No Link */
    [0x144] = 0x00,		/* Tuple length = 0 bytes */

    [0x146] = CISTPL_END,	/* Tuple End */
};

static int dscm1xxxx_attach(void *opaque)
{
    struct md_s *md = (struct md_s *) opaque;
    md->card.attr_read = md_attr_read;
    md->card.attr_write = md_attr_write;
    md->card.common_read = md_common_read;
    md->card.common_write = md_common_write;
    md->card.io_read = md_common_read;
    md->card.io_write = md_common_write;

    md->attr_base = md->card.cis[0x74] | (md->card.cis[0x76] << 8);
    md->io_base = 0x0;

    md_reset(md);
    md_interrupt_update(md);

    md->card.slot->card_string = "DSCM-1xxxx Hitachi Microdrive";
    return 0;
}

static int dscm1xxxx_detach(void *opaque)
{
    struct md_s *md = (struct md_s *) opaque;
    md_reset(md);
    return 0;
}

struct pcmcia_card_s *dscm1xxxx_init(BlockDriverState *bdrv)
{
    struct md_s *md = (struct md_s *) qemu_mallocz(sizeof(struct md_s));
    md->card.state = md;
    md->card.attach = dscm1xxxx_attach;
    md->card.detach = dscm1xxxx_detach;
    md->card.cis = dscm1xxxx_cis;
    md->card.cis_len = sizeof(dscm1xxxx_cis);

    ide_init2(md->ide, bdrv, 0, qemu_allocate_irqs(md_set_irq, md, 1)[0]);
    md->ide->is_cf = 1;
    md->ide->mdata_size = METADATA_SIZE;
    md->ide->mdata_storage = (uint8_t *) qemu_mallocz(METADATA_SIZE);

    register_savevm("microdrive", -1, 0, md_save, md_load, md);

    return &md->card;
}
