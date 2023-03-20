/*
 * Nuvoton NPCM7xx OTP (Fuse Array) Interface
 *
 * Copyright 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */
#ifndef NPCM7XX_OTP_H
#define NPCM7XX_OTP_H

#include "exec/memory.h"
#include "hw/sysbus.h"

/* Each OTP module holds 8192 bits of one-time programmable storage */
#define NPCM7XX_OTP_ARRAY_BITS (8192)
#define NPCM7XX_OTP_ARRAY_BYTES (NPCM7XX_OTP_ARRAY_BITS / BITS_PER_BYTE)

/* Fuse array offsets */
#define NPCM7XX_FUSE_FUSTRAP (0)
#define NPCM7XX_FUSE_CP_FUSTRAP (12)
#define NPCM7XX_FUSE_DAC_CALIB (16)
#define NPCM7XX_FUSE_ADC_CALIB (24)
#define NPCM7XX_FUSE_DERIVATIVE (64)
#define NPCM7XX_FUSE_TEST_SIG (72)
#define NPCM7XX_FUSE_DIE_LOCATION (74)
#define NPCM7XX_FUSE_GP1 (80)
#define NPCM7XX_FUSE_GP2 (128)

/*
 * Number of registers in our device state structure. Don't change this without
 * incrementing the version_id in the vmstate.
 */
#define NPCM7XX_OTP_NR_REGS (0x18 / sizeof(uint32_t))

/**
 * struct NPCM7xxOTPState - Device state for one OTP module.
 * @parent: System bus device.
 * @mmio: Memory region through which registers are accessed.
 * @regs: Register contents.
 * @array: OTP storage array.
 */
typedef struct NPCM7xxOTPState {
    SysBusDevice parent;

    MemoryRegion mmio;
    uint32_t regs[NPCM7XX_OTP_NR_REGS];
    uint8_t array[NPCM7XX_OTP_ARRAY_BYTES];
} NPCM7xxOTPState;

#define TYPE_NPCM7XX_OTP "npcm7xx-otp"
#define NPCM7XX_OTP(obj) OBJECT_CHECK(NPCM7xxOTPState, (obj), TYPE_NPCM7XX_OTP)

#define TYPE_NPCM7XX_KEY_STORAGE "npcm7xx-key-storage"
#define TYPE_NPCM7XX_FUSE_ARRAY "npcm7xx-fuse-array"

typedef struct NPCM7xxOTPClass NPCM7xxOTPClass;

/**
 * npcm7xx_otp_array_write - ECC encode and write data to OTP array.
 * @s: OTP module.
 * @data: Data to be encoded and written.
 * @offset: Offset of first byte to be written in the OTP array.
 * @len: Number of bytes before ECC encoding.
 *
 * Each nibble of data is encoded into a byte, so the number of bytes written
 * to the array will be @len * 2.
 */
void npcm7xx_otp_array_write(NPCM7xxOTPState *s, const void *data,
                             unsigned int offset, unsigned int len);

#endif /* NPCM7XX_OTP_H */
