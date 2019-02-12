/*
 * TOD (Time Of Day) clock
 *
 * Copyright 2018 Red Hat, Inc.
 * Author(s): David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TARGET_S390_TOD_H
#define TARGET_S390_TOD_H

/* The value of the TOD clock for 1.1.1970. */
#define TOD_UNIX_EPOCH 0x7d91048bca000000ULL

/* Converts ns to s390's clock format */
static inline uint64_t time2tod(uint64_t ns)
{
    return (ns << 9) / 125 + (((ns & 0xff80000000000000ull) / 125) << 9);
}

/* Converts s390's clock format to ns */
static inline uint64_t tod2time(uint64_t t)
{
    return ((t >> 9) * 125) + (((t & 0x1ff) * 125) >> 9);
}

#endif
