#ifndef HW_IDE_INTERNAL_H
#define HW_IDE_INTERNAL_H

/*
 * QEMU IDE Emulation -- internal header file
 * only files in hw/ide/ are supposed to include this file.
 * non-internal declarations are in hw/ide.h
 */
#include <hw/ide.h>
#include <hw/isa/isa.h>
#include "sysemu/dma.h"
#include "sysemu/sysemu.h"
#include "hw/block/block.h"
#include "block/scsi.h"

/* debug IDE devices */
//#define DEBUG_IDE
//#define DEBUG_IDE_ATAPI
//#define DEBUG_AIO
#define USE_DMA_CDROM

typedef struct IDEBus IDEBus;
typedef struct IDEDevice IDEDevice;
typedef struct IDEState IDEState;
typedef struct IDEDMA IDEDMA;
typedef struct IDEDMAOps IDEDMAOps;

#define TYPE_IDE_BUS "IDE"
#define IDE_BUS(obj) OBJECT_CHECK(IDEBus, (obj), TYPE_IDE_BUS)

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

/* ACS-2 T13/2015-D Table B.2 Command codes */
#define WIN_NOP				0x00
/* reserved                             0x01..0x02 */
#define CFA_REQ_EXT_ERROR_CODE		0x03 /* CFA Request Extended Error Code */
/* reserved                             0x04..0x05 */
#define WIN_DSM                         0x06
/* reserved                             0x07 */
#define WIN_DEVICE_RESET		0x08
/* reserved                             0x09..0x0a */
/* REQUEST SENSE DATA EXT               0x0B */
/* reserved                             0x0C..0x0F */
#define WIN_RECAL                       0x10 /* obsolete since ATA4 */
/* obsolete since ATA3, retired in ATA4 0x11..0x1F */
#define WIN_READ			0x20 /* 28-Bit */
#define WIN_READ_ONCE                   0x21 /* 28-Bit w/o retries, obsolete since ATA5 */
/* obsolete since ATA4                  0x22..0x23 */
#define WIN_READ_EXT			0x24 /* 48-Bit */
#define WIN_READDMA_EXT			0x25 /* 48-Bit */
#define WIN_READDMA_QUEUED_EXT          0x26 /* 48-Bit, obsolete since ACS2 */
#define WIN_READ_NATIVE_MAX_EXT		0x27 /* 48-Bit */
/* reserved                             0x28 */
#define WIN_MULTREAD_EXT		0x29 /* 48-Bit */
/* READ STREAM DMA EXT                  0x2A */
/* READ STREAM EXT                      0x2B */
/* reserved                             0x2C..0x2E */
/* READ LOG EXT                         0x2F */
#define WIN_WRITE			0x30 /* 28-Bit */
#define WIN_WRITE_ONCE                  0x31 /* 28-Bit w/o retries, obsolete since ATA5 */
/* obsolete since ATA4                  0x32..0x33 */
#define WIN_WRITE_EXT			0x34 /* 48-Bit */
#define WIN_WRITEDMA_EXT		0x35 /* 48-Bit */
#define WIN_WRITEDMA_QUEUED_EXT		0x36 /* 48-Bit */
#define WIN_SET_MAX_EXT                 0x37 /* 48-Bit, obsolete since ACS2 */
#define WIN_SET_MAX_EXT			0x37 /* 48-Bit */
#define CFA_WRITE_SECT_WO_ERASE		0x38 /* CFA Write Sectors without erase */
#define WIN_MULTWRITE_EXT		0x39 /* 48-Bit */
/* WRITE STREAM DMA EXT                 0x3A */
/* WRITE STREAM EXT                     0x3B */
#define WIN_WRITE_VERIFY                0x3C /* 28-Bit, obsolete since ATA4 */
/* WRITE DMA FUA EXT                    0x3D */
/* obsolete since ACS2                  0x3E */
/* WRITE LOG EXT                        0x3F */
#define WIN_VERIFY			0x40 /* 28-Bit - Read Verify Sectors */
#define WIN_VERIFY_ONCE                 0x41 /* 28-Bit - w/o retries, obsolete since ATA5 */
#define WIN_VERIFY_EXT			0x42 /* 48-Bit */
/* reserved                             0x43..0x44 */
/* WRITE UNCORRECTABLE EXT              0x45 */
/* reserved                             0x46 */
/* READ LOG DMA EXT                     0x47 */
/* reserved                             0x48..0x4F */
/* obsolete since ATA4                  0x50 */
/* CONFIGURE STREAM                     0x51 */
/* reserved                             0x52..0x56 */
/* WRITE LOG DMA EXT                    0x57 */
/* reserved                             0x58..0x5A */
/* TRUSTED NON DATA                     0x5B */
/* TRUSTED RECEIVE                      0x5C */
/* TRUSTED RECEIVE DMA                  0x5D */
/* TRUSTED SEND                         0x5E */
/* TRUSTED SEND DMA                     0x5F */
/* READ FPDMA QUEUED                    0x60 */
/* WRITE FPDMA QUEUED                   0x61 */
/* reserved                             0x62->0x6F */
#define WIN_SEEK                        0x70 /* obsolete since ATA7 */
/* reserved                             0x71-0x7F */
/* vendor specific                      0x80-0x86 */
#define CFA_TRANSLATE_SECTOR		0x87 /* CFA Translate Sector */
/* vendor specific                      0x88-0x8F */
#define WIN_DIAGNOSE			0x90
#define WIN_SPECIFY                     0x91 /* set drive geometry translation, obsolete since ATA6 */
#define WIN_DOWNLOAD_MICROCODE		0x92
/* DOWNLOAD MICROCODE DMA               0x93 */
#define WIN_STANDBYNOW2                 0x94 /* retired in ATA4 */
#define WIN_IDLEIMMEDIATE2              0x95 /* force drive to become "ready", retired in ATA4 */
#define WIN_STANDBY2                    0x96 /* retired in ATA4 */
#define WIN_SETIDLE2                    0x97 /* retired in ATA4 */
#define WIN_CHECKPOWERMODE2             0x98 /* retired in ATA4 */
#define WIN_SLEEPNOW2                   0x99 /* retired in ATA4 */
/* vendor specific                      0x9A */
/* reserved                             0x9B..0x9F */
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* identify ATAPI device	*/
#define WIN_QUEUED_SERVICE              0xA2 /* obsolete since ACS2 */
/* reserved                             0xA3..0xAF */
#define WIN_SMART			0xB0 /* self-monitoring and reporting */
/* Device Configuration Overlay         0xB1 */
/* reserved                             0xB2..0xB3 */
/* Sanitize Device                      0xB4 */
/* reserved                             0xB5 */
/* NV Cache                             0xB6 */
/* reserved for CFA                     0xB7..0xBB */
#define CFA_ACCESS_METADATA_STORAGE	0xB8
/* reserved                             0xBC..0xBF */
#define CFA_ERASE_SECTORS       	0xC0 /* microdrives implement as NOP */
/* vendor specific                      0xC1..0xC3 */
#define WIN_MULTREAD			0xC4 /* read sectors using multiple mode*/
#define WIN_MULTWRITE			0xC5 /* write sectors using multiple mode */
#define WIN_SETMULT			0xC6 /* enable/disable multiple mode */
#define WIN_READDMA_QUEUED              0xC7 /* read sectors using Queued DMA transfers, obsolete since ACS2 */
#define WIN_READDMA			0xC8 /* read sectors using DMA transfers */
#define WIN_READDMA_ONCE                0xC9 /* 28-Bit - w/o retries, obsolete since ATA5 */
#define WIN_WRITEDMA			0xCA /* write sectors using DMA transfers */
#define WIN_WRITEDMA_ONCE               0xCB /* 28-Bit - w/o retries, obsolete since ATA5 */
#define WIN_WRITEDMA_QUEUED		0xCC /* write sectors using Queued DMA transfers, obsolete since ACS2 */
#define CFA_WRITE_MULTI_WO_ERASE	0xCD /* CFA Write multiple without erase */
/* WRITE MULTIPLE FUA EXT               0xCE */
/* reserved                             0xCF..0xDO */
/* CHECK MEDIA CARD TYPE                0xD1 */
/* reserved for media card pass through 0xD2..0xD4 */
/* reserved                             0xD5..0xD9 */
#define WIN_GETMEDIASTATUS              0xDA /* obsolete since ATA8 */
/* obsolete since ATA3, retired in ATA4 0xDB..0xDD */
#define WIN_DOORLOCK                    0xDE /* lock door on removable drives, obsolete since ATA8 */
#define WIN_DOORUNLOCK                  0xDF /* unlock door on removable drives, obsolete since ATA8 */
#define WIN_STANDBYNOW1			0xE0
#define WIN_IDLEIMMEDIATE		0xE1 /* force drive to become "ready" */
#define WIN_STANDBY             	0xE2 /* Set device in Standby Mode */
#define WIN_SETIDLE1			0xE3
#define WIN_READ_BUFFER			0xE4 /* force read only 1 sector */
#define WIN_CHECKPOWERMODE1		0xE5
#define WIN_SLEEPNOW1			0xE6
#define WIN_FLUSH_CACHE			0xE7
#define WIN_WRITE_BUFFER		0xE8 /* force write only 1 sector */
/* READ BUFFER DMA                      0xE9 */
#define WIN_FLUSH_CACHE_EXT		0xEA /* 48-Bit */
/* WRITE BUFFER DMA                     0xEB */
#define WIN_IDENTIFY			0xEC /* ask drive to identify itself	*/
#define WIN_MEDIAEJECT                  0xED /* obsolete since ATA8 */
/* obsolete since ATA4                  0xEE */
#define WIN_SETFEATURES			0xEF /* set special drive features */
#define IBM_SENSE_CONDITION             0xF0 /* measure disk temperature, vendor specific */
#define WIN_SECURITY_SET_PASS		0xF1
#define WIN_SECURITY_UNLOCK		0xF2
#define WIN_SECURITY_ERASE_PREPARE	0xF3
#define WIN_SECURITY_ERASE_UNIT		0xF4
#define WIN_SECURITY_FREEZE_LOCK	0xF5
#define CFA_WEAR_LEVEL                  0xF5 /* microdrives implement as NOP; not specified in T13! */
#define WIN_SECURITY_DISABLE		0xF6
/* vendor specific                      0xF7 */
#define WIN_READ_NATIVE_MAX		0xF8 /* return the native maximum address */
#define WIN_SET_MAX			0xF9
/* vendor specific                      0xFA..0xFF */

