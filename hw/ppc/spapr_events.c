/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * RTAS events handling
 *
 * Copyright (c) 2012 David Gibson, IBM Corporation.
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
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/device_tree.h"
#include "sysemu/runstate.h"

#include "hw/ppc/fdt.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/pci/pci.h"
#include "hw/irq.h"
#include "hw/pci-host/spapr.h"
#include "hw/ppc/spapr_drc.h"
#include "qemu/help_option.h"
#include "qemu/bcd.h"
#include "qemu/main-loop.h"
#include "hw/ppc/spapr_ovec.h"
#include <libfdt.h>
#include "migration/blocker.h"

#define RTAS_LOG_VERSION_MASK                   0xff000000
#define   RTAS_LOG_VERSION_6                    0x06000000
#define RTAS_LOG_SEVERITY_MASK                  0x00e00000
#define   RTAS_LOG_SEVERITY_ALREADY_REPORTED    0x00c00000
#define   RTAS_LOG_SEVERITY_FATAL               0x00a00000
#define   RTAS_LOG_SEVERITY_ERROR               0x00800000
#define   RTAS_LOG_SEVERITY_ERROR_SYNC          0x00600000
#define   RTAS_LOG_SEVERITY_WARNING             0x00400000
#define   RTAS_LOG_SEVERITY_EVENT               0x00200000
#define   RTAS_LOG_SEVERITY_NO_ERROR            0x00000000
#define RTAS_LOG_DISPOSITION_MASK               0x00180000
#define   RTAS_LOG_DISPOSITION_FULLY_RECOVERED  0x00000000
#define   RTAS_LOG_DISPOSITION_LIMITED_RECOVERY 0x00080000
#define   RTAS_LOG_DISPOSITION_NOT_RECOVERED    0x00100000
#define RTAS_LOG_OPTIONAL_PART_PRESENT          0x00040000
#define RTAS_LOG_INITIATOR_MASK                 0x0000f000
#define   RTAS_LOG_INITIATOR_UNKNOWN            0x00000000
#define   RTAS_LOG_INITIATOR_CPU                0x00001000
#define   RTAS_LOG_INITIATOR_PCI                0x00002000
#define   RTAS_LOG_INITIATOR_MEMORY             0x00004000
#define   RTAS_LOG_INITIATOR_HOTPLUG            0x00006000
#define RTAS_LOG_TARGET_MASK                    0x00000f00
#define   RTAS_LOG_TARGET_UNKNOWN               0x00000000
#define   RTAS_LOG_TARGET_CPU                   0x00000100
#define   RTAS_LOG_TARGET_PCI                   0x00000200
#define   RTAS_LOG_TARGET_MEMORY                0x00000400
#define   RTAS_LOG_TARGET_HOTPLUG               0x00000600
#define RTAS_LOG_TYPE_MASK                      0x000000ff
#define   RTAS_LOG_TYPE_OTHER                   0x00000000
#define   RTAS_LOG_TYPE_RETRY                   0x00000001
#define   RTAS_LOG_TYPE_TCE_ERR                 0x00000002
#define   RTAS_LOG_TYPE_INTERN_DEV_FAIL         0x00000003
#define   RTAS_LOG_TYPE_TIMEOUT                 0x00000004
#define   RTAS_LOG_TYPE_DATA_PARITY             0x00000005
#define   RTAS_LOG_TYPE_ADDR_PARITY             0x00000006
#define   RTAS_LOG_TYPE_CACHE_PARITY            0x00000007
#define   RTAS_LOG_TYPE_ADDR_INVALID            0x00000008
#define   RTAS_LOG_TYPE_ECC_UNCORR              0x00000009
#define   RTAS_LOG_TYPE_ECC_CORR                0x0000000a
#define   RTAS_LOG_TYPE_EPOW                    0x00000040
#define   RTAS_LOG_TYPE_HOTPLUG                 0x000000e5

struct rtas_error_log {
    uint32_t summary;
    uint32_t extended_length;
} QEMU_PACKED;

struct rtas_event_log_v6 {
    uint8_t b0;
#define RTAS_LOG_V6_B0_VALID                          0x80
#define RTAS_LOG_V6_B0_UNRECOVERABLE_ERROR            0x40
#define RTAS_LOG_V6_B0_RECOVERABLE_ERROR              0x20
#define RTAS_LOG_V6_B0_DEGRADED_OPERATION             0x10
#define RTAS_LOG_V6_B0_PREDICTIVE_ERROR               0x08
#define RTAS_LOG_V6_B0_NEW_LOG                        0x04
#define RTAS_LOG_V6_B0_BIGENDIAN                      0x02
    uint8_t _resv1;
    uint8_t b2;
#define RTAS_LOG_V6_B2_POWERPC_FORMAT                 0x80
#define RTAS_LOG_V6_B2_LOG_FORMAT_MASK                0x0f
#define   RTAS_LOG_V6_B2_LOG_FORMAT_PLATFORM_EVENT    0x0e
    uint8_t _resv2[9];
    uint32_t company;
#define RTAS_LOG_V6_COMPANY_IBM                 0x49424d00 /* IBM<null> */
} QEMU_PACKED;

struct rtas_event_log_v6_section_header {
    uint16_t section_id;
    uint16_t section_length;
    uint8_t section_version;
    uint8_t section_subtype;
    uint16_t creator_component_id;
} QEMU_PACKED;

struct rtas_event_log_v6_maina {
#define RTAS_LOG_V6_SECTION_ID_MAINA                0x5048 /* PH */
    struct rtas_event_log_v6_section_header hdr;
    uint32_t creation_date; /* BCD: YYYYMMDD */
    uint32_t creation_time; /* BCD: HHMMSS00 */
    uint8_t _platform1[8];
    char creator_id;
    uint8_t _resv1[2];
    uint8_t section_count;
    uint8_t _resv2[4];
    uint8_t _platform2[8];
    uint32_t plid;
    uint8_t _platform3[4];
} QEMU_PACKED;

