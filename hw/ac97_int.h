/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_AC97_INT_H
#define HW_AC97_INT_H

#include "hw.h"
#include "audiodev.h"
#include "audio/audio.h"
#include "pci/pci.h"
#include "sysemu/dma.h"

enum {
    PI_INDEX = 0,   /* PCM in */
    PO_INDEX,       /* PCM out */
    MC_INDEX,       /* Mic in */
    SO_INDEX = 7,   /* SPDIF out */
    LAST_INDEX
};

typedef struct BD {
    uint32_t addr;
    uint32_t ctl_len;
} BD;

typedef struct AC97BusMasterRegs {
    uint32_t bdbar;             /* rw 0, buffer descriptor list base address register */
    uint8_t civ;                /* ro 0, current index value */
    uint8_t lvi;                /* rw 0, last valid index */
    uint16_t sr;                /* rw 1, status register */
    uint16_t picb;              /* ro 0, position in current buffer */
    uint8_t piv;                /* ro 0, prefetched index value */
    uint8_t cr;                 /* rw 0, control register */
    unsigned int bd_valid;
    BD bd;
} AC97BusMasterRegs;

typedef struct AC97LinkState {
    qemu_irq irq;
    DMAContext *dma;
    QEMUSoundCard card;

    uint32_t glob_cnt;
    uint32_t glob_sta;
    uint32_t cas; /* Codec Access Semaphore Register */
    uint32_t last_samp;
    AC97BusMasterRegs bm_regs[LAST_INDEX];
    uint8_t mixer_data[256];

    SWVoiceIn *voice_pi;
    SWVoiceOut *voice_po;
    SWVoiceIn *voice_mc;
    int invalid_freq[LAST_INDEX];
    uint8_t silence[128];
    int bup_flag;
} AC97LinkState;

void ac97_common_init (AC97LinkState *s,
                       qemu_irq irq,
                       DMAContext *dma);


extern const MemoryRegionOps ac97_io_nam_ops;
extern const MemoryRegionOps ac97_io_nabm_ops;


#endif