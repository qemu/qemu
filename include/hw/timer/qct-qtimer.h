/*
 * Qualcomm QCT QTimer
 *
 * Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TIMER_QCT_QTIMER_H
#define TIMER_QCT_QTIMER_H

#include "hw/ptimer.h"
#include "hw/sysbus.h"

#define TYPE_QCT_QTIMER "qct-qtimer"
#define TYPE_QCT_HEXTIMER "qct-hextimer"
OBJECT_DECLARE_SIMPLE_TYPE(QCTQtimerState, QCT_QTIMER)
OBJECT_DECLARE_SIMPLE_TYPE(QCTHextimerState, QCT_HEXTIMER)

struct QCTHextimerState {
    QCTQtimerState *qtimer;
    ptimer_state *timer;
    uint64_t cntval; /*
                      * Physical timer compare value interrupt when cntpct >
                      * cntval
                      */
    uint64_t cntpct; /* Physical counter */
    uint32_t control;
    uint32_t cnt_ctrl;
    uint32_t cntpl0acr;
    uint64_t limit;
    uint32_t freq;
    uint32_t int_level;
    qemu_irq irq;
};

#define QCT_QTIMER_TIMER_FRAME_ELTS (8)
#define QCT_QTIMER_TIMER_VIEW_ELTS (2)
struct QCTQtimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion view_iomem;
    uint32_t secure;
    struct QCTHextimerState timer[QCT_QTIMER_TIMER_FRAME_ELTS];
    uint32_t frame_id;
    uint32_t freq;
    uint32_t nr_frames;
    uint32_t nr_views;
    uint32_t cnttid;
};

#define QCT_QTIMER_AC_CNTFRQ (0x000)
#define QCT_QTIMER_AC_CNTSR (0x004)
#define QCT_QTIMER_AC_CNTSR_NSN_1 (1 << 0)
#define QCT_QTIMER_AC_CNTSR_NSN_2 (1 << 1)
#define QCT_QTIMER_AC_CNTSR_NSN_3 (1 << 2)
#define QCT_QTIMER_AC_CNTTID (0x08)
#define QCT_QTIMER_AC_CNTACR_0 (0x40)
#define QCT_QTIMER_AC_CNTACR_1 (0x44)
#define QCT_QTIMER_AC_CNTACR_2 (0x48)
#define QCT_QTIMER_AC_CNTACR_RWPT (1 << 5) /* R/W of CNTP_* regs */
#define QCT_QTIMER_AC_CNTACR_RWVT (1 << 4) /* R/W of CNTV_* regs */
#define QCT_QTIMER_AC_CNTACR_RVOFF (1 << 3) /* R/W of CNTVOFF register */
#define QCT_QTIMER_AC_CNTACR_RFRQ (1 << 2) /* R/W of CNTFRQ register */
#define QCT_QTIMER_AC_CNTACR_RPVCT (1 << 1) /* R/W of CNTVCT register */
#define QCT_QTIMER_AC_CNTACR_RPCT (1 << 0) /* R/W of CNTPCT register */
#define QCT_QTIMER_VERSION (0x0fd0)
#define QCT_QTIMER_CNTPCT_LO (0x000)
#define QCT_QTIMER_CNTPCT_HI (0x004)
#define QCT_QTIMER_CNT_FREQ (0x010)
#define QCT_QTIMER_CNTPL0ACR (0x014)
#define QCT_QTIMER_CNTPL0ACR_PL0CTEN (1 << 9)
#define QCT_QTIMER_CNTPL0ACR_PL0TVEN (1 << 8)
#define QCT_QTIMER_CNTPL0ACR_PL0VCTEN (1 << 1)
#define QCT_QTIMER_CNTPL0ACR_PL0PCTEN (1 << 0)
#define QCT_QTIMER_CNTP_CVAL_LO (0x020)
#define QCT_QTIMER_CNTP_CVAL_HI (0x024)
#define QCT_QTIMER_CNTP_TVAL (0x028)
#define QCT_QTIMER_CNTP_CTL (0x02c)
#define QCT_QTIMER_CNTP_CTL_ISTAT (1 << 2)
#define QCT_QTIMER_CNTP_CTL_INTEN (1 << 1)
#define QCT_QTIMER_CNTP_CTL_ENABLE (1 << 0)
#define QCT_QTIMER_AC_CNTACR_START 0x40
#define QCT_QTIMER_AC_CNTACR_END 0x5C

#endif /* TIMER_QCT_QTIMER_H */
