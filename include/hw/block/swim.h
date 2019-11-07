/*
 * QEMU Macintosh floppy disk controller emulator (SWIM)
 *
 * Copyright (c) 2014-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef SWIM_H
#define SWIM_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#define SWIM_MAX_FD            2

typedef struct SWIMDrive SWIMDrive;
typedef struct SWIMBus SWIMBus;
typedef struct SWIMCtrl SWIMCtrl;

#define TYPE_SWIM_DRIVE "swim-drive"
#define SWIM_DRIVE(obj) OBJECT_CHECK(SWIMDrive, (obj), TYPE_SWIM_DRIVE)

struct SWIMDrive {
    DeviceState qdev;
    int32_t     unit;
    BlockConf   conf;
};

#define TYPE_SWIM_BUS "swim-bus"
#define SWIM_BUS(obj) OBJECT_CHECK(SWIMBus, (obj), TYPE_SWIM_BUS)

struct SWIMBus {
    BusState bus;
    struct SWIMCtrl *ctrl;
};

typedef struct FDrive {
    SWIMCtrl *swimctrl;
    BlockBackend *blk;
    BlockConf *conf;
} FDrive;

struct SWIMCtrl {
    MemoryRegion iomem;
    FDrive drives[SWIM_MAX_FD];
    int mode;
    /* IWM mode */
    int iwm_switch;
    uint16_t regs[8];
#define IWM_PH0   0
#define IWM_PH1   1
#define IWM_PH2   2
#define IWM_PH3   3
#define IWM_MTR   4
#define IWM_DRIVE 5
#define IWM_Q6    6
#define IWM_Q7    7
    uint8_t iwm_data;
    uint8_t iwm_mode;
    /* SWIM mode */
    uint8_t swim_phase;
    uint8_t swim_mode;
    SWIMBus bus;
};

#define TYPE_SWIM "swim"
#define SWIM(obj) OBJECT_CHECK(SWIM, (obj), TYPE_SWIM)

typedef struct SWIM {
    SysBusDevice parent_obj;
    SWIMCtrl     ctrl;
} SWIM;
#endif
