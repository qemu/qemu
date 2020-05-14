/*
 *  QEMU UUID functions
 *
 *  Copyright 2016 Red Hat, Inc.
 *
 *  Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef QEMU_UUID_H
#define QEMU_UUID_H


/* Version 4 UUID (pseudo random numbers), RFC4122 4.4. */

typedef struct {
    union {
        unsigned char data[16];
        struct {
            /* Generated in BE endian, can be swapped with qemu_uuid_bswap. */
            uint32_t time_low;
            uint16_t time_mid;
            uint16_t time_high_and_version;
            uint8_t  clock_seq_and_reserved;
            uint8_t  clock_seq_low;
            uint8_t  node[6];
        } fields;
    };
} QemuUUID;

/**
 * UUID_LE - converts the fields of UUID to little-endian array,
 * each of parameters is the filed of UUID.
 *
 * @time_low: The low field of the timestamp
 * @time_mid: The middle field of the timestamp
 * @time_hi_and_version: The high field of the timestamp
 *                       multiplexed with the version number
 * @clock_seq_hi_and_reserved: The high field of the clock
 *                             sequence multiplexed with the variant
 * @clock_seq_low: The low field of the clock sequence
 * @node0: The spatially unique node0 identifier
 * @node1: The spatially unique node1 identifier
 * @node2: The spatially unique node2 identifier
 * @node3: The spatially unique node3 identifier
 * @node4: The spatially unique node4 identifier
 * @node5: The spatially unique node5 identifier
 */
#define UUID_LE(time_low, time_mid, time_hi_and_version,                    \
  clock_seq_hi_and_reserved, clock_seq_low, node0, node1, node2,            \
  node3, node4, node5)                                                      \
  { (time_low) & 0xff, ((time_low) >> 8) & 0xff, ((time_low) >> 16) & 0xff, \
    ((time_low) >> 24) & 0xff, (time_mid) & 0xff, ((time_mid) >> 8) & 0xff, \
    (time_hi_and_version) & 0xff, ((time_hi_and_version) >> 8) & 0xff,      \
    (clock_seq_hi_and_reserved), (clock_seq_low), (node0), (node1), (node2),\
    (node3), (node4), (node5) }

#define UUID_FMT "%02hhx%02hhx%02hhx%02hhx-" \
                 "%02hhx%02hhx-%02hhx%02hhx-" \
                 "%02hhx%02hhx-" \
                 "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"

#define UUID_FMT_LEN 36

#define UUID_NONE "00000000-0000-0000-0000-000000000000"

void qemu_uuid_generate(QemuUUID *out);

int qemu_uuid_is_null(const QemuUUID *uu);

int qemu_uuid_is_equal(const QemuUUID *lhv, const QemuUUID *rhv);

void qemu_uuid_unparse(const QemuUUID *uuid, char *out);

char *qemu_uuid_unparse_strdup(const QemuUUID *uuid);

int qemu_uuid_parse(const char *str, QemuUUID *uuid);

QemuUUID qemu_uuid_bswap(QemuUUID uuid);

#endif
