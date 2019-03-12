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

#include "hw/qdev.h"
#include "hw/cpu/core.h"
#include "target/ppc/cpu-qom.h"
#include "target/ppc/cpu.h"

#define TYPE_SPAPR_CPU_CORE "spapr-cpu-core"
#define SPAPR_CPU_CORE(obj) \
    OBJECT_CHECK(SpaprCpuCore, (obj), TYPE_SPAPR_CPU_CORE)
#define SPAPR_CPU_CORE_CLASS(klass) \
    OBJECT_CLASS_CHECK(SpaprCpuCoreClass, (klass), TYPE_SPAPR_CPU_CORE)
#define SPAPR_CPU_CORE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SpaprCpuCoreClass, (obj), TYPE_SPAPR_CPU_CORE)

#define SPAPR_CPU_CORE_TYPE_NAME(model) model "-" TYPE_SPAPR_CPU_CORE

typedef struct SpaprCpuCore {
    /*< private >*/
    CPUCore parent_obj;

    /*< public >*/
    PowerPCCPU **threads;
    int node_id;
    bool pre_3_0_migration; /* older machine don't know about SpaprCpuState */
} SpaprCpuCore;

typedef struct SpaprCpuCoreClass {
    DeviceClass parent_class;
    const char *cpu_type;
} SpaprCpuCoreClass;

const char *spapr_get_cpu_core_type(const char *cpu_type);
void spapr_cpu_set_entry_state(PowerPCCPU *cpu, target_ulong nip, target_ulong r3);

typedef struct SpaprCpuState {
    uint64_t vpa_addr;
    uint64_t slb_shadow_addr, slb_shadow_size;
    uint64_t dtl_addr, dtl_size;
    struct ICPState *icp;
    struct XiveTCTX *tctx;
} SpaprCpuState;

static inline SpaprCpuState *spapr_cpu_state(PowerPCCPU *cpu)
{
    return (SpaprCpuState *)cpu->machine_data;
}

#endif
