/*
 * ide device definitions
 *
 * Copyright (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IDE_DEV_H
#define IDE_DEV_H

#include "sysemu/dma.h"
#include "hw/qdev-properties.h"
#include "hw/block/block.h"

typedef struct IDEDevice IDEDevice;
typedef struct IDEState IDEState;
typedef struct IDEBus IDEBus;

typedef void EndTransferFunc(IDEState *);

#define MAX_IDE_DEVS 2

#define TYPE_IDE_DEVICE "ide-device"
OBJECT_DECLARE_TYPE(IDEDevice, IDEDeviceClass, IDE_DEVICE)

typedef enum { IDE_HD, IDE_CD, IDE_CFATA } IDEDriveKind;

struct unreported_events {
    bool eject_request;
    bool new_media;
};

enum ide_dma_cmd {
    IDE_DMA_READ = 0,
    IDE_DMA_WRITE,
    IDE_DMA_TRIM,
    IDE_DMA_ATAPI,
    IDE_DMA__COUNT
};

/* NOTE: IDEState represents in fact one drive */
struct IDEState {
    IDEBus *bus;
    uint8_t unit;
    /* ide config */
    IDEDriveKind drive_kind;
    int drive_heads, drive_sectors;
    int cylinders, heads, sectors, chs_trans;
    int64_t nb_sectors;
    int mult_sectors;
    int identify_set;
    uint8_t identify_data[512];
    int drive_serial;
    char drive_serial_str[21];
    char drive_model_str[41];
    bool win2k_install_hack;
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

    bool io8;
    bool reset_reverts;

    /* set for lba48 access */
    uint8_t lba48;
    BlockBackend *blk;
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
    int32_t io_buffer_index;
    int lba;
    int cd_sector_size;
    int atapi_dma; /* true if dma is requested for the packet cmd */
    BlockAcctCookie acct;
    BlockAIOCB *pio_aiocb;
    QEMUIOVector qiov;
    QLIST_HEAD(, IDEBufferedRequest) buffered_requests;
    /* ATA DMA state */
    uint64_t io_buffer_offset;
    int32_t io_buffer_size;
    QEMUSGList sg;
    /* PIO transfer handling */
    int req_nb_sectors; /* number of sectors per interrupt */
    EndTransferFunc *end_transfer_func;
    uint8_t *data_ptr;
    uint8_t *data_end;
    uint8_t *io_buffer;
    /* PIO save/restore */
    int32_t io_buffer_total_len;
    int32_t cur_io_buffer_offset;
    int32_t cur_io_buffer_len;
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

struct IDEDeviceClass {
    DeviceClass parent_class;
    void (*realize)(IDEDevice *dev, Error **errp);
};

struct IDEDevice {
    DeviceState qdev;
    uint32_t unit;
    BlockConf conf;
    int chs_trans;
    char *version;
    char *serial;
    char *model;
    uint64_t wwn;
    /*
     * 0x0000        - rotation rate not reported
     * 0x0001        - non-rotating medium (SSD)
     * 0x0002-0x0400 - reserved
     * 0x0401-0xffe  - rotations per minute
     * 0xffff        - reserved
     */
    uint16_t rotation_rate;
    bool win2k_install_hack;
};

typedef struct IDEDrive {
    IDEDevice dev;
} IDEDrive;

#define DEFINE_IDE_DEV_PROPERTIES()                     \
    DEFINE_BLOCK_PROPERTIES(IDEDrive, dev.conf),        \
    DEFINE_BLOCK_ERROR_PROPERTIES(IDEDrive, dev.conf),  \
    DEFINE_PROP_STRING("ver",  IDEDrive, dev.version),  \
    DEFINE_PROP_UINT64("wwn",  IDEDrive, dev.wwn, 0),   \
    DEFINE_PROP_STRING("serial",  IDEDrive, dev.serial),\
    DEFINE_PROP_STRING("model", IDEDrive, dev.model)

void ide_dev_initfn(IDEDevice *dev, IDEDriveKind kind, Error **errp);

void ide_drive_get(DriveInfo **hd, int max_bus);

#endif