/* set to 1 set disable mult support */
#define MAX_MULT_SECTORS 16

#define IDE_DMA_BUF_SECTORS 256

/* feature values for Data Set Management */
#define DSM_TRIM                        0x01

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
#define ASC_MEDIA_REMOVAL_PREVENTED          0x53

#define CFA_NO_ERROR            0x00
#define CFA_MISC_ERROR          0x09
#define CFA_INVALID_COMMAND     0x20
#define CFA_INVALID_ADDRESS     0x21
#define CFA_ADDRESS_OVERFLOW    0x2f

#define SMART_READ_DATA       0xd0
#define SMART_READ_THRESH     0xd1
#define SMART_ATTR_AUTOSAVE   0xd2
#define SMART_SAVE_ATTR       0xd3
#define SMART_EXECUTE_OFFLINE 0xd4
#define SMART_READ_LOG        0xd5
#define SMART_WRITE_LOG       0xd6
#define SMART_ENABLE          0xd8
#define SMART_DISABLE         0xd9
#define SMART_STATUS          0xda

typedef enum { IDE_HD, IDE_CD, IDE_CFATA } IDEDriveKind;

typedef void EndTransferFunc(IDEState *);

typedef void DMAStartFunc(IDEDMA *, IDEState *, BlockDriverCompletionFunc *);
typedef void DMAVoidFunc(IDEDMA *);
typedef int DMAIntFunc(IDEDMA *, int);
typedef void DMAStopFunc(IDEDMA *, bool);
typedef void DMARestartFunc(void *, int, RunState);

