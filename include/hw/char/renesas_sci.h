/*
 * Renesas Serial Communication Interface
 *
 * Copyright (c) 2018 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_RENESAS_SCI_H
#define HW_CHAR_RENESAS_SCI_H

#include "chardev/char-fe.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RENESAS_SCI "renesas-sci"
typedef struct RSCIState RSCIState;
DECLARE_INSTANCE_CHECKER(RSCIState, RSCI,
                         TYPE_RENESAS_SCI)

enum {
    ERI = 0,
    RXI = 1,
    TXI = 2,
    TEI = 3,
    SCI_NR_IRQ = 4
};

struct RSCIState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    QEMUTimer timer;
    CharBackend chr;
    qemu_irq irq[SCI_NR_IRQ];

    uint8_t smr;
    uint8_t brr;
    uint8_t scr;
    uint8_t tdr;
    uint8_t ssr;
    uint8_t rdr;
    uint8_t scmr;
    uint8_t semr;

    uint8_t read_ssr;
    int64_t trtime;
    int64_t rx_next;
    uint64_t input_freq;
};

#endif
