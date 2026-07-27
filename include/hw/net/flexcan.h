/*
 * QEMU model of the NXP FLEXCAN device.
 *
 * Copyright (c) 2025 Matyas Bobek <matyas.bobek@gmail.com>
 *
 * Based on CTU CAN FD emulation implemented by Jan Charvat.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CAN_FLEXCAN_H
#define HW_CAN_FLEXCAN_H

#include "net/can_emu.h"
#include "qom/object.h"
#include "hw/misc/imx_ccm.h"

#define FLEXCAN_FIFO_DEPTH 6
#define FLEXCAN_MAILBOX_COUNT 64

/**
 * Definitions of structs FlexcanRegs and FlexcanRegsMessageBuffer were
 * originally written for the Linux kernel by:
 *   Andrey Volkov <andrey@volkov.fr>
 *   Sascha Hauer <s.hauer@pengutronix.de>
 *   Marc Kleine-Budde <mkl@pengutronix.de>
 *   David Jander <david@protonic.nl>
 * and they have agreed to license them under GPL-2.0-or-later.
 */

/* view of single message buffer registers */
typedef struct FlexcanRegsMessageBuffer {
    uint32_t can_ctrl;
    uint32_t can_id;
    uint32_t data[2];
} FlexcanRegsMessageBuffer;

/* FlexCAN register in hw layout */
typedef struct FlexcanRegs {
    uint32_t mcr;                /* 0x00 */
    uint32_t ctrl;               /* 0x04 - not affected by soft reset */
    uint32_t timer;              /* 0x08 */
    uint32_t tcr;                /* 0x0C */
    uint32_t rxmgmask;           /* 0x10 - not affected by soft reset */
    uint32_t rx14mask;           /* 0x14 - not affected by soft reset */
    uint32_t rx15mask;           /* 0x18 - not affected by soft reset */
    uint32_t ecr;                /* 0x1C */
    uint32_t esr;                /* 0x20 */
    uint32_t imask2;             /* 0x24 */
    uint32_t imask1;             /* 0x28 */
    uint32_t iflag2;             /* 0x2C */
    uint32_t iflag1;             /* 0x30 */
    union {                      /* 0x34 */
        uint32_t gfwr_mx28;      /* MX28, MX53 */
        uint32_t ctrl2;          /* MX6, VF610 - not affected by soft reset */
    };
    uint32_t esr2;               /* 0x38 */
    uint32_t imeur;              /* 0x3C, unused */
    uint32_t lrfr;               /* 0x40, unused */
    uint32_t crcr;               /* 0x44 */
    uint32_t rxfgmask;           /* 0x48 */
    uint32_t rxfir;              /* 0x4C - not affected by soft reset */
    uint32_t cbt;                /* 0x50, unused - not affected by soft reset */
    uint32_t _reserved2;         /* 0x54 */
    uint32_t dbg1;               /* 0x58, unused */
    uint32_t dbg2;               /* 0x5C, unused */
    uint32_t _reserved3[8];      /* 0x60 */
    union {                      /* 0x80 - not affected by soft reset */
        uint32_t mb[sizeof(FlexcanRegsMessageBuffer) * FLEXCAN_MAILBOX_COUNT];
        FlexcanRegsMessageBuffer mbs[FLEXCAN_MAILBOX_COUNT];
    };
    uint32_t _reserved4[256];    /* 0x480 */
    uint32_t rximr[64];          /* 0x880 - not affected by soft reset */
    uint32_t _reserved5[24];     /* 0x980 */
    uint32_t gfwr_mx6;           /* 0x9E0 - MX6 */

    /* the rest is unused except for SMB */
    uint32_t _reserved6[39];     /* 0x9E4 */
    uint32_t _rxfir[6];          /* 0xA80 */
    uint32_t _reserved8[2];      /* 0xA98 */
    uint32_t _rxmgmask;          /* 0xAA0 */
    uint32_t _rxfgmask;          /* 0xAA4 */
    uint32_t _rx14mask;          /* 0xAA8 */
    uint32_t _rx15mask;          /* 0xAAC */
    uint32_t tx_smb[4];          /* 0xAB0 */
    union {                      /* 0xAC0, used for SMB emulation */
        uint32_t rx_smb0_raw[4];
        FlexcanRegsMessageBuffer rx_smb0;
    };
    uint32_t rx_smb1[4];         /* 0xAD0 */
    uint32_t mecr;               /* 0xAE0 */
    uint32_t erriar;             /* 0xAE4 */
    uint32_t erridpr;            /* 0xAE8 */
    uint32_t errippr;            /* 0xAEC */
    uint32_t rerrar;             /* 0xAF0 */
    uint32_t rerrdr;             /* 0xAF4 */
    uint32_t rerrsynr;           /* 0xAF8 */
    uint32_t errsr;              /* 0xAFC */
    uint32_t _reserved7[64];     /* 0xB00 */
    uint32_t fdctrl;             /* 0xC00 - not affected by soft reset */
    uint32_t fdcbt;              /* 0xC04 - not affected by soft reset */
    uint32_t fdcrc;              /* 0xC08 */
    uint32_t _reserved9[199];    /* 0xC0C */
    uint32_t tx_smb_fd[18];      /* 0xF28 */
    uint32_t rx_smb0_fd[18];     /* 0xF70 */
    uint32_t rx_smb1_fd[18];     /* 0xFB8 */
} FlexcanRegs;

typedef struct FlexcanState {
    SysBusDevice        parent_obj;

    MemoryRegion        iomem;
    IMXCCMState        *ccm;
    qemu_irq            irq;

    CanBusState        *canbus;
    CanBusClientState   bus_client;

    union {
        FlexcanRegs     regs;
        uint32_t        regs_raw[sizeof(FlexcanRegs) / 4];
    };
    int64_t             timer_start;
    uint64_t            last_rx_timer_cycles;
    int32_t             locked_mbidx;
    int32_t             smb_target_mbidx;
    uint32_t            timer_freq;
} FlexcanState;

#define TYPE_CAN_FLEXCAN "flexcan"
OBJECT_DECLARE_SIMPLE_TYPE(FlexcanState, CAN_FLEXCAN);

#define TYPE_CAN_FLEXCAN2 "flexcan2"
#define TYPE_CAN_FLEXCAN3 "flexcan3"

#endif
