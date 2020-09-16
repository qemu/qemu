/*
 * sPAPR CPU core device.
 *
 * Copyright (C) 2016 Bharata B Rao <bharata@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_SPAPR_CPU_CORE_H
#define HW_SPAPR_CPU_CORE_H

#include "hw/cpu/core.h"
#include "hw/qdev-core.h"
#include "target/ppc/cpu-qom.h"
#include "target/ppc/cpu.h"
#include "qom/object.h"

#define TYPE_SPAPR_CPU_CORE "spapr-cpu-core"
OBJECT_DECLARE_TYPE(SpaprCpuCore, SpaprCpuCoreClass,
                    SPAPR_CPU_CORE)

#define SPAPR_CPU_CORE_TYPE_NAME(model) model "-" TYPE_SPAPR_CPU_CORE

struct SpaprCpuCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    PowerPCCPU **threads;
    int node_id;
    bool pre_3_0_migration; /* older machine don't know about SpaprCpuState */
};

struct SpaprCpuCoreClass {
    DeviceClass parent_class;
    const char *cpu_type;
};

const char *spapr_get_cpu_core_type(const char *cpu_type);
void spapr_cpu_set_entry_state(PowerPCCPU *cpu, target_ulong nip,
                               target_ulong r1, target_ulong r3,
                               target_ulong r4);

typedef struct SpaprCpuState {
    uint64_t vpa_addr;
    uint64_t slb_shadow_addr, slb_shadow_size;
    uint64_t dtl_addr, dtl_size;
    bool prod; /* not migrated, only used to improve dispatch latencies */
    struct ICPState *icp;
    struct XiveTCTX *tctx;
} SpaprCpuState;

static inline SpaprCpuState *spapr_cpu_state(PowerPCCPU *cpu)
{
    return (SpaprCpuState *)cpu->machine_data;
}

#endif