struct rtas_event_log_v6_mainb {
#define RTAS_LOG_V6_SECTION_ID_MAINB                0x5548 /* UH */
    struct rtas_event_log_v6_section_header hdr;
    uint8_t subsystem_id;
    uint8_t _platform1;
    uint8_t event_severity;
    uint8_t event_subtype;
    uint8_t _platform2[4];
    uint8_t _resv1[2];
    uint16_t action_flags;
    uint8_t _resv2[4];
} QEMU_PACKED;

struct rtas_event_log_v6_epow {
#define RTAS_LOG_V6_SECTION_ID_EPOW                 0x4550 /* EP */
    struct rtas_event_log_v6_section_header hdr;
    uint8_t sensor_value;
#define RTAS_LOG_V6_EPOW_ACTION_RESET                    0
#define RTAS_LOG_V6_EPOW_ACTION_WARN_COOLING             1
#define RTAS_LOG_V6_EPOW_ACTION_WARN_POWER               2
#define RTAS_LOG_V6_EPOW_ACTION_SYSTEM_SHUTDOWN          3
#define RTAS_LOG_V6_EPOW_ACTION_SYSTEM_HALT              4
#define RTAS_LOG_V6_EPOW_ACTION_MAIN_ENCLOSURE           5
#define RTAS_LOG_V6_EPOW_ACTION_POWER_OFF                7
    uint8_t event_modifier;
#define RTAS_LOG_V6_EPOW_MODIFIER_NORMAL                 1
#define RTAS_LOG_V6_EPOW_MODIFIER_ON_UPS                 2
#define RTAS_LOG_V6_EPOW_MODIFIER_CRITICAL               3
#define RTAS_LOG_V6_EPOW_MODIFIER_TEMPERATURE            4
    uint8_t extended_modifier;
#define RTAS_LOG_V6_EPOW_XMODIFIER_SYSTEM_WIDE           0
#define RTAS_LOG_V6_EPOW_XMODIFIER_PARTITION_SPECIFIC    1
    uint8_t _resv;
    uint64_t reason_code;
} QEMU_PACKED;

struct epow_extended_log {
    struct rtas_event_log_v6 v6hdr;
    struct rtas_event_log_v6_maina maina;
    struct rtas_event_log_v6_mainb mainb;
    struct rtas_event_log_v6_epow epow;
} QEMU_PACKED;

union drc_identifier {
    uint32_t index;
    uint32_t count;
    struct {
        uint32_t count;
        uint32_t index;
    } count_indexed;
    char name[1];
} QEMU_PACKED;

struct rtas_event_log_v6_hp {
#define RTAS_LOG_V6_SECTION_ID_HOTPLUG              0x4850 /* HP */
    struct rtas_event_log_v6_section_header hdr;
    uint8_t hotplug_type;
#define RTAS_LOG_V6_HP_TYPE_CPU                          1
#define RTAS_LOG_V6_HP_TYPE_MEMORY                       2
#define RTAS_LOG_V6_HP_TYPE_SLOT                         3
#define RTAS_LOG_V6_HP_TYPE_PHB                          4
#define RTAS_LOG_V6_HP_TYPE_PCI                          5
#define RTAS_LOG_V6_HP_TYPE_PMEM                         6
    uint8_t hotplug_action;
#define RTAS_LOG_V6_HP_ACTION_ADD                        1
#define RTAS_LOG_V6_HP_ACTION_REMOVE                     2
    uint8_t hotplug_identifier;
#define RTAS_LOG_V6_HP_ID_DRC_NAME                       1
#define RTAS_LOG_V6_HP_ID_DRC_INDEX                      2
#define RTAS_LOG_V6_HP_ID_DRC_COUNT                      3
#define RTAS_LOG_V6_HP_ID_DRC_COUNT_INDEXED              4
    uint8_t reserved;
    union drc_identifier drc_id;
} QEMU_PACKED;

struct hp_extended_log {
    struct rtas_event_log_v6 v6hdr;
    struct rtas_event_log_v6_maina maina;
    struct rtas_event_log_v6_mainb mainb;
    struct rtas_event_log_v6_hp hp;
} QEMU_PACKED;

struct rtas_event_log_v6_mc {
#define RTAS_LOG_V6_SECTION_ID_MC                   0x4D43 /* MC */
    struct rtas_event_log_v6_section_header hdr;
    uint32_t fru_id;
    uint32_t proc_id;
    uint8_t error_type;
#define RTAS_LOG_V6_MC_TYPE_UE                           0
#define RTAS_LOG_V6_MC_TYPE_SLB                          1
#define RTAS_LOG_V6_MC_TYPE_ERAT                         2
#define RTAS_LOG_V6_MC_TYPE_TLB                          4
#define RTAS_LOG_V6_MC_TYPE_D_CACHE                      5
#define RTAS_LOG_V6_MC_TYPE_I_CACHE                      7
    uint8_t sub_err_type;
#define RTAS_LOG_V6_MC_UE_INDETERMINATE                  0
#define RTAS_LOG_V6_MC_UE_IFETCH                         1
#define RTAS_LOG_V6_MC_UE_PAGE_TABLE_WALK_IFETCH         2
#define RTAS_LOG_V6_MC_UE_LOAD_STORE                     3
#define RTAS_LOG_V6_MC_UE_PAGE_TABLE_WALK_LOAD_STORE     4
#define RTAS_LOG_V6_MC_SLB_PARITY                        0
#define RTAS_LOG_V6_MC_SLB_MULTIHIT                      1
#define RTAS_LOG_V6_MC_SLB_INDETERMINATE                 2
#define RTAS_LOG_V6_MC_ERAT_PARITY                       1
#define RTAS_LOG_V6_MC_ERAT_MULTIHIT                     2
#define RTAS_LOG_V6_MC_ERAT_INDETERMINATE                3
#define RTAS_LOG_V6_MC_TLB_PARITY                        1
#define RTAS_LOG_V6_MC_TLB_MULTIHIT                      2
#define RTAS_LOG_V6_MC_TLB_INDETERMINATE                 3
/*
 * Per PAPR,
 * For UE error type, set bit 1 of sub_err_type to indicate effective addr is
 * provided. For other error types (SLB/ERAT/TLB), set bit 0 to indicate
 * same.
 */
#define RTAS_LOG_V6_MC_UE_EA_ADDR_PROVIDED               0x40
#define RTAS_LOG_V6_MC_EA_ADDR_PROVIDED                  0x80
    uint8_t reserved_1[6];
    uint64_t effective_address;
    uint64_t logical_address;
} QEMU_PACKED;

