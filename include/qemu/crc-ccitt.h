/*
 * CRC16 (CCITT) Checksum Algorithm
 *
 * Copyright (c) 2021 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * From Linux kernel v5.10 include/linux/crc-ccitt.h
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _CRC_CCITT_H
#define _CRC_CCITT_H

extern uint16_t const crc_ccitt_table[256];
extern uint16_t const crc_ccitt_false_table[256];

extern uint16_t crc_ccitt(uint16_t crc, const uint8_t *buffer, size_t len);
extern uint16_t crc_ccitt_false(uint16_t crc, const uint8_t *buffer, size_t len);

static inline uint16_t crc_ccitt_byte(uint16_t crc, const uint8_t c)
{
    return (crc >> 8) ^ crc_ccitt_table[(crc ^ c) & 0xff];
}

static inline uint16_t crc_ccitt_false_byte(uint16_t crc, const uint8_t c)
{
    return (crc << 8) ^ crc_ccitt_false_table[(crc >> 8) ^ c];
}

#endif /* _CRC_CCITT_H */
