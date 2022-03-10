/*
 *
 * Copyright (c) 2011-2018 Laurent Vivier
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_MISC_MAC_VIA_H
#define HW_MISC_MAC_VIA_H

#include "exec/memory.h"
#include "hw/sysbus.h"
#include "hw/misc/mos6522.h"
#include "qom/object.h"


#define VIA_SIZE   0x2000

/* VIA 1 */
#define VIA1_IRQ_ONE_SECOND_BIT CA2_INT_BIT
#define VIA1_IRQ_60HZ_BIT       CA1_INT_BIT
#define VIA1_IRQ_ADB_READY_BIT  SR_INT_BIT
#define VIA1_IRQ_ADB_DATA_BIT   CB2_INT_BIT
#define VIA1_IRQ_ADB_CLOCK_BIT  CB1_INT_BIT

#define VIA1_IRQ_ONE_SECOND     BIT(VIA1_IRQ_ONE_SECOND_BIT)
#define VIA1_IRQ_60HZ           BIT(VIA1_IRQ_60HZ_BIT)
#define VIA1_IRQ_ADB_READY      BIT(VIA1_IRQ_ADB_READY_BIT)
#define VIA1_IRQ_ADB_DATA       BIT(VIA1_IRQ_ADB_DATA_BIT)
#define VIA1_IRQ_ADB_CLOCK      BIT(VIA1_IRQ_ADB_CLOCK_BIT)


#define TYPE_MOS6522_Q800_VIA1 "mos6522-q800-via1"
OBJECT_DECLARE_SIMPLE_TYPE(MOS6522Q800VIA1State, MOS6522_Q800_VIA1)

struct MOS6522Q800VIA1State {
    /*< private >*/
    MOS6522State parent_obj;

    MemoryRegion via_mem;

    qemu_irq auxmode_irq;
    uint8_t last_b;

    /* RTC */
    uint8_t PRAM[256];
    BlockBackend *blk;
    VMChangeStateEntry *vmstate;

    uint32_t tick_offset;

    uint8_t data_out;
    int data_out_cnt;
    uint8_t data_in;
    uint8_t data_in_cnt;
    uint8_t cmd;
    int wprotect;
    int alt;

    /* ADB */
    ADBBusState adb_bus;
    qemu_irq adb_data_ready;
    int adb_data_in_size;
    int adb_data_in_index;
    int adb_data_out_index;
    uint8_t adb_data_in[128];
    uint8_t adb_data_out[16];
    uint8_t adb_autopoll_cmd;

    /* external timers */
    QEMUTimer *one_second_timer;
    int64_t next_second;
    QEMUTimer *sixty_hz_timer;
    int64_t next_sixty_hz;
};


/* VIA 2 */
#define VIA2_IRQ_SCSI_DATA_BIT  CA2_INT_BIT
#define VIA2_IRQ_NUBUS_BIT      CA1_INT_BIT
#define VIA2_IRQ_SCSI_BIT       CB2_INT_BIT
#define VIA2_IRQ_ASC_BIT        CB1_INT_BIT

#define VIA2_IRQ_SCSI_DATA      BIT(VIA2_IRQ_SCSI_DATA_BIT)
#define VIA2_IRQ_NUBUS          BIT(VIA2_IRQ_NUBUS_BIT)
#define VIA2_IRQ_UNUSED         BIT(VIA2_IRQ_SCSI_BIT)
#define VIA2_IRQ_SCSI           BIT(VIA2_IRQ_UNUSED_BIT)
#define VIA2_IRQ_ASC            BIT(VIA2_IRQ_ASC_BIT)

#define VIA2_NUBUS_IRQ_NB       7

#define VIA2_NUBUS_IRQ_9        0
#define VIA2_NUBUS_IRQ_A        1
#define VIA2_NUBUS_IRQ_B        2
#define VIA2_NUBUS_IRQ_C        3
#define VIA2_NUBUS_IRQ_D        4
#define VIA2_NUBUS_IRQ_E        5
#define VIA2_NUBUS_IRQ_INTVIDEO 6

#define TYPE_MOS6522_Q800_VIA2 "mos6522-q800-via2"
OBJECT_DECLARE_SIMPLE_TYPE(MOS6522Q800VIA2State, MOS6522_Q800_VIA2)

struct MOS6522Q800VIA2State {
    /*< private >*/
    MOS6522State parent_obj;

    MemoryRegion via_mem;
};

#endif