struct mc_extended_log {
    struct rtas_event_log_v6 v6hdr;
    struct rtas_event_log_v6_mc mc;
} QEMU_PACKED;

struct MC_ierror_table {
    unsigned long srr1_mask;
    unsigned long srr1_value;
    bool nip_valid; /* nip is a valid indicator of faulting address */
    uint8_t error_type;
    uint8_t error_subtype;
    unsigned int initiator;
    unsigned int severity;
};

static const struct MC_ierror_table mc_ierror_table[] = {
{ 0x00000000081c0000, 0x0000000000040000, true,
  RTAS_LOG_V6_MC_TYPE_UE, RTAS_LOG_V6_MC_UE_IFETCH,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000080000, true,
  RTAS_LOG_V6_MC_TYPE_SLB, RTAS_LOG_V6_MC_SLB_PARITY,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000000081c0000, 0x00000000000c0000, true,
  RTAS_LOG_V6_MC_TYPE_SLB, RTAS_LOG_V6_MC_SLB_MULTIHIT,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000100000, true,
  RTAS_LOG_V6_MC_TYPE_ERAT, RTAS_LOG_V6_MC_ERAT_MULTIHIT,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000140000, true,
  RTAS_LOG_V6_MC_TYPE_TLB, RTAS_LOG_V6_MC_TLB_MULTIHIT,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000180000, true,
  RTAS_LOG_V6_MC_TYPE_UE, RTAS_LOG_V6_MC_UE_PAGE_TABLE_WALK_IFETCH,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, } };

struct MC_derror_table {
    unsigned long dsisr_value;
    bool dar_valid; /* dar is a valid indicator of faulting address */
    uint8_t error_type;
    uint8_t error_subtype;
    unsigned int initiator;
    unsigned int severity;
};

static const struct MC_derror_table mc_derror_table[] = {
{ 0x00008000, false,
  RTAS_LOG_V6_MC_TYPE_UE, RTAS_LOG_V6_MC_UE_LOAD_STORE,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00004000, true,
  RTAS_LOG_V6_MC_TYPE_UE, RTAS_LOG_V6_MC_UE_PAGE_TABLE_WALK_LOAD_STORE,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000800, true,
  RTAS_LOG_V6_MC_TYPE_ERAT, RTAS_LOG_V6_MC_ERAT_MULTIHIT,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000400, true,
  RTAS_LOG_V6_MC_TYPE_TLB, RTAS_LOG_V6_MC_TLB_MULTIHIT,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000080, true,
  RTAS_LOG_V6_MC_TYPE_SLB, RTAS_LOG_V6_MC_SLB_MULTIHIT,  /* Before PARITY */
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, },
{ 0x00000100, true,
  RTAS_LOG_V6_MC_TYPE_SLB, RTAS_LOG_V6_MC_SLB_PARITY,
  RTAS_LOG_INITIATOR_CPU, RTAS_LOG_SEVERITY_ERROR_SYNC, } };

#define SRR1_MC_LOADSTORE(srr1) ((srr1) & PPC_BIT(42))

typedef enum EventClass {
    EVENT_CLASS_INTERNAL_ERRORS     = 0,
    EVENT_CLASS_EPOW                = 1,
    EVENT_CLASS_RESERVED            = 2,
    EVENT_CLASS_HOT_PLUG            = 3,
    EVENT_CLASS_IO                  = 4,
    EVENT_CLASS_MAX
} EventClassIndex;
#define EVENT_CLASS_MASK(index) (1 << (31 - index))

static const char * const event_names[EVENT_CLASS_MAX] = {
    [EVENT_CLASS_INTERNAL_ERRORS]       = "internal-errors",
    [EVENT_CLASS_EPOW]                  = "epow-events",
    [EVENT_CLASS_HOT_PLUG]              = "hot-plug-events",
    [EVENT_CLASS_IO]                    = "ibm,io-events",
};

struct SpaprEventSource {
    int irq;
    uint32_t mask;
    bool enabled;
};

static SpaprEventSource *spapr_event_sources_new(void)
{
    return g_new0(SpaprEventSource, EVENT_CLASS_MAX);
}

static void spapr_event_sources_register(SpaprEventSource *event_sources,
                                        EventClassIndex index, int irq)
{
    /* we only support 1 irq per event class at the moment */
    g_assert(event_sources);
    g_assert(!event_sources[index].enabled);
    event_sources[index].irq = irq;
    event_sources[index].mask = EVENT_CLASS_MASK(index);
    event_sources[index].enabled = true;
}

