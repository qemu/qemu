/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PPC_SPAPR_FADUMP_H
#define PPC_SPAPR_FADUMP_H

#include "qemu/osdep.h"
#include "cpu.h"

/* Fadump commands */
#define FADUMP_CMD_REGISTER            1
#define FADUMP_CMD_UNREGISTER          2
#define FADUMP_CMD_INVALIDATE          3

#define FADUMP_VERSION                 1

/* Firmware provided dump sections */
#define FADUMP_CPU_STATE_DATA   0x0001
#define FADUMP_HPTE_REGION      0x0002
#define FADUMP_REAL_MODE_REGION 0x0011

/* OS defined sections */
#define FADUMP_PARAM_AREA       0x0100

/* Dump request flag */
#define FADUMP_REQUEST_FLAG     0x00000001

/* Dump status flags */
#define FADUMP_STATUS_DUMP_PERFORMED            0x8000
#define FADUMP_STATUS_DUMP_TRIGGERED            0x4000
#define FADUMP_STATUS_DUMP_ERROR                0x2000

/* Region dump error flags */
#define FADUMP_ERROR_INVALID_DATA_TYPE          0x8000
#define FADUMP_ERROR_INVALID_SOURCE_ADDR        0x4000
#define FADUMP_ERROR_LENGTH_EXCEEDS_SOURCE      0x2000
#define FADUMP_ERROR_INVALID_DEST_ADDR          0x1000
#define FAUDMP_ERROR_DEST_TOO_SMALL             0x0800

/*
 * The Firmware Assisted Dump Memory structure supports a maximum of 10 sections
 * in the dump memory structure. Presently, three sections are used for
 * CPU state data, HPTE & Parameters area, while the remaining seven sections
 * can be used for boot memory regions.
 */
#define FADUMP_MAX_SECTIONS            10

typedef struct FadumpSection FadumpSection;
typedef struct FadumpSectionHeader FadumpSectionHeader;
typedef struct FadumpMemStruct FadumpMemStruct;

struct SpaprMachineState;

/* Kernel Dump section info */
/* All fields are in big-endian */
struct FadumpSection {
    uint32_t    request_flag;
    uint16_t    source_data_type;
    uint16_t    error_flags;
    uint64_t    source_address;
    uint64_t    source_len;
    uint64_t    bytes_dumped;
    uint64_t    destination_address;
};

/* ibm,configure-kernel-dump header. */
struct FadumpSectionHeader {
    uint32_t    dump_format_version;
    uint16_t    dump_num_sections;
    uint16_t    dump_status_flag;
    uint32_t    offset_first_dump_section;

    /* Fields for disk dump option. */
    uint32_t    dd_block_size;
    uint64_t    dd_block_offset;
    uint64_t    dd_num_blocks;
    uint32_t    dd_offset_disk_path;

    /* Maximum time allowed to prevent an automatic dump-reboot. */
    uint32_t    max_time_auto;
};

/* Note: All the data in these structures is in big-endian */
struct FadumpMemStruct {
    FadumpSectionHeader header;
    FadumpSection       rgn[FADUMP_MAX_SECTIONS];
};

uint32_t do_fadump_register(struct SpaprMachineState *, target_ulong);
void     trigger_fadump_boot(struct SpaprMachineState *, target_ulong);
#endif /* PPC_SPAPR_FADUMP_H */
