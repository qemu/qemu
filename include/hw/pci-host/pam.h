#ifndef QEMU_PAM_H
#define QEMU_PAM_H

/*
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2011 Isaku Yamahata <yamahata at valinux co jp>
 *               VA Linux Systems Japan K.K.
 * Copyright (c) 2012 Jason Baron <jbaron@redhat.com>
 *
 * Split out from piix.c
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

/*
 * SMRAM memory area and PAM memory area in Legacy address range for PC.
 * PAM: Programmable Attribute Map registers
 *
 * 0xa0000 - 0xbffff compatible SMRAM
 *
 * 0xc0000 - 0xc3fff Expansion area memory segments
 * 0xc4000 - 0xc7fff
 * 0xc8000 - 0xcbfff
 * 0xcc000 - 0xcffff
 * 0xd0000 - 0xd3fff
 * 0xd4000 - 0xd7fff
 * 0xd8000 - 0xdbfff
 * 0xdc000 - 0xdffff
 * 0xe0000 - 0xe3fff Extended System BIOS Area Memory Segments
 * 0xe4000 - 0xe7fff
 * 0xe8000 - 0xebfff
 * 0xec000 - 0xeffff
 *
 * 0xf0000 - 0xfffff System BIOS Area Memory Segments
 */

#include "exec/memory.h"

#define SMRAM_C_BASE    0xa0000
#define SMRAM_C_END     0xc0000
#define SMRAM_C_SIZE    0x20000

#define PAM_EXPAN_BASE  0xc0000
#define PAM_EXPAN_SIZE  0x04000

#define PAM_EXBIOS_BASE 0xe0000
#define PAM_EXBIOS_SIZE 0x04000

#define PAM_BIOS_BASE   0xf0000
#define PAM_BIOS_END    0xfffff
/* 64KB: Intel 3 series express chipset family p. 58*/
#define PAM_BIOS_SIZE   0x10000

/* PAM registers: log nibble and high nibble*/
#define PAM_ATTR_WE     ((uint8_t)2)
#define PAM_ATTR_RE     ((uint8_t)1)
#define PAM_ATTR_MASK   ((uint8_t)3)

/* SMRAM register */
#define SMRAM_D_OPEN           ((uint8_t)(1 << 6))
#define SMRAM_D_CLS            ((uint8_t)(1 << 5))
#define SMRAM_D_LCK            ((uint8_t)(1 << 4))
#define SMRAM_G_SMRAME         ((uint8_t)(1 << 3))
#define SMRAM_C_BASE_SEG_MASK  ((uint8_t)0x7)
#define SMRAM_C_BASE_SEG       ((uint8_t)0x2)  /* hardwired to b010 */

#define PAM_REGIONS_COUNT       13

typedef struct PAMMemoryRegion {
    MemoryRegion alias[4];  /* index = PAM value */
    unsigned current;
} PAMMemoryRegion;

void init_pam(PAMMemoryRegion *mem, Object *owner, MemoryRegion *ram,
              MemoryRegion *system, MemoryRegion *pci,
              uint32_t start, uint32_t size);
void pam_update(PAMMemoryRegion *mem, int idx, uint8_t val);

#endif /* QEMU_PAM_H */