static const SpaprEventSource *
spapr_event_sources_get_source(SpaprEventSource *event_sources,
                               EventClassIndex index)
{
    g_assert(index < EVENT_CLASS_MAX);
    g_assert(event_sources);

    return &event_sources[index];
}

void spapr_dt_events(SpaprMachineState *spapr, void *fdt)
{
    uint32_t irq_ranges[EVENT_CLASS_MAX * 2];
    int i, count = 0, event_sources;
    SpaprEventSource *events = spapr->event_sources;

    g_assert(events);

    _FDT(event_sources = fdt_add_subnode(fdt, 0, "event-sources"));

    for (i = 0, count = 0; i < EVENT_CLASS_MAX; i++) {
        int node_offset;
        uint32_t interrupts[2];
        const SpaprEventSource *source =
            spapr_event_sources_get_source(events, i);
        const char *source_name = event_names[i];

        if (!source->enabled) {
            continue;
        }

        spapr_dt_irq(interrupts, source->irq, false);

        _FDT(node_offset = fdt_add_subnode(fdt, event_sources, source_name));
        _FDT(fdt_setprop(fdt, node_offset, "interrupts", interrupts,
                         sizeof(interrupts)));

        irq_ranges[count++] = interrupts[0];
        irq_ranges[count++] = cpu_to_be32(1);
    }

    _FDT((fdt_setprop(fdt, event_sources, "interrupt-controller", NULL, 0)));
    _FDT((fdt_setprop_cell(fdt, event_sources, "#interrupt-cells", 2)));
    _FDT((fdt_setprop(fdt, event_sources, "interrupt-ranges",
                      irq_ranges, count * sizeof(uint32_t))));
}

static const SpaprEventSource *
rtas_event_log_to_source(SpaprMachineState *spapr, int log_type)
{
    const SpaprEventSource *source;

    g_assert(spapr->event_sources);

    switch (log_type) {
    case RTAS_LOG_TYPE_HOTPLUG:
        source = spapr_event_sources_get_source(spapr->event_sources,
                                                EVENT_CLASS_HOT_PLUG);
        if (spapr_ovec_test(spapr->ov5_cas, OV5_HP_EVT)) {
            g_assert(source->enabled);
            break;
        }
        /* fall through back to epow for legacy hotplug interrupt source */
    case RTAS_LOG_TYPE_EPOW:
        source = spapr_event_sources_get_source(spapr->event_sources,
                                                EVENT_CLASS_EPOW);
        break;
    default:
        source = NULL;
    }

    return source;
}

static int rtas_event_log_to_irq(SpaprMachineState *spapr, int log_type)
{
    const SpaprEventSource *source;

    source = rtas_event_log_to_source(spapr, log_type);
    g_assert(source);
    g_assert(source->enabled);

    return source->irq;
}

static uint32_t spapr_event_log_entry_type(SpaprEventLogEntry *entry)
{
    return entry->summary & RTAS_LOG_TYPE_MASK;
}

static void rtas_event_log_queue(SpaprMachineState *spapr,
                                 SpaprEventLogEntry *entry)
{
    QTAILQ_INSERT_TAIL(&spapr->pending_events, entry, next);
}

static SpaprEventLogEntry *rtas_event_log_dequeue(SpaprMachineState *spapr,
                                                  uint32_t event_mask)
{
    SpaprEventLogEntry *entry = NULL;

    QTAILQ_FOREACH(entry, &spapr->pending_events, next) {
        const SpaprEventSource *source =
            rtas_event_log_to_source(spapr,
                                     spapr_event_log_entry_type(entry));

        g_assert(source);
        if (source->mask & event_mask) {
            break;
        }
    }

    if (entry) {
        QTAILQ_REMOVE(&spapr->pending_events, entry, next);
    }

    return entry;
}

static bool rtas_event_log_contains(SpaprMachineState *spapr, uint32_t event_mask)
{
    SpaprEventLogEntry *entry = NULL;

    QTAILQ_FOREACH(entry, &spapr->pending_events, next) {
        const SpaprEventSource *source =
            rtas_event_log_to_source(spapr,
                                     spapr_event_log_entry_type(entry));

        if (source->mask & event_mask) {
            return true;
        }
    }

    return false;
}

static uint32_t next_plid;

static void spapr_init_v6hdr(struct rtas_event_log_v6 *v6hdr)
{
    v6hdr->b0 = RTAS_LOG_V6_B0_VALID | RTAS_LOG_V6_B0_NEW_LOG
        | RTAS_LOG_V6_B0_BIGENDIAN;
    v6hdr->b2 = RTAS_LOG_V6_B2_POWERPC_FORMAT
        | RTAS_LOG_V6_B2_LOG_FORMAT_PLATFORM_EVENT;
    v6hdr->company = cpu_to_be32(RTAS_LOG_V6_COMPANY_IBM);
}

static void spapr_init_maina(SpaprMachineState *spapr,
                             struct rtas_event_log_v6_maina *maina,
                             int section_count)
{
    struct tm tm;
    int year;

    maina->hdr.section_id = cpu_to_be16(RTAS_LOG_V6_SECTION_ID_MAINA);
    maina->hdr.section_length = cpu_to_be16(sizeof(*maina));
    /* FIXME: section version, subtype and creator id? */
    spapr_rtc_read(&spapr->rtc, &tm, NULL);
    year = tm.tm_year + 1900;
    maina->creation_date = cpu_to_be32((to_bcd(year / 100) << 24)
                                       | (to_bcd(year % 100) << 16)
                                       | (to_bcd(tm.tm_mon + 1) << 8)
                                       | to_bcd(tm.tm_mday));
    maina->creation_time = cpu_to_be32((to_bcd(tm.tm_hour) << 24)
                                       | (to_bcd(tm.tm_min) << 16)
                                       | (to_bcd(tm.tm_sec) << 8));
    maina->creator_id = 'H'; /* Hypervisor */
    maina->section_count = section_count;
    maina->plid = next_plid++;
}

