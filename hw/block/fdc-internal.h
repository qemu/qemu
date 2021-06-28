/*
 * QEMU Floppy disk emulator (Intel 82078)
 *
 * Copyright (c) 2003, 2007 Jocelyn Mayer
 * Copyright (c) 2008 Hervé Poussineau
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
#ifndef HW_BLOCK_FDC_INTERNAL_H
#define HW_BLOCK_FDC_INTERNAL_H

#include "exec/memory.h"
#include "exec/ioport.h"
#include "hw/block/block.h"
#include "hw/block/fdc.h"
#include "qapi/qapi-types-block.h"

typedef struct FDCtrl FDCtrl;

/* Floppy bus emulation */

typedef struct FloppyBus {
    BusState bus;
    FDCtrl *fdc;
} FloppyBus;

/* Floppy disk drive emulation */

typedef enum FDriveRate {
    FDRIVE_RATE_500K = 0x00,  /* 500 Kbps */
    FDRIVE_RATE_300K = 0x01,  /* 300 Kbps */
    FDRIVE_RATE_250K = 0x02,  /* 250 Kbps */
    FDRIVE_RATE_1M   = 0x03,  /*   1 Mbps */
} FDriveRate;

typedef enum FDriveSize {
    FDRIVE_SIZE_UNKNOWN,
    FDRIVE_SIZE_350,
    FDRIVE_SIZE_525,
} FDriveSize;

typedef struct FDFormat {
    FloppyDriveType drive;
    uint8_t last_sect;
    uint8_t max_track;
    uint8_t max_head;
    FDriveRate rate;
} FDFormat;

typedef enum FDiskFlags {
    FDISK_DBL_SIDES  = 0x01,
} FDiskFlags;

typedef struct FDrive {
    FDCtrl *fdctrl;
    BlockBackend *blk;
    BlockConf *conf;
    /* Drive status */
    FloppyDriveType drive;    /* CMOS drive type        */
    uint8_t perpendicular;    /* 2.88 MB access mode    */
    /* Position */
    uint8_t head;
    uint8_t track;
    uint8_t sect;
    /* Media */
    FloppyDriveType disk;     /* Current disk type      */
    FDiskFlags flags;
    uint8_t last_sect;        /* Nb sector per track    */
    uint8_t max_track;        /* Nb of tracks           */
    uint16_t bps;             /* Bytes per sector       */
    uint8_t ro;               /* Is read-only           */
    uint8_t media_changed;    /* Is media changed       */
    uint8_t media_rate;       /* Data rate of medium    */

    bool media_validated;     /* Have we validated the media? */
} FDrive;

struct FDCtrl {
    MemoryRegion iomem;
    qemu_irq irq;
    /* Controller state */
    QEMUTimer *result_timer;
    int dma_chann;
    uint8_t phase;
    IsaDma *dma;
    /* Controller's identification */
    uint8_t version;
    /* HW */
    uint8_t sra;
    uint8_t srb;
    uint8_t dor;
    uint8_t dor_vmstate; /* only used as temp during vmstate */
    uint8_t tdr;
    uint8_t dsr;
    uint8_t msr;
    uint8_t cur_drv;
    uint8_t status0;
    uint8_t status1;
    uint8_t status2;
    /* Command FIFO */
    uint8_t *fifo;
    int32_t fifo_size;
    uint32_t data_pos;
    uint32_t data_len;
    uint8_t data_state;
    uint8_t data_dir;
    uint8_t eot; /* last wanted sector */
    /* States kept only to be returned back */
    /* precompensation */
    uint8_t precomp_trk;
    uint8_t config;
    uint8_t lock;
    /* Power down config (also with status regB access mode */
    uint8_t pwrd;
    /* Floppy drives */
    FloppyBus bus;
    uint8_t num_floppies;
    FDrive drives[MAX_FD];
    struct {
        FloppyDriveType type;
    } qdev_for_drives[MAX_FD];
    int reset_sensei;
    FloppyDriveType fallback; /* type=auto failure fallback */
    /* Timers state */
    uint8_t timer0;
    uint8_t timer1;
    PortioList portio_list;
};

extern const FDFormat fd_formats[];
extern const VMStateDescription vmstate_fdc;

uint32_t fdctrl_read(void *opaque, uint32_t reg);
void fdctrl_write(void *opaque, uint32_t reg, uint32_t value);
void fdctrl_reset(FDCtrl *fdctrl, int do_irq);
void fdctrl_realize_common(DeviceState *dev, FDCtrl *fdctrl, Error **errp);

int fdctrl_transfer_handler(void *opaque, int nchan, int dma_pos, int dma_len);

void fdctrl_init_drives(FloppyBus *bus, DriveInfo **fds);

#endif