struct unreported_events {
    bool eject_request;
    bool new_media;
};

enum ide_dma_cmd {
    IDE_DMA_READ,
    IDE_DMA_WRITE,
    IDE_DMA_TRIM,
};

#define ide_cmd_is_read(s) \
	((s)->dma_cmd == IDE_DMA_READ)

/* NOTE: IDEState represents in fact one drive */
struct IDEState {
    IDEBus *bus;
    uint8_t unit;
    /* ide config */
    IDEDriveKind drive_kind;
    int cylinders, heads, sectors, chs_trans;
    int64_t nb_sectors;
    int mult_sectors;
    int identify_set;
    uint8_t identify_data[512];
    int drive_serial;
    char drive_serial_str[21];
    char drive_model_str[41];
    uint64_t wwn;
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

    /* set for lba48 access */
    uint8_t lba48;
    BlockDriverState *bs;
    char version[9];
    /* ATAPI specific */
    struct unreported_events events;
    uint8_t sense_key;
    uint8_t asc;
    bool tray_open;
    bool tray_locked;
    uint8_t cdrom_changed;
    int packet_transfer_size;
    int elementary_transfer_size;
    int io_buffer_index;
    int lba;
    int cd_sector_size;
    int atapi_dma; /* true if dma is requested for the packet cmd */
    BlockAcctCookie acct;
    BlockDriverAIOCB *pio_aiocb;
    struct iovec iov;
    QEMUIOVector qiov;
    /* ATA DMA state */
    int io_buffer_offset;
    int io_buffer_size;
    QEMUSGList sg;
    /* PIO transfer handling */
    int req_nb_sectors; /* number of sectors per interrupt */
    EndTransferFunc *end_transfer_func;
    uint8_t *data_ptr;
    uint8_t *data_end;
    uint8_t *io_buffer;
    /* PIO save/restore */
    int32_t io_buffer_total_len;
    int cur_io_buffer_offset;
    int cur_io_buffer_len;
    uint8_t end_transfer_fn_idx;
    QEMUTimer *sector_write_timer; /* only used for win2k install hack */
    uint32_t irq_count; /* counts IRQs when using win2k install hack */
    /* CF-ATA extended error */
    uint8_t ext_error;
    /* CF-ATA metadata storage */
    uint32_t mdata_size;
    uint8_t *mdata_storage;
    int media_changed;
    enum ide_dma_cmd dma_cmd;
    /* SMART */
    uint8_t smart_enabled;
    uint8_t smart_autosave;
    int smart_errors;
    uint8_t smart_selftest_count;
    uint8_t *smart_selftest_data;
    /* AHCI */
    int ncq_queues;
};