static void spapr_powerdown_req(Notifier *n, void *opaque)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    SpaprEventLogEntry *entry;
    struct rtas_event_log_v6 *v6hdr;
    struct rtas_event_log_v6_maina *maina;
    struct rtas_event_log_v6_mainb *mainb;
    struct rtas_event_log_v6_epow *epow;
    struct epow_extended_log *new_epow;

    entry = g_new(SpaprEventLogEntry, 1);
    new_epow = g_malloc0(sizeof(*new_epow));
    entry->extended_log = new_epow;

    v6hdr = &new_epow->v6hdr;
    maina = &new_epow->maina;
    mainb = &new_epow->mainb;
    epow = &new_epow->epow;

    entry->summary = RTAS_LOG_VERSION_6
                       | RTAS_LOG_SEVERITY_EVENT
                       | RTAS_LOG_DISPOSITION_NOT_RECOVERED
                       | RTAS_LOG_OPTIONAL_PART_PRESENT
                       | RTAS_LOG_TYPE_EPOW;
    entry->extended_length = sizeof(*new_epow);

    spapr_init_v6hdr(v6hdr);
    spapr_init_maina(spapr, maina, 3 /* Main-A, Main-B and EPOW */);

    mainb->hdr.section_id = cpu_to_be16(RTAS_LOG_V6_SECTION_ID_MAINB);
    mainb->hdr.section_length = cpu_to_be16(sizeof(*mainb));
    /* FIXME: section version, subtype and creator id? */
    mainb->subsystem_id = 0xa0; /* External environment */
    mainb->event_severity = 0x00; /* Informational / non-error */
    mainb->event_subtype = 0xd0; /* Normal shutdown */

    epow->hdr.section_id = cpu_to_be16(RTAS_LOG_V6_SECTION_ID_EPOW);
    epow->hdr.section_length = cpu_to_be16(sizeof(*epow));
    epow->hdr.section_version = 2; /* includes extended modifier */
    /* FIXME: section subtype and creator id? */
    epow->sensor_value = RTAS_LOG_V6_EPOW_ACTION_SYSTEM_SHUTDOWN;
    epow->event_modifier = RTAS_LOG_V6_EPOW_MODIFIER_NORMAL;
    epow->extended_modifier = RTAS_LOG_V6_EPOW_XMODIFIER_PARTITION_SPECIFIC;

    rtas_event_log_queue(spapr, entry);

    qemu_irq_pulse(spapr_qirq(spapr,
                   rtas_event_log_to_irq(spapr, RTAS_LOG_TYPE_EPOW)));
}

static void spapr_hotplug_req_event(uint8_t hp_id, uint8_t hp_action,
                                    SpaprDrcType drc_type,
                                    union drc_identifier *drc_id)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    SpaprEventLogEntry *entry;
    struct hp_extended_log *new_hp;
    struct rtas_event_log_v6 *v6hdr;
    struct rtas_event_log_v6_maina *maina;
    struct rtas_event_log_v6_mainb *mainb;
    struct rtas_event_log_v6_hp *hp;

    entry = g_new(SpaprEventLogEntry, 1);
    new_hp = g_new0(struct hp_extended_log, 1);
    entry->extended_log = new_hp;

    v6hdr = &new_hp->v6hdr;
    maina = &new_hp->maina;
    mainb = &new_hp->mainb;
    hp = &new_hp->hp;

    entry->summary = RTAS_LOG_VERSION_6
        | RTAS_LOG_SEVERITY_EVENT
        | RTAS_LOG_DISPOSITION_NOT_RECOVERED
        | RTAS_LOG_OPTIONAL_PART_PRESENT
        | RTAS_LOG_INITIATOR_HOTPLUG
        | RTAS_LOG_TYPE_HOTPLUG;
    entry->extended_length = sizeof(*new_hp);

    spapr_init_v6hdr(v6hdr);
    spapr_init_maina(spapr, maina, 3 /* Main-A, Main-B, HP */);

    mainb->hdr.section_id = cpu_to_be16(RTAS_LOG_V6_SECTION_ID_MAINB);
    mainb->hdr.section_length = cpu_to_be16(sizeof(*mainb));
    mainb->subsystem_id = 0x80; /* External environment */
    mainb->event_severity = 0x00; /* Informational / non-error */
    mainb->event_subtype = 0x00; /* Normal shutdown */

    hp->hdr.section_id = cpu_to_be16(RTAS_LOG_V6_SECTION_ID_HOTPLUG);
    hp->hdr.section_length = cpu_to_be16(sizeof(*hp));
    hp->hdr.section_version = 1; /* includes extended modifier */
    hp->hotplug_action = hp_action;
    hp->hotplug_identifier = hp_id;

    switch (drc_type) {
    case SPAPR_DR_CONNECTOR_TYPE_PCI:
        hp->hotplug_type = RTAS_LOG_V6_HP_TYPE_PCI;
        break;
    case SPAPR_DR_CONNECTOR_TYPE_LMB:
        hp->hotplug_type = RTAS_LOG_V6_HP_TYPE_MEMORY;
        break;
    case SPAPR_DR_CONNECTOR_TYPE_CPU:
        hp->hotplug_type = RTAS_LOG_V6_HP_TYPE_CPU;
        break;
    case SPAPR_DR_CONNECTOR_TYPE_PHB:
        hp->hotplug_type = RTAS_LOG_V6_HP_TYPE_PHB;
        break;
    case SPAPR_DR_CONNECTOR_TYPE_PMEM:
        hp->hotplug_type = RTAS_LOG_V6_HP_TYPE_PMEM;
        break;
    default:
        /* we shouldn't be signaling hotplug events for resources
         * that don't support them
         */
        g_assert(false);
        return;
    }

    if (hp_id == RTAS_LOG_V6_HP_ID_DRC_COUNT) {
        hp->drc_id.count = cpu_to_be32(drc_id->count);
    } else if (hp_id == RTAS_LOG_V6_HP_ID_DRC_INDEX) {
        hp->drc_id.index = cpu_to_be32(drc_id->index);
    } else if (hp_id == RTAS_LOG_V6_HP_ID_DRC_COUNT_INDEXED) {
        /* we should not be using count_indexed value unless the guest
         * supports dedicated hotplug event source
         */
        g_assert(spapr_memory_hot_unplug_supported(spapr));
        hp->drc_id.count_indexed.count =
            cpu_to_be32(drc_id->count_indexed.count);
        hp->drc_id.count_indexed.index =
            cpu_to_be32(drc_id->count_indexed.index);
    }

    rtas_event_log_queue(spapr, entry);

    qemu_irq_pulse(spapr_qirq(spapr,
                   rtas_event_log_to_irq(spapr, RTAS_LOG_TYPE_HOTPLUG)));
}

