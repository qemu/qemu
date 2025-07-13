/*
 * MAX78000 AES
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MAX78000_AES_H
#define HW_MAX78000_AES_H

#include "hw/sysbus.h"
#include "crypto/aes.h"
#include "qom/object.h"

#define TYPE_MAX78000_AES "max78000-aes"
OBJECT_DECLARE_SIMPLE_TYPE(Max78000AesState, MAX78000_AES)

#define CTRL 0
#define STATUS 4
#define INTFL 8
#define INTEN 0xc
#define FIFO 0x10

#define KEY_BASE 0x400
#define KEY_END 0x420

/* CTRL */
#define TYPE (1 << 9 | 1 << 8)
#define KEY_SIZE (1 << 7 | 1 << 6)
#define OUTPUT_FLUSH (1 << 5)
#define INPUT_FLUSH (1 << 4)
#define START (1 << 3)

#define AES_EN (1 << 0)

/* STATUS */
#define OUTPUT_FULL (1 << 4)
#define OUTPUT_EMPTY (1 << 3)
#define INPUT_FULL (1 << 2)
#define INPUT_EMPTY (1 << 1)
#define BUSY (1 << 0)

/* INTFL*/
#define DONE (1 << 0)

struct Max78000AesState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t ctrl;
    uint32_t status;
    uint32_t intfl;
    uint32_t inten;
    uint32_t data_index;
    uint8_t data[16];

    uint8_t key[32];
    AES_KEY internal_key;

    uint32_t result_index;
    uint8_t result[16];


    qemu_irq irq;
};

#endif
