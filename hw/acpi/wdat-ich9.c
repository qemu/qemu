/*
 * TCO Watchdog Action Table (WDAT)
 *
 * Copyright Red Hat, Inc. 2026
 * Author(s): Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/acpi/wdat.h"
#include "hw/acpi/wdat-ich9.h"
#include "hw/southbridge/ich9.h"

#define TCO_REG(base, reg_offset, reg_width) { .space_id = AML_AS_SYSTEM_IO, \
            .address = base + reg_offset, .bit_width = reg_width, \
            .access_width = AML_WORD_ACC, };

/*
 *   "Hardware Watchdog Timers Design Specification"
 *       https://uefi.org/acpi 'Watchdog Action Table (WDAT)'
 *
 *   ICH9 specific implementation.
 */
void build_ich9_wdat(GArray *table_data, BIOSLinker *linker, const char *oem_id,
                     const char *oem_table_id, uint64_t tco_base)
{
    AcpiTable table = { .sig = "WDAT", .rev = 1, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };
    struct AcpiGenericAddress tco_rld =  TCO_REG(tco_base, 0x0, 16);
    struct AcpiGenericAddress tco2_sts = TCO_REG(tco_base, 0x6, 16);
    struct AcpiGenericAddress tco1_cnt = TCO_REG(tco_base, 0x8, 16);
    struct AcpiGenericAddress tco_tmr =  TCO_REG(tco_base, 0x12, 16);

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 0x20, 4); /* Watchdog Header Length */
    build_append_int_noprefix(table_data, 0xff, 2); /* PCI Segment */
    build_append_int_noprefix(table_data, 0xff, 1); /* PCI Bus Number */
    build_append_int_noprefix(table_data, 0xff, 1); /* PCI Device Number */
    build_append_int_noprefix(table_data, 0xff, 1); /* PCI Function Number */
    build_append_int_noprefix(table_data, 0, 3);    /* Reserved */
    /*
     * limits/resolution are defined by ICH9 TCO spec
     */
    build_append_int_noprefix(table_data, 0x258, 4);/* Timer Period, ms */
    build_append_int_noprefix(table_data, 0x3ff, 4);/* Maximum Count */
    build_append_int_noprefix(table_data, 0x4, 4);  /* Minimum Count */
    /*
     * WATCHDOG_ENABLED & WATCHDOG_STOPPED_IN_SLEEP_STATE
     */
    build_append_int_noprefix(table_data, 0x81, 1); /* Watchdog Flags */
    build_append_int_noprefix(table_data, 0, 3);    /* Reserved */
    /*
     * watchdog instruction entries
     */
    build_append_int_noprefix(table_data, 10 /* # of actions below */, 4);
    /* Action table */
    build_append_wdat_ins(table_data, WDAT_ACTION_RESET,
        WDAT_INS_WRITE_VALUE,
        tco_rld, 0x1, 0x1ff);
    build_append_wdat_ins(table_data, WDAT_ACTION_QUERY_RUNNING_STATE,
        WDAT_INS_READ_VALUE,
        tco1_cnt, 0x0, 0x800);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_RUNNING_STATE,
        WDAT_INS_WRITE_VALUE | WDAT_INS_PRESERVE_REGISTER,
        tco1_cnt, 0, 0x800);
    build_append_wdat_ins(table_data, WDAT_ACTION_QUERY_STOPPED_STATE,
        WDAT_INS_READ_VALUE,
        tco1_cnt, 0x800, 0x800);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_STOPPED_STATE,
        WDAT_INS_WRITE_VALUE | WDAT_INS_PRESERVE_REGISTER,
        tco1_cnt, 0x800, 0x800);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_COUNTDOWN_PERIOD,
        WDAT_INS_WRITE_COUNTDOWN,
        tco_tmr, 0x0, 0x3FF);
    build_append_wdat_ins(table_data, WDAT_ACTION_QUERY_COUNTDOWN_PERIOD,
        WDAT_INS_READ_COUNTDOWN,
        tco_tmr, 0x0, 0x3FF);
    build_append_wdat_ins(table_data, WDAT_ACTION_QUERY_WATCHDOG_STATUS,
        WDAT_INS_READ_VALUE,
        tco2_sts, 0x2, 0x2);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_WATCHDOG_STATUS,
        WDAT_INS_WRITE_VALUE | WDAT_INS_PRESERVE_REGISTER,
        tco2_sts, 0x2, 0x2);
    build_append_wdat_ins(table_data, WDAT_ACTION_SET_WATCHDOG_STATUS,
        WDAT_INS_WRITE_VALUE | WDAT_INS_PRESERVE_REGISTER,
        tco2_sts, 0x4, 0x4);

    acpi_table_end(linker, &table);
}
