/*
 * QEMU Floppy disk emulator
 * 
 * Copyright (c) 2003 Jocelyn Mayer
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "vl.h"

/********************************************************/
/* debug Floppy devices */
//#define DEBUG_FLOPPY

#ifdef DEBUG_FLOPPY
#define FLOPPY_DPRINTF(fmt, args...) \
do { printf("FLOPPY: " fmt , ##args); } while (0)
#else
#define FLOPPY_DPRINTF(fmt, args...)
#endif

#define FLOPPY_ERROR(fmt, args...) \
do { printf("FLOPPY ERROR: %s: " fmt, __func__ , ##args); } while (0)

/********************************************************/
/* Floppy drive emulation                               */

/* Will always be a fixed parameter for us */
#define FD_SECTOR_LEN 512
#define FD_SECTOR_SC  2   /* Sector size code */

/* Floppy disk drive emulation */
typedef enum fdisk_type_t {
    FDRIVE_DISK_288   = 0x01, /* 2.88 MB disk           */
    FDRIVE_DISK_144   = 0x02, /* 1.44 MB disk           */
    FDRIVE_DISK_720   = 0x03, /* 720 kB disk            */
    FDRIVE_DISK_NONE  = 0x04, /* No disk                */
} fdisk_type_t;

typedef enum fdrive_type_t {
    FDRIVE_DRV_144  = 0x00,   /* 1.44 MB 3"5 drive      */
    FDRIVE_DRV_288  = 0x01,   /* 2.88 MB 3"5 drive      */
    FDRIVE_DRV_120  = 0x02,   /* 1.2  MB 5"25 drive     */
    FDRIVE_DRV_NONE = 0x03,   /* No drive connected     */
} fdrive_type_t;

typedef struct fdrive_t {
    BlockDriverState *bs;
    /* Drive status */
    fdrive_type_t drive;
    uint8_t motor;            /* on/off                 */
    uint8_t perpendicular;    /* 2.88 MB access mode    */
    uint8_t rv;               /* Revalidated            */
    /* Position */
    uint8_t head;
    uint8_t track;
    uint8_t sect;
    /* Last operation status */
    uint8_t dir;              /* Direction              */
    uint8_t rw;               /* Read/write             */
    /* Media */
    fdisk_type_t disk;        /* Disk type              */
    uint8_t last_sect;        /* Nb sector per track    */
    uint8_t max_track;        /* Nb of tracks           */
    uint8_t ro;               /* Is read-only           */
} fdrive_t;

static void fd_init (fdrive_t *drv)
{
    /* Drive */
    drv->bs = NULL;
//    drv->drive = FDRIVE_DRV_288;
    drv->drive = FDRIVE_DRV_144;
    drv->motor = 0;
    drv->perpendicular = 0;
    drv->rv = 0;
    /* Disk */
    drv->disk = FDRIVE_DISK_NONE;
    drv->last_sect = 1;
    drv->max_track = 0;
}

static int _fd_sector (uint8_t head, uint8_t track,
                        uint8_t sect, uint8_t last_sect)
{
    return (((track * 2) + head) * last_sect) + sect - 1;
}

/* Returns current position, in sectors, for given drive */
static int fd_sector (fdrive_t *drv)
{
    return _fd_sector(drv->head, drv->track, drv->sect, drv->last_sect);
}

static int fd_seek (fdrive_t *drv, uint8_t head, uint8_t track, uint8_t sect,
                    int enable_seek)
{
    uint32_t sector;

    if (track > drv->max_track) {
        FLOPPY_ERROR("try to read %d %02x %02x (max=%d %02x %02x)\n",
                     head, track, sect, 1, drv->max_track, drv->last_sect);
        return 2;
    }
    if (sect > drv->last_sect) {
        FLOPPY_ERROR("try to read %d %02x %02x (max=%d %02x %02x)\n",
                     head, track, sect, 1, drv->max_track, drv->last_sect);
        return 3;
    }
    sector = _fd_sector(head, track, sect, drv->last_sect);
    if (sector != fd_sector(drv)) {
#if 0
        if (!enable_seek) {
            FLOPPY_ERROR("no implicit seek %d %02x %02x (max=%d %02x %02x)\n",
                         head, track, sect, 1, drv->max_track, drv->last_sect);
            return 4;
        }
#endif
        drv->head = head;
        drv->track = track;
        drv->sect = sect;
        return 1;
    }

    return 0;
}

/* Set drive back to track 0 */
static void fd_recalibrate (fdrive_t *drv)
{
    FLOPPY_DPRINTF("recalibrate\n");
    drv->head = 0;
    drv->track = 0;
    drv->sect = 1;
    drv->dir = 1;
    drv->rw = 0;
}

/* Revalidate a disk drive after a disk change */
static void fd_revalidate (fdrive_t *drv, int ro)
{
    int64_t nb_sectors;

    FLOPPY_DPRINTF("revalidate\n");
    drv->rv = 0;
    if (drv->bs != NULL) {
        bdrv_get_geometry(drv->bs, &nb_sectors);
#if 1
        if (nb_sectors > 2880) 
#endif
        {
            /* Pretend we have a 2.88 MB disk */
            drv->disk = FDRIVE_DISK_288;
            drv->last_sect = 36;
            drv->max_track = 80;
#if 1
        } else if (nb_sectors > 1440) {
            /* Pretend we have a 1.44 MB disk */
            drv->disk = FDRIVE_DISK_144;
            drv->last_sect = 18;
            drv->max_track = 80;
        } else {
            /* Pretend we have a 720 kB disk */
            drv->disk = FDRIVE_DISK_720;
            drv->last_sect = 9;
            drv->max_track = 80;
#endif
        }
    } else {
        drv->disk = FDRIVE_DISK_NONE;
        drv->last_sect = 1; /* Avoid eventual divide by 0 bugs */
    }
    drv->ro = ro;
    drv->rv = 1;
}

/* Motor control */
static void fd_start (fdrive_t *drv)
{
    drv->motor = 1;
}

static void fd_stop (fdrive_t *drv)
{
    drv->motor = 0;
}

/* Re-initialise a drives (motor off, repositioned) */
static void fd_reset (fdrive_t *drv)
{
    fd_stop(drv);
    fd_recalibrate(drv);
}

/********************************************************/
/* Intel 82078 floppy disk controler emulation          */

static void fdctrl_reset (int do_irq);
static void fdctrl_reset_fifo (void);
static int fdctrl_transfer_handler (void *opaque, target_ulong addr, int size);
static void fdctrl_raise_irq (uint8_t status);

static uint32_t fdctrl_read_statusB (CPUState *env, uint32_t reg);
static uint32_t fdctrl_read_dor (CPUState *env, uint32_t reg);
static void fdctrl_write_dor (CPUState *env, uint32_t reg, uint32_t value);
static uint32_t fdctrl_read_tape (CPUState *env, uint32_t reg);
static void fdctrl_write_tape (CPUState *env, uint32_t reg, uint32_t value);
static uint32_t fdctrl_read_main_status (CPUState *env, uint32_t reg);
static void fdctrl_write_rate (CPUState *env, uint32_t reg, uint32_t value);
static uint32_t fdctrl_read_data (CPUState *env, uint32_t reg);
static void fdctrl_write_data (CPUState *env, uint32_t reg, uint32_t value);
static uint32_t fdctrl_read_dir (CPUState *env, uint32_t reg);

enum {
    FD_CTRL_ACTIVE = 0x01,
    FD_CTRL_RESET  = 0x02,
    FD_CTRL_SLEEP  = 0x04,
    FD_CTRL_BUSY   = 0x08,
    FD_CTRL_INTR   = 0x10,
};

enum {
    FD_DIR_WRITE   = 0,
    FD_DIR_READ    = 1,
    FD_DIR_SCANE   = 2,
    FD_DIR_SCANL   = 3,
    FD_DIR_SCANH   = 4,
};

enum {
    FD_STATE_CMD    = 0x00,
    FD_STATE_STATUS = 0x01,
    FD_STATE_DATA   = 0x02,
    FD_STATE_STATE  = 0x03,
    FD_STATE_MULTI  = 0x10,
    FD_STATE_SEEK   = 0x20,
};

#define FD_STATE(state) ((state) & FD_STATE_STATE)
#define FD_MULTI_TRACK(state) ((state) & FD_STATE_MULTI)
#define FD_DID_SEEK(state) ((state) & FD_STATE_SEEK)

typedef struct fdctrl_t {
    /* Controler's identification */
    uint8_t version;
    /* HW */
    int irq_lvl;
    int dma_chann;
    /* Controler state */
    uint8_t state;
    uint8_t dma_en;
    uint8_t cur_drv;
    uint8_t bootsel;
    /* Command FIFO */
    uint8_t fifo[FD_SECTOR_LEN];
    uint32_t data_pos;
    uint32_t data_len;
    uint8_t data_state;
    uint8_t data_dir;
    uint8_t int_status;
    /* States kept only to be returned back */
    /* Timers state */
    uint8_t timer0;
    uint8_t timer1;
    /* precompensation */
    uint8_t precomp_trk;
    uint8_t config;
    uint8_t lock;
    /* Power down config (also with status regB access mode */
    uint8_t pwrd;
    /* Floppy drives */
    fdrive_t drives[2];
} fdctrl_t;

static fdctrl_t fdctrl;

void fdctrl_init (int irq_lvl, int dma_chann, int mem_mapped, uint32_t base,
                  char boot_device)
{
//    int io_mem;
    int i;

    FLOPPY_DPRINTF("init controler\n");
    memset(&fdctrl, 0, sizeof(fdctrl));
    fdctrl.version = 0x90; /* Intel 82078 controler */
    fdctrl.irq_lvl = irq_lvl;
    fdctrl.dma_chann = dma_chann;
    fdctrl.config = 0x40; /* Implicit seek, polling & FIFO enabled */
    if (fdctrl.dma_chann != -1) {
        fdctrl.dma_en = 1;
        DMA_register_channel(dma_chann, fdctrl_transfer_handler, &fdctrl);
    } else {
        fdctrl.dma_en = 0;
    }
    for (i = 0; i < MAX_FD; i++)
        fd_init(&fdctrl.drives[i]);
    fdctrl_reset(0);
    fdctrl.state = FD_CTRL_ACTIVE;
    if (mem_mapped) {
        FLOPPY_ERROR("memory mapped floppy not supported by now !\n");
#if 0
        io_mem = cpu_register_io_memory(0, fdctrl_mem_read, fdctrl_mem_write);
        cpu_register_physical_memory(base, 0x08, io_mem);
#endif
    } else {
        register_ioport_read(base + 0x01, 1, fdctrl_read_statusB, 1);
        register_ioport_read(base + 0x02, 1, fdctrl_read_dor, 1);
        register_ioport_write(base + 0x02, 1, fdctrl_write_dor, 1);
        register_ioport_read(base + 0x03, 1, fdctrl_read_tape, 1);
        register_ioport_write(base + 0x03, 1, fdctrl_write_tape, 1);
        register_ioport_read(base + 0x04, 1, fdctrl_read_main_status, 1);
        register_ioport_write(base + 0x04, 1, fdctrl_write_rate, 1);
        register_ioport_read(base + 0x05, 1, fdctrl_read_data, 1);
        register_ioport_write(base + 0x05, 1, fdctrl_write_data, 1);
        register_ioport_read(base + 0x07, 1, fdctrl_read_dir, 1);
    }
    if (boot_device == 'b')
        fdctrl.bootsel = 1;
    else
        fdctrl.bootsel = 0;
#if defined (TARGET_I386)
    cmos_register_fd(fdctrl.drives[0].drive, fdctrl.drives[1].drive);
#endif
}

int fdctrl_disk_change (int idx, const unsigned char *filename, int ro)
{
    fdrive_t *drv;
    
    if (idx < 0 || idx > 1)
        return -1;
    FLOPPY_DPRINTF("disk %d change: %s (%s)\n", idx, filename,
                   ro == 0 ? "rw" : "ro");
    drv = &fdctrl.drives[idx];
    if (fd_table[idx] != NULL) {
        bdrv_close(fd_table[idx]);
        fd_table[idx] = NULL;
    }
    fd_table[idx] = bdrv_open(filename, ro);
    drv->bs = fd_table[idx];
    if (fd_table[idx] == NULL)
        return -1;
    fd_revalidate(drv, ro);
#if 0
    fd_recalibrate(drv);
    fdctrl_reset_fifo();
    fdctrl_raise_irq(0x20);
#endif

    return 0;
}

/* Change IRQ state */
static void fdctrl_reset_irq (void)
{
    if (fdctrl.state & FD_CTRL_INTR) {
        pic_set_irq(fdctrl.irq_lvl, 0);
        fdctrl.state &= ~(FD_CTRL_INTR | FD_CTRL_SLEEP | FD_CTRL_BUSY);
    }
}

static void fdctrl_raise_irq (uint8_t status)
{
    if (~(fdctrl.state & FD_CTRL_INTR)) {
        pic_set_irq(fdctrl.irq_lvl, 1);
        fdctrl.state |= FD_CTRL_INTR;
    }
    FLOPPY_DPRINTF("Set interrupt status to 0x%02x\n", status);
    fdctrl.int_status = status;
}

/* Reset controler */
static void fdctrl_reset (int do_irq)
{
    int i;

    FLOPPY_DPRINTF("reset controler\n");
    fdctrl_reset_irq();
    /* Initialise controler */
    fdctrl.cur_drv = 0;
    /* FIFO state */
    fdctrl.data_pos = 0;
    fdctrl.data_len = 0;
    fdctrl.data_state = FD_STATE_CMD;
    fdctrl.data_dir = FD_DIR_WRITE;
    for (i = 0; i < MAX_FD; i++)
        fd_reset(&fdctrl.drives[i]);
    fdctrl_reset_fifo();
    if (do_irq)
        fdctrl_raise_irq(0x20);
}

/* Status B register : 0x01 (read-only) */
static uint32_t fdctrl_read_statusB (CPUState *env, uint32_t reg)
{
    fdctrl_reset_irq();
    FLOPPY_DPRINTF("status register: 0x00\n");

    return 0;
}

/* Digital output register : 0x02 */
static uint32_t fdctrl_read_dor (CPUState *env, uint32_t reg)
{
    fdrive_t *cur_drv, *drv0, *drv1;
    uint32_t retval = 0;

    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
    /* Drive motors state indicators */
    retval |= drv1->motor << 5;
    retval |= drv0->motor << 4;
    /* DMA enable */
    retval |= fdctrl.dma_en << 3;
    /* Reset indicator */
    retval |= (fdctrl.state & FD_CTRL_RESET) == 0 ? 0x04 : 0;
    /* Selected drive */
    retval |= fdctrl.cur_drv;
    FLOPPY_DPRINTF("digital output register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_write_dor (CPUState *env, uint32_t reg, uint32_t value)
{
    fdrive_t *drv0, *drv1;
    
    fdctrl_reset_irq();
    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    /* Reset mode */
    if (fdctrl.state & FD_CTRL_RESET) {
        if (!(value & 0x04)) {
            FLOPPY_DPRINTF("Floppy controler in RESET state !\n");
            return;
        }
    }
    FLOPPY_DPRINTF("digital output register set to 0x%02x\n", value);
    /* Drive motors state indicators */
    if (value & 0x20)
        fd_start(drv1);
    else
        fd_stop(drv1);
    if (value & 0x10)
        fd_start(drv0);
    else
        fd_stop(drv0);
    /* DMA enable */
#if 0
    if (fdctrl.dma_chann != -1)
        fdctrl.dma_en = 1 - ((value >> 3) & 1);
#endif
    /* Reset */
    if (!(value & 0x04)) {
        if (!(fdctrl.state & FD_CTRL_RESET)) {
            FLOPPY_DPRINTF("controler enter RESET state\n");
            fdctrl.state |= FD_CTRL_RESET;
            fdctrl_reset(1);
        }
    } else {
        if (fdctrl.state & FD_CTRL_RESET) {
            FLOPPY_DPRINTF("controler out of RESET state\n");
            fdctrl.state &= ~(FD_CTRL_RESET | FD_CTRL_SLEEP);
        }
    }
    /* Selected drive */
    fdctrl.cur_drv = value & 1;
}

/* Tape drive register : 0x03 */
static uint32_t fdctrl_read_tape (CPUState *env, uint32_t reg)
{
    uint32_t retval = 0;

    fdctrl_reset_irq();
    /* Disk boot selection indicator */
    retval |= fdctrl.bootsel << 2;
    /* Tape indicators: never allowed */
    FLOPPY_DPRINTF("tape drive register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_write_tape (CPUState *env, uint32_t reg, uint32_t value)
{
    fdctrl_reset_irq();
    /* Reset mode */
    if (fdctrl.state & FD_CTRL_RESET) {
        FLOPPY_DPRINTF("Floppy controler in RESET state !\n");
        return;
    }
    FLOPPY_DPRINTF("tape drive register set to 0x%02x\n", value);
    /* Disk boot selection indicator */
    fdctrl.bootsel = (value >> 2) & 1;
    /* Tape indicators: never allow */
}

/* Main status register : 0x04 (read) */
static uint32_t fdctrl_read_main_status (CPUState *env, uint32_t reg)
{
    uint32_t retval = 0;

    fdctrl_reset_irq();
    fdctrl.state &= ~(FD_CTRL_SLEEP | FD_CTRL_RESET);
    if (!(fdctrl.state & FD_CTRL_BUSY)) {
        /* Data transfer allowed */
        retval |= 0x80;
        /* Data transfer direction indicator */
        if (fdctrl.data_dir == FD_DIR_READ)
            retval |= 0x40;
    }
    /* Should handle 0x20 for SPECIFY command */
    /* Command busy indicator */
    if (FD_STATE(fdctrl.data_state) == FD_STATE_DATA ||
        FD_STATE(fdctrl.data_state) == FD_STATE_STATUS)
        retval |= 0x10;
    FLOPPY_DPRINTF("main status register: 0x%02x\n", retval);

    return retval;
}

/* Data select rate register : 0x04 (write) */
static void fdctrl_write_rate (CPUState *env, uint32_t reg, uint32_t value)
{
    fdctrl_reset_irq();
    /* Reset mode */
    if (fdctrl.state & FD_CTRL_RESET) {
        if (reg != 0x2 || !(value & 0x04)) {
            FLOPPY_DPRINTF("Floppy controler in RESET state !\n");
            return;
        }
    }
    FLOPPY_DPRINTF("select rate register set to 0x%02x\n", value);
    /* Reset: autoclear */
    if (value & 0x80) {
        fdctrl.state |= FD_CTRL_RESET;
        fdctrl_reset(1);
        fdctrl.state &= ~FD_CTRL_RESET;
    }
    if (value & 0x40) {
        fdctrl.state |= FD_CTRL_SLEEP;
        fdctrl_reset(1);
    }
//        fdctrl.precomp = (value >> 2) & 0x07;
}

/* Digital input register : 0x07 (read-only) */
static uint32_t fdctrl_read_dir (CPUState *env, uint32_t reg)
{
    fdrive_t *drv0, *drv1;
    uint32_t retval = 0;

    fdctrl_reset_irq();
    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    if (drv0->rv || drv1->rv)
        retval |= 0x80;
    if (retval != 0)
        FLOPPY_ERROR("Floppy digital input register: 0x%02x\n", retval);
    drv0->rv = 0;
    drv1->rv = 0;

    return retval;
}

/* FIFO state control */
static void fdctrl_reset_fifo (void)
{
    fdctrl.data_dir = FD_DIR_WRITE;
    fdctrl.data_pos = 0;
    fdctrl.data_state = FD_STATE_CMD;
}

/* Set FIFO status for the host to read */
static void fdctrl_set_fifo (int fifo_len, int do_irq)
{
    fdctrl.data_dir = FD_DIR_READ;
    fdctrl.data_len = fifo_len;
    fdctrl.data_pos = 0;
    fdctrl.data_state = FD_STATE_STATUS;
    if (do_irq)
        fdctrl_raise_irq(0x00);
}

/* Set an error: unimplemented/unknown command */
static void fdctrl_unimplemented (void)
{
#if 0
    fdrive_t *cur_drv, *drv0, *drv1;

    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
    fdctrl.fifo[0] = 0x60 | (cur_drv->head << 1) | fdctrl.cur_drv;
    fdctrl.fifo[1] = 0x00;
    fdctrl.fifo[2] = 0x00;
    fdctrl_set_fifo(3, 1);
#else
    fdctrl_reset_fifo();
#endif
}

/* Callback for transfer end (stop or abort) */
static void fdctrl_stop_transfer (uint8_t status0, uint8_t status1,
                                  uint8_t status2)
{
    fdrive_t *cur_drv, *drv0, *drv1;

    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
    FLOPPY_DPRINTF("transfer status: %02x %02x %02x (%02x)\n",
                   status0, status1, status2,
                   status0 | (cur_drv->head << 1) | fdctrl.cur_drv);
    fdctrl.fifo[0] = status0 | (cur_drv->head << 1) | fdctrl.cur_drv;
    fdctrl.fifo[1] = status1;
    fdctrl.fifo[2] = status2;
    fdctrl.fifo[3] = cur_drv->track;
    fdctrl.fifo[4] = cur_drv->head;
    fdctrl.fifo[5] = cur_drv->sect;
    fdctrl.fifo[6] = FD_SECTOR_SC;
    fdctrl.data_dir = FD_DIR_READ;
    if (fdctrl.state & FD_CTRL_BUSY)
        DMA_release_DREQ(fdctrl.dma_chann);
    fdctrl_set_fifo(7, 1);
}

/* Prepare a data transfer (either DMA or FIFO) */
static void fdctrl_start_transfer (int direction)
{
    fdrive_t *cur_drv, *drv0, *drv1;
    uint8_t kh, kt, ks;
    int did_seek;

    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    fdctrl.cur_drv = fdctrl.fifo[1] & 1;
    cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
    kt = fdctrl.fifo[2];
    kh = fdctrl.fifo[3];
    ks = fdctrl.fifo[4];
    FLOPPY_DPRINTF("Start tranfert at %d %d %02x %02x (%d)\n",
                   fdctrl.cur_drv, kh, kt, ks,
                   _fd_sector(kh, kt, ks, cur_drv->last_sect));
    did_seek = 0;
    switch (fd_seek(cur_drv, kh, kt, ks, fdctrl.config & 0x40)) {
    case 2:
        /* sect too big */
        fdctrl_stop_transfer(0x40, 0x00, 0x00);
        fdctrl.fifo[3] = kt;
        fdctrl.fifo[4] = kh;
        fdctrl.fifo[5] = ks;
        return;
    case 3:
        /* track too big */
        fdctrl_stop_transfer(0x40, 0x80, 0x00);
        fdctrl.fifo[3] = kt;
        fdctrl.fifo[4] = kh;
        fdctrl.fifo[5] = ks;
        return;
    case 4:
        /* No seek enabled */
        fdctrl_stop_transfer(0x40, 0x00, 0x00);
        fdctrl.fifo[3] = kt;
        fdctrl.fifo[4] = kh;
        fdctrl.fifo[5] = ks;
        return;
    case 1:
        did_seek = 1;
        break;
    default:
        break;
    }
    /* Set the FIFO state */
    fdctrl.data_dir = direction;
    fdctrl.data_pos = 0;
    fdctrl.data_state = FD_STATE_DATA; /* FIFO ready for data */
    if (fdctrl.fifo[0] & 0x80)
        fdctrl.data_state |= FD_STATE_MULTI;
    if (did_seek)
        fdctrl.data_state |= FD_STATE_SEEK;
    if (fdctrl.dma_en) {
        int dma_mode;
        /* DMA transfer are enabled. Check if DMA channel is well programmed */
        dma_mode = DMA_get_channel_mode(fdctrl.dma_chann);
        dma_mode = (dma_mode >> 2) & 3;
        FLOPPY_DPRINTF("dma_mode=%d direction=%d (%d)\n", dma_mode, direction,
                       (128 << fdctrl.fifo[5]) *
                       (cur_drv->last_sect - ks + 1));
        if (((direction == FD_DIR_SCANE || direction == FD_DIR_SCANL ||
              direction == FD_DIR_SCANH) && dma_mode == 0) ||
            (direction == FD_DIR_WRITE && dma_mode == 2) ||
            (direction == FD_DIR_READ && dma_mode == 1)) {
            /* No access is allowed until DMA transfer has completed */
            fdctrl.state |= FD_CTRL_BUSY;
            /* Now, we just have to wait for the DMA controler to
             * recall us...
             */
            DMA_hold_DREQ(fdctrl.dma_chann);
            DMA_schedule(fdctrl.dma_chann);
            return;
        }
    }
    FLOPPY_DPRINTF("start non-DMA transfer\n");
    /* IO based transfer: calculate len */
    if (fdctrl.fifo[5] == 00) {
        fdctrl.data_len = fdctrl.fifo[8];
    } else {
        fdctrl.data_len = 128 << fdctrl.fifo[5];
        fdctrl.data_len *= (cur_drv->last_sect - ks + 1);
        if (fdctrl.fifo[0] & 0x80)
            fdctrl.data_len *= 2;
    }
    fdctrl_raise_irq(0x00);

    return;
}

/* Prepare a transfer of deleted data */
static void fdctrl_start_transfer_del (int direction)
{
    /* We don't handle deleted data,
     * so we don't return *ANYTHING*
     */
    fdctrl_stop_transfer(0x60, 0x00, 0x00);
}

/* handlers for DMA transfers */
/* XXX: the partial transfer logic seems to be broken */
static int fdctrl_transfer_handler (void *opaque, target_ulong addr, int size)
{
    fdrive_t *cur_drv, *drv0, *drv1;
    int len;
    uint8_t status0 = 0x00, status1 = 0x00, status2 = 0x00;
    uint8_t tmpbuf[FD_SECTOR_LEN];

    fdctrl_reset_irq();
    if (!(fdctrl.state & FD_CTRL_BUSY)) {
        FLOPPY_DPRINTF("Not in DMA transfer mode !\n");
        return 0;
    }
    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
    if (fdctrl.data_dir == FD_DIR_SCANE || fdctrl.data_dir == FD_DIR_SCANL ||
        fdctrl.data_dir == FD_DIR_SCANH)
        status2 = 0x04;
    for (fdctrl.data_len = size; fdctrl.data_pos < fdctrl.data_len;
         fdctrl.data_pos += len) {
        len = size - fdctrl.data_pos;
        if (len > FD_SECTOR_LEN)
            len = FD_SECTOR_LEN;
        FLOPPY_DPRINTF("copy %d bytes (%d %d %d) %d pos %d %02x %02x "
                       "(%d-0x%08x)\n", len, size, fdctrl.data_pos,
                       fdctrl.data_len, fdctrl.cur_drv, cur_drv->head,
                       cur_drv->track, cur_drv->sect, fd_sector(cur_drv),
                       fd_sector(cur_drv) * 512);
        if (fdctrl.data_dir != FD_DIR_WRITE) {
            /* READ & SCAN commands */
            if (cur_drv->bs == NULL) {
                fdctrl_stop_transfer(0x40, 0x00, 0x00);
                goto transfer_error;
            }
            if (bdrv_read(cur_drv->bs, fd_sector(cur_drv), tmpbuf, 1) < 0) {
                FLOPPY_DPRINTF("Floppy: error getting sector %d\n",
                               fd_sector(cur_drv));
                /* Sure, image size is too small... */
                memset(tmpbuf, 0, FD_SECTOR_LEN);
            }
            if (fdctrl.data_dir == FD_DIR_READ) {
                cpu_physical_memory_write(addr + fdctrl.data_pos,
                                          tmpbuf, len);
                if (len < FD_SECTOR_LEN) {
                    memcpy(&fdctrl.fifo[0], tmpbuf + len, FD_SECTOR_LEN - len);
                    memset(&fdctrl.fifo[FD_SECTOR_LEN - len], 0, len);
                }
            } else {
                int ret;
                /* XXX: what to do if not enough data ? */
                cpu_physical_memory_read(addr + fdctrl.data_pos, 
                                         fdctrl.fifo, len); 
                if (len < FD_SECTOR_LEN) {
                    memset(&fdctrl.fifo[len], 0, FD_SECTOR_LEN - len);
                }
                ret = memcmp(tmpbuf, fdctrl.fifo, FD_SECTOR_LEN);
                if (ret == 0) {
                    status2 = 0x08;
                    goto end_transfer;
                }
                if ((ret < 0 && fdctrl.data_dir == FD_DIR_SCANL) ||
                    (ret > 0 && fdctrl.data_dir == FD_DIR_SCANH)) {
                    status2 = 0x00;
                    goto end_transfer;
                }
            }
        } else {
            /* WRITE commands */
            cpu_physical_memory_read(addr + fdctrl.data_pos, tmpbuf, len);
            if (len < FD_SECTOR_LEN) {
                memset(tmpbuf + len, 0, FD_SECTOR_LEN - len);
            }
            if (cur_drv->bs == NULL ||
                bdrv_write(cur_drv->bs, fd_sector(cur_drv), tmpbuf, 1) < 0) {
                FLOPPY_ERROR("writting sector %d\n", fd_sector(cur_drv));
                fdctrl_stop_transfer(0x60, 0x00, 0x00);
                goto transfer_error;
            }
        }
        if (len == FD_SECTOR_LEN) {
            /* Seek to next sector */
            if (cur_drv->sect == cur_drv->last_sect) {
                if (cur_drv->head == 0) {
                    cur_drv->head = 1;
                } else {
                    cur_drv->track++;
                    cur_drv->head = 0;
                }
                cur_drv->sect = 1;
                FLOPPY_DPRINTF("seek to next sector (%d %02x %02x => %d)\n",
                               cur_drv->head, cur_drv->track, cur_drv->sect,
                               fd_sector(cur_drv));
                if (cur_drv->head == 0) {
                    FLOPPY_DPRINTF("end transfer\n");
                    goto end_transfer;
                }
                if (!FD_MULTI_TRACK(fdctrl.data_state)) {
                    /* Single track read */
                    FLOPPY_DPRINTF("single track transfert: end transfer\n");
//                    status1 |= 0x80;
                    goto end_transfer;
                }
            } else {
                cur_drv->sect++;
                FLOPPY_DPRINTF("seek to next sector (%d %02x %02x => %d)\n",
                               cur_drv->head, cur_drv->track, cur_drv->sect,
                               fd_sector(cur_drv));
            }
        }
    }
end_transfer:
    if (fdctrl.data_dir == FD_DIR_SCANE ||
        fdctrl.data_dir == FD_DIR_SCANL ||
        fdctrl.data_dir == FD_DIR_SCANH)
        status2 = 0x08;
    if (FD_DID_SEEK(fdctrl.data_state))
        status0 |= 0x20;
    fdctrl_stop_transfer(status0, status1, status2);
transfer_error:

    return fdctrl.data_pos;
}

/* Data register : 0x05 */
static uint32_t fdctrl_read_data (CPUState *env, uint32_t reg)
{
    fdrive_t *cur_drv, *drv0, *drv1;
    uint32_t retval = 0;
    int pos, len;

    fdctrl_reset_irq();
    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
    fdctrl.state &= ~FD_CTRL_SLEEP;
    if (FD_STATE(fdctrl.data_state) == FD_STATE_CMD) {
        FLOPPY_ERROR("can't read data in CMD state\n");
        return 0;
    }
    pos = fdctrl.data_pos;
    if (FD_STATE(fdctrl.data_state) == FD_STATE_DATA) {
        pos %= FD_SECTOR_LEN;
        if (pos == 0) {
            len = fdctrl.data_len - fdctrl.data_pos;
            if (len > FD_SECTOR_LEN)
                len = FD_SECTOR_LEN;
            bdrv_read(cur_drv->bs, fd_sector(cur_drv),
                      fdctrl.fifo, len);
        }
    }
    retval = fdctrl.fifo[pos];
    if (++fdctrl.data_pos == fdctrl.data_len) {
        fdctrl.data_pos = 0;
        /* Switch from transfert mode to status mode
         * then from status mode to command mode
         */
        if (FD_STATE(fdctrl.data_state) == FD_STATE_DATA)
            fdctrl_stop_transfer(0x20, 0x00, 0x00);
        else
            fdctrl_reset_fifo();
    }
    FLOPPY_DPRINTF("data register: 0x%02x\n", retval);

    return retval;
}

static void fdctrl_write_data (CPUState *env, uint32_t reg, uint32_t value)
{
    fdrive_t *cur_drv, *drv0, *drv1;

    fdctrl_reset_irq();
    drv0 = &fdctrl.drives[fdctrl.bootsel];
    drv1 = &fdctrl.drives[1 - fdctrl.bootsel];
    cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
    /* Reset mode */
    if (fdctrl.state & FD_CTRL_RESET) {
        FLOPPY_DPRINTF("Floppy controler in RESET state !\n");
        return;
    }
    fdctrl.state &= ~FD_CTRL_SLEEP;
    if ((fdctrl.data_state & FD_STATE_STATE) == FD_STATE_STATUS) {
        FLOPPY_ERROR("can't write data in status mode\n");
        return;
    }
    /* Is it write command time ? */
    if (FD_STATE(fdctrl.data_state) == FD_STATE_DATA) {
        /* FIFO data write */
        fdctrl.fifo[fdctrl.data_pos++] = value;
        if (fdctrl.data_pos % FD_SECTOR_LEN == (FD_SECTOR_LEN - 1) ||
            fdctrl.data_pos == fdctrl.data_len) {
            bdrv_write(cur_drv->bs, fd_sector(cur_drv),
                       fdctrl.fifo, FD_SECTOR_LEN);
        }
        /* Switch from transfert mode to status mode
         * then from status mode to command mode
         */
        if (FD_STATE(fdctrl.data_state) == FD_STATE_DATA)
            fdctrl_stop_transfer(0x20, 0x00, 0x00);
        return;
    }
    if (fdctrl.data_pos == 0) {
        /* Command */
        switch (value & 0x5F) {
        case 0x46:
            /* READ variants */
            FLOPPY_DPRINTF("READ command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x4C:
            /* READ_DELETED variants */
            FLOPPY_DPRINTF("READ_DELETED command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x50:
            /* SCAN_EQUAL variants */
            FLOPPY_DPRINTF("SCAN_EQUAL command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x56:
            /* VERIFY variants */
            FLOPPY_DPRINTF("VERIFY command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x59:
            /* SCAN_LOW_OR_EQUAL variants */
            FLOPPY_DPRINTF("SCAN_LOW_OR_EQUAL command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x5D:
            /* SCAN_HIGH_OR_EQUAL variants */
            FLOPPY_DPRINTF("SCAN_HIGH_OR_EQUAL command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        default:
            break;
        }
        switch (value & 0x7F) {
        case 0x45:
            /* WRITE variants */
            FLOPPY_DPRINTF("WRITE command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x49:
            /* WRITE_DELETED variants */
            FLOPPY_DPRINTF("WRITE_DELETED command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        default:
            break;
        }
        switch (value) {
        case 0x03:
            /* SPECIFY */
            FLOPPY_DPRINTF("SPECIFY command\n");
            /* 1 parameter cmd */
            fdctrl.data_len = 3;
            goto enqueue;
        case 0x04:
            /* SENSE_DRIVE_STATUS */
            FLOPPY_DPRINTF("SENSE_DRIVE_STATUS command\n");
            /* 1 parameter cmd */
            fdctrl.data_len = 2;
            goto enqueue;
        case 0x07:
            /* RECALIBRATE */
            FLOPPY_DPRINTF("RECALIBRATE command\n");
            /* 1 parameter cmd */
            fdctrl.data_len = 2;
            goto enqueue;
        case 0x08:
            /* SENSE_INTERRUPT_STATUS */
            FLOPPY_DPRINTF("SENSE_INTERRUPT_STATUS command (%02x)\n",
                           fdctrl.int_status);
            /* No parameters cmd: returns status if no interrupt */
            fdctrl.fifo[0] =
                fdctrl.int_status | (cur_drv->head << 2) | fdctrl.cur_drv;
            fdctrl.fifo[1] = cur_drv->track;
            fdctrl_set_fifo(2, 0);
            return;
        case 0x0E:
            /* DUMPREG */
            FLOPPY_DPRINTF("DUMPREG command\n");
            /* Drives position */
            fdctrl.fifo[0] = drv0->track;
            fdctrl.fifo[1] = drv1->track;
            fdctrl.fifo[2] = 0;
            fdctrl.fifo[3] = 0;
            /* timers */
            fdctrl.fifo[4] = fdctrl.timer0;
            fdctrl.fifo[5] = (fdctrl.timer1 << 1) | fdctrl.dma_en;
            fdctrl.fifo[6] = cur_drv->last_sect;
            fdctrl.fifo[7] = (fdctrl.lock << 7) |
                    (cur_drv->perpendicular << 2);
            fdctrl.fifo[8] = fdctrl.config;
            fdctrl.fifo[9] = fdctrl.precomp_trk;
            fdctrl_set_fifo(10, 0);
            return;
        case 0x0F:
            /* SEEK */
            FLOPPY_DPRINTF("SEEK command\n");
            /* 2 parameters cmd */
            fdctrl.data_len = 3;
            goto enqueue;
        case 0x10:
            /* VERSION */
            FLOPPY_DPRINTF("VERSION command\n");
            /* No parameters cmd */
            /* Controler's version */
            fdctrl.fifo[0] = fdctrl.version;
            fdctrl_set_fifo(1, 1);
            return;
        case 0x12:
            /* PERPENDICULAR_MODE */
            FLOPPY_DPRINTF("PERPENDICULAR_MODE command\n");
            /* 1 parameter cmd */
            fdctrl.data_len = 2;
            goto enqueue;
        case 0x13:
            /* CONFIGURE */
            FLOPPY_DPRINTF("CONFIGURE command\n");
            /* 3 parameters cmd */
            fdctrl.data_len = 4;
            goto enqueue;
        case 0x14:
            /* UNLOCK */
            FLOPPY_DPRINTF("UNLOCK command\n");
            /* No parameters cmd */
            fdctrl.lock = 0;
            fdctrl.fifo[0] = 0;
            fdctrl_set_fifo(1, 0);
            return;
        case 0x17:
            /* POWERDOWN_MODE */
            FLOPPY_DPRINTF("POWERDOWN_MODE command\n");
            /* 2 parameters cmd */
            fdctrl.data_len = 3;
            goto enqueue;
        case 0x18:
            /* PART_ID */
            FLOPPY_DPRINTF("PART_ID command\n");
            /* No parameters cmd */
            fdctrl.fifo[0] = 0x41; /* Stepping 1 */
            fdctrl_set_fifo(1, 0);
            return;
        case 0x2C:
            /* SAVE */
            FLOPPY_DPRINTF("SAVE command\n");
            /* No parameters cmd */
            fdctrl.fifo[0] = 0;
            fdctrl.fifo[1] = 0;
            /* Drives position */
            fdctrl.fifo[2] = drv0->track;
            fdctrl.fifo[3] = drv1->track;
            fdctrl.fifo[4] = 0;
            fdctrl.fifo[5] = 0;
            /* timers */
            fdctrl.fifo[6] = fdctrl.timer0;
            fdctrl.fifo[7] = fdctrl.timer1;
            fdctrl.fifo[8] = cur_drv->last_sect;
            fdctrl.fifo[9] = (fdctrl.lock << 7) |
                    (cur_drv->perpendicular << 2);
            fdctrl.fifo[10] = fdctrl.config;
            fdctrl.fifo[11] = fdctrl.precomp_trk;
            fdctrl.fifo[12] = fdctrl.pwrd;
            fdctrl.fifo[13] = 0;
            fdctrl.fifo[14] = 0;
            fdctrl_set_fifo(15, 1);
            return;
        case 0x33:
            /* OPTION */
            FLOPPY_DPRINTF("OPTION command\n");
            /* 1 parameter cmd */
            fdctrl.data_len = 2;
            goto enqueue;
        case 0x42:
            /* READ_TRACK */
            FLOPPY_DPRINTF("READ_TRACK command\n");
            /* 8 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x4A:
            /* READ_ID */
            FLOPPY_DPRINTF("READ_ID command\n");
            /* 1 parameter cmd */
            fdctrl.data_len = 2;
            goto enqueue;
        case 0x4C:
            /* RESTORE */
            FLOPPY_DPRINTF("RESTORE command\n");
            /* 17 parameters cmd */
            fdctrl.data_len = 18;
            goto enqueue;
        case 0x4D:
            /* FORMAT_TRACK */
            FLOPPY_DPRINTF("FORMAT_TRACK command\n");
            /* 5 parameters cmd */
            fdctrl.data_len = 9;
            goto enqueue;
        case 0x8E:
            /* DRIVE_SPECIFICATION_COMMAND */
            FLOPPY_DPRINTF("DRIVE_SPECIFICATION_COMMAND command\n");
            /* 5 parameters cmd */
            fdctrl.data_len = 6;
            goto enqueue;
        case 0x8F:
            /* RELATIVE_SEEK_OUT */
            FLOPPY_DPRINTF("RELATIVE_SEEK_OUT command\n");
            /* 2 parameters cmd */
            fdctrl.data_len = 3;
            goto enqueue;
        case 0x94:
            /* LOCK */
            FLOPPY_DPRINTF("LOCK command\n");
            /* No parameters cmd */
            fdctrl.lock = 1;
            fdctrl.fifo[0] = 0x10;
            fdctrl_set_fifo(1, 1);
            return;
        case 0xCD:
            /* FORMAT_AND_WRITE */
            FLOPPY_DPRINTF("FORMAT_AND_WRITE command\n");
            /* 10 parameters cmd */
            fdctrl.data_len = 11;
            goto enqueue;
        case 0xCF:
            /* RELATIVE_SEEK_IN */
            FLOPPY_DPRINTF("RELATIVE_SEEK_IN command\n");
            /* 2 parameters cmd */
            fdctrl.data_len = 3;
            goto enqueue;
        default:
            /* Unknown command */
            FLOPPY_ERROR("unknown command: 0x%02x\n", value);
            fdctrl_unimplemented();
            return;
        }
    }
enqueue:
    fdctrl.fifo[fdctrl.data_pos] = value;
    if (++fdctrl.data_pos == fdctrl.data_len) {
        /* We now have all parameters
         * and will be able to treat the command
         */
        switch (fdctrl.fifo[0] & 0x1F) {
        case 0x06:
        {
            /* READ variants */
            FLOPPY_DPRINTF("treat READ command\n");
            fdctrl_start_transfer(FD_DIR_READ);
            return;
        }
        case 0x0C:
            /* READ_DELETED variants */
//            FLOPPY_DPRINTF("treat READ_DELETED command\n");
            FLOPPY_ERROR("treat READ_DELETED command\n");
            fdctrl_start_transfer_del(1);
            return;
        case 0x16:
            /* VERIFY variants */
//            FLOPPY_DPRINTF("treat VERIFY command\n");
            FLOPPY_ERROR("treat VERIFY command\n");
            fdctrl_stop_transfer(0x20, 0x00, 0x00);
            return;
        case 0x10:
            /* SCAN_EQUAL variants */
//            FLOPPY_DPRINTF("treat SCAN_EQUAL command\n");
            FLOPPY_ERROR("treat SCAN_EQUAL command\n");
            fdctrl_start_transfer(FD_DIR_SCANE);
            return;
        case 0x19:
            /* SCAN_LOW_OR_EQUAL variants */
//            FLOPPY_DPRINTF("treat SCAN_LOW_OR_EQUAL command\n");
            FLOPPY_ERROR("treat SCAN_LOW_OR_EQUAL command\n");
            fdctrl_start_transfer(FD_DIR_SCANL);
            return;
        case 0x1D:
            /* SCAN_HIGH_OR_EQUAL variants */
//            FLOPPY_DPRINTF("treat SCAN_HIGH_OR_EQUAL command\n");
            FLOPPY_ERROR("treat SCAN_HIGH_OR_EQUAL command\n");
            fdctrl_start_transfer(FD_DIR_SCANH);
            return;
        default:
            break;
        }
        switch (fdctrl.fifo[0] & 0x3F) {
        case 0x05:
            /* WRITE variants */
            FLOPPY_DPRINTF("treat WRITE command (%02x)\n", fdctrl.fifo[0]);
            fdctrl_start_transfer(FD_DIR_WRITE);
            return;
        case 0x09:
            /* WRITE_DELETED variants */
//            FLOPPY_DPRINTF("treat WRITE_DELETED command\n");
            FLOPPY_ERROR("treat WRITE_DELETED command\n");
            fdctrl_start_transfer_del(FD_DIR_WRITE);
            return;
        default:
            break;
        }
        switch (fdctrl.fifo[0]) {
        case 0x03:
            /* SPECIFY */
            FLOPPY_DPRINTF("treat SPECIFY command\n");
            fdctrl.timer0 = (fdctrl.fifo[1] >> 4) & 0xF;
            fdctrl.timer1 = fdctrl.fifo[1] >> 1;
            /* No result back */
            fdctrl_reset_fifo();
            break;
        case 0x04:
            /* SENSE_DRIVE_STATUS */
            FLOPPY_DPRINTF("treat SENSE_DRIVE_STATUS command\n");
            fdctrl.cur_drv = fdctrl.fifo[1] & 1;
            cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
            cur_drv->head = (fdctrl.fifo[1] >> 2) & 1;
            /* 1 Byte status back */
            fdctrl.fifo[0] = (cur_drv->ro << 6) |
                (cur_drv->track == 0 ? 0x10 : 0x00) |
                fdctrl.cur_drv;
            fdctrl_set_fifo(1, 0);
            break;
        case 0x07:
            /* RECALIBRATE */
            FLOPPY_DPRINTF("treat RECALIBRATE command\n");
            fdctrl.cur_drv = fdctrl.fifo[1] & 1;
            cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
            fd_recalibrate(cur_drv);
            fdctrl_reset_fifo();
            /* Raise Interrupt */
            fdctrl_raise_irq(0x20);
            break;
        case 0x0F:
            /* SEEK */
            FLOPPY_DPRINTF("treat SEEK command\n");
            fdctrl.cur_drv = fdctrl.fifo[1] & 1;
            cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
            if (fdctrl.fifo[2] <= cur_drv->track)
                cur_drv->dir = 1;
            else
                cur_drv->dir = 0;
            cur_drv->head = (fdctrl.fifo[1] >> 2) & 1;
            if (fdctrl.fifo[2] > cur_drv->max_track) {
                fdctrl_raise_irq(0x60);
            } else {
                cur_drv->track = fdctrl.fifo[2];
                fdctrl_reset_fifo();
                /* Raise Interrupt */
                fdctrl_raise_irq(0x20);
            }
            break;
        case 0x12:
            /* PERPENDICULAR_MODE */
            FLOPPY_DPRINTF("treat PERPENDICULAR_MODE command\n");
            if (fdctrl.fifo[1] & 0x80)
                cur_drv->perpendicular = fdctrl.fifo[1] & 0x7;
            /* No result back */
            fdctrl_reset_fifo();
            break;
        case 0x13:
            /* CONFIGURE */
            FLOPPY_DPRINTF("treat CONFIGURE command\n");
            fdctrl.config = fdctrl.fifo[2];
            fdctrl.precomp_trk =  fdctrl.fifo[3];
            /* No result back */
            fdctrl_reset_fifo();
            break;
        case 0x17:
            /* POWERDOWN_MODE */
            FLOPPY_DPRINTF("treat POWERDOWN_MODE command\n");
            fdctrl.pwrd = fdctrl.fifo[1];
            fdctrl.fifo[0] = fdctrl.fifo[1];
            fdctrl_set_fifo(1, 1);
            break;
        case 0x33:
            /* OPTION */
            FLOPPY_DPRINTF("treat OPTION command\n");
            /* No result back */
            fdctrl_reset_fifo();
            break;
        case 0x42:
            /* READ_TRACK */
//            FLOPPY_DPRINTF("treat READ_TRACK command\n");
            FLOPPY_ERROR("treat READ_TRACK command\n");
            fdctrl_unimplemented();
            break;
        case 0x4A:
                /* READ_ID */
//            FLOPPY_DPRINTF("treat READ_ID command\n");
            FLOPPY_ERROR("treat READ_ID command\n");
            fdctrl_stop_transfer(0x00, 0x00, 0x00);
            break;
        case 0x4C:
            /* RESTORE */
            FLOPPY_DPRINTF("treat RESTORE command\n");
            /* Drives position */
            drv0->track = fdctrl.fifo[3];
            drv1->track = fdctrl.fifo[4];
            /* timers */
            fdctrl.timer0 = fdctrl.fifo[7];
            fdctrl.timer1 = fdctrl.fifo[8];
            cur_drv->last_sect = fdctrl.fifo[9];
            fdctrl.lock = fdctrl.fifo[10] >> 7;
            cur_drv->perpendicular = (fdctrl.fifo[10] >> 2) & 0xF;
            fdctrl.config = fdctrl.fifo[11];
            fdctrl.precomp_trk = fdctrl.fifo[12];
            fdctrl.pwrd = fdctrl.fifo[13];
            fdctrl_reset_fifo();
            break;
        case 0x4D:
            /* FORMAT_TRACK */
//                FLOPPY_DPRINTF("treat FORMAT_TRACK command\n");
            FLOPPY_ERROR("treat FORMAT_TRACK command\n");
            fdctrl_unimplemented();
            break;
        case 0x8E:
            /* DRIVE_SPECIFICATION_COMMAND */
            FLOPPY_DPRINTF("treat DRIVE_SPECIFICATION_COMMAND command\n");
            if (fdctrl.fifo[fdctrl.data_pos - 1] & 0x80) {
                /* Command parameters done */
                if (fdctrl.fifo[fdctrl.data_pos - 1] & 0x40) {
                    fdctrl.fifo[0] = fdctrl.fifo[1];
                    fdctrl.fifo[2] = 0;
                    fdctrl.fifo[3] = 0;
                    fdctrl_set_fifo(4, 1);
                } else {
                    fdctrl_reset_fifo();
                }
            } else if (fdctrl.data_len > 7) {
                /* ERROR */
                fdctrl.fifo[0] = 0x80 |
                    (cur_drv->head << 2) | fdctrl.cur_drv;
                fdctrl_set_fifo(1, 1);
            }
            break;
        case 0x8F:
            /* RELATIVE_SEEK_OUT */
            FLOPPY_DPRINTF("treat RELATIVE_SEEK_OUT command\n");
            fdctrl.cur_drv = fdctrl.fifo[1] & 1;
            cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
            cur_drv->head = (fdctrl.fifo[1] >> 2) & 1;
            if (fdctrl.fifo[2] + cur_drv->track > cur_drv->max_track) {
                /* ERROR */
                fdctrl_raise_irq(0x70);
            } else {
                cur_drv->track += fdctrl.fifo[2];
                cur_drv->dir = 0;
                fdctrl_reset_fifo();
                fdctrl_raise_irq(0x20);
            }
            break;
        case 0xCD:
            /* FORMAT_AND_WRITE */
//                FLOPPY_DPRINTF("treat FORMAT_AND_WRITE command\n");
            FLOPPY_ERROR("treat FORMAT_AND_WRITE command\n");
            fdctrl_unimplemented();
            break;
        case 0xCF:
                /* RELATIVE_SEEK_IN */
            FLOPPY_DPRINTF("treat RELATIVE_SEEK_IN command\n");
            fdctrl.cur_drv = fdctrl.fifo[1] & 1;
            cur_drv = fdctrl.cur_drv == 0 ? drv0 : drv1;
            cur_drv->head = (fdctrl.fifo[1] >> 2) & 1;
            if (fdctrl.fifo[2] > cur_drv->track) {
                /* ERROR */
                fdctrl_raise_irq(0x60);
            } else {
                fdctrl_reset_fifo();
                cur_drv->track -= fdctrl.fifo[2];
                cur_drv->dir = 1;
                /* Raise Interrupt */
                fdctrl_raise_irq(0x20);
            }
            break;
        }
    }
}
