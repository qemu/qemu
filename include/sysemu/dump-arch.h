/*
 * QEMU dump
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef DUMP_ARCH_H
#define DUMP_ARCH_H

typedef struct ArchDumpInfo {
    int d_machine;           /* Architecture */
    int d_endian;            /* ELFDATA2LSB or ELFDATA2MSB */
    int d_class;             /* ELFCLASS32 or ELFCLASS64 */
    uint32_t page_size;      /* The target's page size. If it's variable and
                              * unknown, then this should be the maximum. */
    uint64_t phys_base;      /* The target's physmem base. */
    void (*arch_sections_add_fn)(DumpState *s);
    uint64_t (*arch_sections_write_hdr_fn)(DumpState *s, uint8_t *buff);
    int (*arch_sections_write_fn)(DumpState *s, uint8_t *buff);
} ArchDumpInfo;

struct GuestPhysBlockList; /* memory_mapping.h */
int cpu_get_dump_info(ArchDumpInfo *info,
                      const struct GuestPhysBlockList *guest_phys_blocks);
ssize_t cpu_get_note_size(int class, int machine, int nr_cpus);

#endif