void spapr_hotplug_req_add_by_index(SpaprDrc *drc)
{
    SpaprDrcType drc_type = spapr_drc_type(drc);
    union drc_identifier drc_id;

    drc_id.index = spapr_drc_index(drc);
    spapr_hotplug_req_event(RTAS_LOG_V6_HP_ID_DRC_INDEX,
                            RTAS_LOG_V6_HP_ACTION_ADD, drc_type, &drc_id);
}

void spapr_hotplug_req_remove_by_index(SpaprDrc *drc)
{
    SpaprDrcType drc_type = spapr_drc_type(drc);
    union drc_identifier drc_id;

    drc_id.index = spapr_drc_index(drc);
    spapr_hotplug_req_event(RTAS_LOG_V6_HP_ID_DRC_INDEX,
                            RTAS_LOG_V6_HP_ACTION_REMOVE, drc_type, &drc_id);
}

void spapr_hotplug_req_add_by_count(SpaprDrcType drc_type,
                                       uint32_t count)
{
    union drc_identifier drc_id;

    drc_id.count = count;
    spapr_hotplug_req_event(RTAS_LOG_V6_HP_ID_DRC_COUNT,
                            RTAS_LOG_V6_HP_ACTION_ADD, drc_type, &drc_id);
}

void spapr_hotplug_req_remove_by_count(SpaprDrcType drc_type,
                                          uint32_t count)
{
    union drc_identifier drc_id;

    drc_id.count = count;
    spapr_hotplug_req_event(RTAS_LOG_V6_HP_ID_DRC_COUNT,
                            RTAS_LOG_V6_HP_ACTION_REMOVE, drc_type, &drc_id);
}

void spapr_hotplug_req_add_by_count_indexed(SpaprDrcType drc_type,
                                            uint32_t count, uint32_t index)
{
    union drc_identifier drc_id;

    drc_id.count_indexed.count = count;
    drc_id.count_indexed.index = index;
    spapr_hotplug_req_event(RTAS_LOG_V6_HP_ID_DRC_COUNT_INDEXED,
                            RTAS_LOG_V6_HP_ACTION_ADD, drc_type, &drc_id);
}

void spapr_hotplug_req_remove_by_count_indexed(SpaprDrcType drc_type,
                                               uint32_t count, uint32_t index)
{
    union drc_identifier drc_id;

    drc_id.count_indexed.count = count;
    drc_id.count_indexed.index = index;
    spapr_hotplug_req_event(RTAS_LOG_V6_HP_ID_DRC_COUNT_INDEXED,
                            RTAS_LOG_V6_HP_ACTION_REMOVE, drc_type, &drc_id);
}

static void spapr_mc_set_ea_provided_flag(struct mc_extended_log *ext_elog)
{
    switch (ext_elog->mc.error_type) {
    case RTAS_LOG_V6_MC_TYPE_UE:
        ext_elog->mc.sub_err_type |= RTAS_LOG_V6_MC_UE_EA_ADDR_PROVIDED;
        break;
    case RTAS_LOG_V6_MC_TYPE_SLB:
    case RTAS_LOG_V6_MC_TYPE_ERAT:
    case RTAS_LOG_V6_MC_TYPE_TLB:
        ext_elog->mc.sub_err_type |= RTAS_LOG_V6_MC_EA_ADDR_PROVIDED;
        break;
    default:
        break;
    }
}

static uint32_t spapr_mce_get_elog_type(PowerPCCPU *cpu, bool recovered,
                                        struct mc_extended_log *ext_elog)
{
    int i;
    CPUPPCState *env = &cpu->env;
    uint32_t summary;
    uint64_t dsisr = env->spr[SPR_DSISR];

    summary = RTAS_LOG_VERSION_6 | RTAS_LOG_OPTIONAL_PART_PRESENT;
    if (recovered) {
        summary |= RTAS_LOG_DISPOSITION_FULLY_RECOVERED;
    } else {
        summary |= RTAS_LOG_DISPOSITION_NOT_RECOVERED;
    }