struct IDEDMAOps {
    DMAStartFunc *start_dma;
    DMAVoidFunc *start_transfer;
    DMAIntFunc *prepare_buf;
    DMAIntFunc *rw_buf;
    DMAIntFunc *set_unit;
    DMAStopFunc *set_inactive;
    DMAVoidFunc *async_cmd_done;
    DMARestartFunc *restart_cb;
    DMAVoidFunc *reset;
};

struct IDEDMA {
    const struct IDEDMAOps *ops;
    struct iovec iov;
    QEMUIOVector qiov;
    BlockDriverAIOCB *aiocb;
};

struct IDEBus {
    BusState qbus;
    IDEDevice *master;
    IDEDevice *slave;
    IDEState ifs[2];
    int bus_id;
    int max_units;
    IDEDMA *dma;
    uint8_t unit;
    uint8_t cmd;
    qemu_irq irq;

    int error_status;
};

#define TYPE_IDE_DEVICE "ide-device"
#define IDE_DEVICE(obj) \
     OBJECT_CHECK(IDEDevice, (obj), TYPE_IDE_DEVICE)
#define IDE_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(IDEDeviceClass, (klass), TYPE_IDE_DEVICE)
#define IDE_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(IDEDeviceClass, (obj), TYPE_IDE_DEVICE)

typedef struct IDEDeviceClass {
    DeviceClass parent_class;
    int (*init)(IDEDevice *dev);
} IDEDeviceClass;

