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


/* VIA 1 */
#define VIA1_IRQ_ONE_SECOND_BIT 0
#define VIA1_IRQ_VBLANK_BIT     1
#define VIA1_IRQ_ADB_READY_BIT  2
#define VIA1_IRQ_ADB_DATA_BIT   3
#define VIA1_IRQ_ADB_CLOCK_BIT  4

#define VIA1_IRQ_NB             8

#define VIA1_IRQ_ONE_SECOND (1 << VIA1_IRQ_ONE_SECOND_BIT)
#define VIA1_IRQ_VBLANK     (1 << VIA1_IRQ_VBLANK_BIT)
#define VIA1_IRQ_ADB_READY  (1 << VIA1_IRQ_ADB_READY_BIT)
#define VIA1_IRQ_ADB_DATA   (1 << VIA1_IRQ_ADB_DATA_BIT)
#define VIA1_IRQ_ADB_CLOCK  (1 << VIA1_IRQ_ADB_CLOCK_BIT)


#define TYPE_MOS6522_Q800_VIA1 "mos6522-q800-via1"
OBJECT_DECLARE_SIMPLE_TYPE(MOS6522Q800VIA1State, MOS6522_Q800_VIA1)

struct MOS6522Q800VIA1State {
    /*< private >*/
    MOS6522State parent_obj;

    qemu_irq irqs[VIA1_IRQ_NB];
    uint8_t last_b;
    uint8_t PRAM[256];

    /* external timers */
    QEMUTimer *one_second_timer;
    int64_t next_second;
    QEMUTimer *VBL_timer;
    int64_t next_VBL;
};


/* VIA 2 */
#define VIA2_IRQ_SCSI_DATA_BIT  0
#define VIA2_IRQ_SLOT_BIT       1
#define VIA2_IRQ_UNUSED_BIT     2
#define VIA2_IRQ_SCSI_BIT       3
#define VIA2_IRQ_ASC_BIT        4

#define VIA2_IRQ_NB             8

#define VIA2_IRQ_SCSI_DATA  (1 << VIA2_IRQ_SCSI_DATA_BIT)
#define VIA2_IRQ_SLOT       (1 << VIA2_IRQ_SLOT_BIT)
#define VIA2_IRQ_UNUSED     (1 << VIA2_IRQ_SCSI_BIT)
#define VIA2_IRQ_SCSI       (1 << VIA2_IRQ_UNUSED_BIT)
#define VIA2_IRQ_ASC        (1 << VIA2_IRQ_ASC_BIT)

#define TYPE_MOS6522_Q800_VIA2 "mos6522-q800-via2"
OBJECT_DECLARE_SIMPLE_TYPE(MOS6522Q800VIA2State, MOS6522_Q800_VIA2)

struct MOS6522Q800VIA2State {
    /*< private >*/
    MOS6522State parent_obj;
};


#define TYPE_MAC_VIA "mac_via"
OBJECT_DECLARE_SIMPLE_TYPE(MacVIAState, MAC_VIA)

struct MacVIAState {
    SysBusDevice busdev;

    VMChangeStateEntry *vmstate;

    /* MMIO */
    MemoryRegion mmio;
    MemoryRegion via1mem;
    MemoryRegion via2mem;

    /* VIAs */
    MOS6522Q800VIA1State mos6522_via1;
    MOS6522Q800VIA2State mos6522_via2;

    /* RTC */
    uint32_t tick_offset;

    uint8_t data_out;
    int data_out_cnt;
    uint8_t data_in;
    uint8_t data_in_cnt;
    uint8_t cmd;
    int wprotect;
    int alt;
    BlockBackend *blk;

    /* ADB */
    ADBBusState adb_bus;
    qemu_irq adb_data_ready;
    int adb_data_in_size;
    int adb_data_in_index;
    int adb_data_out_index;
    uint8_t adb_data_in[128];
    uint8_t adb_data_out[16];
    uint8_t adb_autopoll_cmd;
};

#endif