    if (SRR1_MC_LOADSTORE(env->spr[SPR_SRR1])) {
        for (i = 0; i < ARRAY_SIZE(mc_derror_table); i++) {
            if (!(dsisr & mc_derror_table[i].dsisr_value)) {
                continue;
            }

            ext_elog->mc.error_type = mc_derror_table[i].error_type;
            ext_elog->mc.sub_err_type = mc_derror_table[i].error_subtype;
            if (mc_derror_table[i].dar_valid) {
                ext_elog->mc.effective_address = cpu_to_be64(env->spr[SPR_DAR]);
                spapr_mc_set_ea_provided_flag(ext_elog);
            }

            summary |= mc_derror_table[i].initiator
                        | mc_derror_table[i].severity;

            return summary;
        }
    } else {
        for (i = 0; i < ARRAY_SIZE(mc_ierror_table); i++) {
            if ((env->spr[SPR_SRR1] & mc_ierror_table[i].srr1_mask) !=
                    mc_ierror_table[i].srr1_value) {
                continue;
            }

            ext_elog->mc.error_type = mc_ierror_table[i].error_type;
            ext_elog->mc.sub_err_type = mc_ierror_table[i].error_subtype;
            if (mc_ierror_table[i].nip_valid) {
                ext_elog->mc.effective_address = cpu_to_be64(env->nip);
                spapr_mc_set_ea_provided_flag(ext_elog);
            }

            summary |= mc_ierror_table[i].initiator
                        | mc_ierror_table[i].severity;

            return summary;
        }
    }

    summary |= RTAS_LOG_INITIATOR_CPU;
    return summary;
}

static void spapr_mce_dispatch_elog(SpaprMachineState *spapr, PowerPCCPU *cpu,
                                    bool recovered)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    uint64_t rtas_addr;
    struct rtas_error_log log;
    struct mc_extended_log *ext_elog;
    uint32_t summary;

    ext_elog = g_malloc0(sizeof(*ext_elog));
    summary = spapr_mce_get_elog_type(cpu, recovered, ext_elog);

    log.summary = cpu_to_be32(summary);
    log.extended_length = cpu_to_be32(sizeof(*ext_elog));

    spapr_init_v6hdr(&ext_elog->v6hdr);
    ext_elog->mc.hdr.section_id = cpu_to_be16(RTAS_LOG_V6_SECTION_ID_MC);
    ext_elog->mc.hdr.section_length =
                    cpu_to_be16(sizeof(struct rtas_event_log_v6_mc));
    ext_elog->mc.hdr.section_version = 1;

    /* get rtas addr from fdt */
    rtas_addr = spapr_get_rtas_addr();
    if (!rtas_addr) {
        if (!recovered) {
            error_report(
"FWNMI: Unable to deliver machine check to guest: rtas_addr not found.");
            qemu_system_guest_panicked(NULL);
        } else {
            warn_report(
"FWNMI: Unable to deliver machine check to guest: rtas_addr not found. "
"Machine check recovered.");
        }
        g_free(ext_elog);
        return;
    }

    /*
     * By taking the interlock, we assume that the MCE will be
     * delivered to the guest. CAUTION: don't add anything that could
     * prevent the MCE to be delivered after this line, otherwise the
     * guest won't be able to release the interlock and ultimately
     * hang/crash?
     */
    spapr->fwnmi_machine_check_interlock = cpu->vcpu_id;

    stq_be_phys(&address_space_memory, rtas_addr + RTAS_ERROR_LOG_OFFSET,
                env->gpr[3]);
    cpu_physical_memory_write(rtas_addr + RTAS_ERROR_LOG_OFFSET +
                              sizeof(env->gpr[3]), &log, sizeof(log));
    cpu_physical_memory_write(rtas_addr + RTAS_ERROR_LOG_OFFSET +
                              sizeof(env->gpr[3]) + sizeof(log), ext_elog,
                              sizeof(*ext_elog));
    g_free(ext_elog);

    env->gpr[3] = rtas_addr + RTAS_ERROR_LOG_OFFSET;

    ppc_cpu_do_fwnmi_machine_check(cs, spapr->fwnmi_machine_check_addr);
}

void spapr_mce_req_event(PowerPCCPU *cpu, bool recovered)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    CPUState *cs = CPU(cpu);
    int ret;

    if (spapr->fwnmi_machine_check_addr == -1) {
        /* Non-FWNMI case, deliver it like an architected CPU interrupt. */
        cs->exception_index = POWERPC_EXCP_MCHECK;
        ppc_cpu_do_interrupt(cs);
        return;
    }

    /* Wait for FWNMI interlock. */
    while (spapr->fwnmi_machine_check_interlock != -1) {
        /*
         * Check whether the same CPU got machine check error
         * while still handling the mc error (i.e., before
         * that CPU called "ibm,nmi-interlock")
         */
        if (spapr->fwnmi_machine_check_interlock == cpu->vcpu_id) {
            if (!recovered) {
                error_report(
"FWNMI: Unable to deliver machine check to guest: nested machine check.");
                qemu_system_guest_panicked(NULL);
            } else {
                warn_report(
"FWNMI: Unable to deliver machine check to guest: nested machine check. "
"Machine check recovered.");
            }
            return;
        }
        qemu_cond_wait_bql(&spapr->fwnmi_machine_check_interlock_cond);
        if (spapr->fwnmi_machine_check_addr == -1) {
            /*
             * If the machine was reset while waiting for the interlock,
             * abort the delivery. The machine check applies to a context
             * that no longer exists, so it wouldn't make sense to deliver
             * it now.
             */
            return;
        }
    }

    /*
     * Try to block migration while FWNMI is being handled, so the
     * machine check handler runs where the information passed to it
     * actually makes sense.  This shouldn't actually block migration,
     * only delay it slightly, assuming migration is retried.  If the
     * attempt to block fails, carry on.  Unfortunately, it always
     * fails when running with -only-migrate.  A proper interface to
     * delay migration completion for a bit could avoid that.
     */
    error_setg(&spapr->fwnmi_migration_blocker,
        "A machine check is being handled during migration. The handler"
        "may run and log hardware error on the destination");

    ret = migrate_add_blocker(&spapr->fwnmi_migration_blocker, NULL);
    if (ret == -EBUSY) {
        warn_report("Received a fwnmi while migration was in progress");
    }

    spapr_mce_dispatch_elog(spapr, cpu, recovered);
}

