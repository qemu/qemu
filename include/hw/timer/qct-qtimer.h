/*
 * Qualcomm QCT QTimer
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_QCT_QTIMER_H
#define HW_TIMER_QCT_QTIMER_H

#include "qom/object.h"

#define TYPE_QCT_QTIMER "qct-qtimer"

/* QTimer interface for external access from hexagon_globalreg */
#define TYPE_QCT_QTIMER_INTERFACE "qct-qtimer-if"

typedef struct QctQtimerInterface QctQtimerInterface;

typedef struct QctQtimerInterfaceClass {
    InterfaceClass parent_class;

    /* Read the live physical counter, backing HEX_SREG_TIMERLO/TIMERHI */
    uint32_t (*get_timer_lo)(const QctQtimerInterface *qtimer);
    uint32_t (*get_timer_hi)(const QctQtimerInterface *qtimer);
} QctQtimerInterfaceClass;

DECLARE_OBJ_CHECKERS(QctQtimerInterface, QctQtimerInterfaceClass,
                     QCT_QTIMER_INTERFACE, TYPE_QCT_QTIMER_INTERFACE);

static inline uint32_t qct_qtimer_get_timer_lo(const QctQtimerInterface *qtimer)
{
    QctQtimerInterfaceClass *k = QCT_QTIMER_INTERFACE_GET_CLASS(qtimer);
    return k->get_timer_lo(qtimer);
}

static inline uint32_t qct_qtimer_get_timer_hi(const QctQtimerInterface *qtimer)
{
    QctQtimerInterfaceClass *k = QCT_QTIMER_INTERFACE_GET_CLASS(qtimer);
    return k->get_timer_hi(qtimer);
}

#endif /* HW_TIMER_QCT_QTIMER_H */