struct IDEDevice {
    DeviceState qdev;
    uint32_t unit;
    BlockConf conf;
    int chs_trans;
    char *version;
    char *serial;
    char *model;
    uint64_t wwn;
};

/* FIXME These are not status register bits */
#define BM_STATUS_DMA_RETRY  0x08
#define BM_STATUS_PIO_RETRY  0x10
#define BM_STATUS_RETRY_READ  0x20
#define BM_STATUS_RETRY_FLUSH 0x40
#define BM_STATUS_RETRY_TRIM 0x80

static inline IDEState *idebus_active_if(IDEBus *bus)
{
    return bus->ifs + bus->unit;
}

static inline void ide_set_irq(IDEBus *bus)
{
    if (!(bus->cmd & IDE_CMD_DISABLE_IRQ)) {
        qemu_irq_raise(bus->irq);
    }
}

/* hw/ide/core.c */
extern const VMStateDescription vmstate_ide_bus;

#define VMSTATE_IDE_BUS(_field, _state)                          \
    VMSTATE_STRUCT(_field, _state, 1, vmstate_ide_bus, IDEBus)

#define VMSTATE_IDE_BUS_ARRAY(_field, _state, _num)              \
    VMSTATE_STRUCT_ARRAY(_field, _state, _num, 1, vmstate_ide_bus, IDEBus)

extern const VMStateDescription vmstate_ide_drive;

#define VMSTATE_IDE_DRIVES(_field, _state) \
    VMSTATE_STRUCT_ARRAY(_field, _state, 2, 3, vmstate_ide_drive, IDEState)

void ide_bus_reset(IDEBus *bus);
int64_t ide_get_sector(IDEState *s);
void ide_set_sector(IDEState *s, int64_t sector_num);

void ide_start_dma(IDEState *s, BlockDriverCompletionFunc *cb);
void ide_dma_error(IDEState *s);

void ide_atapi_cmd_ok(IDEState *s);
void ide_atapi_cmd_error(IDEState *s, int sense_key, int asc);
void ide_atapi_io_error(IDEState *s, int ret);

void ide_ioport_write(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_ioport_read(void *opaque, uint32_t addr1);
uint32_t ide_status_read(void *opaque, uint32_t addr);
void ide_cmd_write(void *opaque, uint32_t addr, uint32_t val);
void ide_data_writew(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_data_readw(void *opaque, uint32_t addr);
void ide_data_writel(void *opaque, uint32_t addr, uint32_t val);
uint32_t ide_data_readl(void *opaque, uint32_t addr);

int ide_init_drive(IDEState *s, BlockDriverState *bs, IDEDriveKind kind,
                   const char *version, const char *serial, const char *model,
                   uint64_t wwn,
                   uint32_t cylinders, uint32_t heads, uint32_t secs,
                   int chs_trans);
void ide_init2(IDEBus *bus, qemu_irq irq);
void ide_init_ioport(IDEBus *bus, ISADevice *isa, int iobase, int iobase2);

void ide_exec_cmd(IDEBus *bus, uint32_t val);
void ide_dma_cb(void *opaque, int ret);
void ide_sector_write(IDEState *s);
void ide_sector_read(IDEState *s);
void ide_flush_cache(IDEState *s);

void ide_transfer_start(IDEState *s, uint8_t *buf, int size,
                        EndTransferFunc *end_transfer_func);
void ide_transfer_stop(IDEState *s);
void ide_set_inactive(IDEState *s, bool more);
BlockDriverAIOCB *ide_issue_trim(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);

/* hw/ide/atapi.c */
void ide_atapi_cmd(IDEState *s);
void ide_atapi_cmd_reply_end(IDEState *s);

/* hw/ide/qdev.c */
void ide_bus_new(IDEBus *idebus, size_t idebus_size, DeviceState *dev,
                 int bus_id, int max_units);
IDEDevice *ide_create_drive(IDEBus *bus, int unit, DriveInfo *drive);

#endif /* HW_IDE_INTERNAL_H */