static void check_exception(PowerPCCPU *cpu, SpaprMachineState *spapr,
                            uint32_t token, uint32_t nargs,
                            target_ulong args,
                            uint32_t nret, target_ulong rets)
{
    uint32_t mask, buf, len, event_len;
    SpaprEventLogEntry *event;
    struct rtas_error_log header;
    int i;

    if ((nargs < 6) || (nargs > 7) || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    mask = rtas_ld(args, 2);
    buf = rtas_ld(args, 4);
    len = rtas_ld(args, 5);

    event = rtas_event_log_dequeue(spapr, mask);
    if (!event) {
        goto out_no_events;
    }

    event_len = event->extended_length + sizeof(header);

    if (event_len < len) {
        len = event_len;
    }

    header.summary = cpu_to_be32(event->summary);
    header.extended_length = cpu_to_be32(event->extended_length);
    cpu_physical_memory_write(buf, &header, sizeof(header));
    cpu_physical_memory_write(buf + sizeof(header), event->extended_log,
                              event->extended_length);
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    g_free(event->extended_log);
    g_free(event);

    /* according to PAPR+, the IRQ must be left asserted, or re-asserted, if
     * there are still pending events to be fetched via check-exception. We
     * do the latter here, since our code relies on edge-triggered
     * interrupts.
     */
    for (i = 0; i < EVENT_CLASS_MAX; i++) {
        if (rtas_event_log_contains(spapr, EVENT_CLASS_MASK(i))) {
            const SpaprEventSource *source =
                spapr_event_sources_get_source(spapr->event_sources, i);

            g_assert(source->enabled);
            qemu_irq_pulse(spapr_qirq(spapr, source->irq));
        }
    }

    return;

out_no_events:
    rtas_st(rets, 0, RTAS_OUT_NO_ERRORS_FOUND);
}

static void event_scan(PowerPCCPU *cpu, SpaprMachineState *spapr,
                       uint32_t token, uint32_t nargs,
                       target_ulong args,
                       uint32_t nret, target_ulong rets)
{
    int i;
    if (nargs != 4 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    for (i = 0; i < EVENT_CLASS_MAX; i++) {
        if (rtas_event_log_contains(spapr, EVENT_CLASS_MASK(i))) {
            const SpaprEventSource *source =
                spapr_event_sources_get_source(spapr->event_sources, i);

            g_assert(source->enabled);
            qemu_irq_pulse(spapr_qirq(spapr, source->irq));
        }
    }

    rtas_st(rets, 0, RTAS_OUT_NO_ERRORS_FOUND);
}

void spapr_clear_pending_events(SpaprMachineState *spapr)
{
    SpaprEventLogEntry *entry = NULL, *next_entry;

    QTAILQ_FOREACH_SAFE(entry, &spapr->pending_events, next, next_entry) {
        QTAILQ_REMOVE(&spapr->pending_events, entry, next);
        g_free(entry->extended_log);
        g_free(entry);
    }
}

void spapr_clear_pending_hotplug_events(SpaprMachineState *spapr)
{
    SpaprEventLogEntry *entry = NULL, *next_entry;

    QTAILQ_FOREACH_SAFE(entry, &spapr->pending_events, next, next_entry) {
        if (spapr_event_log_entry_type(entry) == RTAS_LOG_TYPE_HOTPLUG) {
            QTAILQ_REMOVE(&spapr->pending_events, entry, next);
            g_free(entry->extended_log);
            g_free(entry);
        }
    }
}

void spapr_events_init(SpaprMachineState *spapr)
{
    int epow_irq = SPAPR_IRQ_EPOW;

    if (SPAPR_MACHINE_GET_CLASS(spapr)->legacy_irq_allocation) {
        epow_irq = spapr_irq_findone(spapr, &error_fatal);
    }

    spapr_irq_claim(spapr, epow_irq, false, &error_fatal);

    QTAILQ_INIT(&spapr->pending_events);

    spapr->event_sources = spapr_event_sources_new();

    spapr_event_sources_register(spapr->event_sources, EVENT_CLASS_EPOW,
                                 epow_irq);

    /* NOTE: if machine supports modern/dedicated hotplug event source,
     * we add it to the device-tree unconditionally. This means we may
     * have cases where the source is enabled in QEMU, but unused by the
     * guest because it does not support modern hotplug events, so we
     * take care to rely on checking for negotiation of OV5_HP_EVT option
     * before attempting to use it to signal events, rather than simply
     * checking that it's enabled.
     */
    if (spapr->use_hotplug_event_source) {
        int hp_irq = SPAPR_IRQ_HOTPLUG;

        if (SPAPR_MACHINE_GET_CLASS(spapr)->legacy_irq_allocation) {
            hp_irq = spapr_irq_findone(spapr, &error_fatal);
        }

        spapr_irq_claim(spapr, hp_irq, false, &error_fatal);

        spapr_event_sources_register(spapr->event_sources, EVENT_CLASS_HOT_PLUG,
                                     hp_irq);
    }

    spapr->epow_notifier.notify = spapr_powerdown_req;
    qemu_register_powerdown_notifier(&spapr->epow_notifier);
    spapr_rtas_register(RTAS_CHECK_EXCEPTION, "check-exception",
                        check_exception);
    spapr_rtas_register(RTAS_EVENT_SCAN, "event-scan", event_scan);
}
