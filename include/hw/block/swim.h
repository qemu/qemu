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

#include "hw/block/block.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define SWIM_MAX_FD            2

typedef struct SWIMCtrl SWIMCtrl;

#define TYPE_SWIM_DRIVE "swim-drive"
OBJECT_DECLARE_SIMPLE_TYPE(SWIMDrive, SWIM_DRIVE)

struct SWIMDrive {
    DeviceState qdev;
    int32_t     unit;
    BlockConf   conf;
};

#define TYPE_SWIM_BUS "swim-bus"
OBJECT_DECLARE_SIMPLE_TYPE(SWIMBus, SWIM_BUS)

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
    MemoryRegion swim;
    MemoryRegion iwm;
    MemoryRegion ism;
    FDrive drives[SWIM_MAX_FD];
    int mode;
    /* IWM mode */
    int iwm_switch;
    uint8_t iwm_latches;
    uint8_t iwmregs[8];
    /* SWIM mode */
    uint8_t ismregs[16];
    uint8_t swim_phase;
    uint8_t swim_mode;
    uint8_t swim_status;
    uint8_t pram[16];
    uint8_t pram_idx;
    SWIMBus bus;
};

#define TYPE_SWIM "swim"
OBJECT_DECLARE_SIMPLE_TYPE(Swim, SWIM)

struct Swim {
    SysBusDevice parent_obj;
    SWIMCtrl     ctrl;
};
#endif
