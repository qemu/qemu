/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2015 Imagination Technologies
 *
 */

#ifndef _MIPS_GCR_H
#define _MIPS_GCR_H

#define TYPE_MIPS_GCR "mips-gcr"
#define MIPS_GCR(obj) OBJECT_CHECK(MIPSGCRState, (obj), TYPE_MIPS_GCR)

#define GCR_BASE_ADDR           0x1fbf8000ULL
#define GCR_ADDRSPACE_SZ        0x8000

/* Offsets to register blocks */
#define MIPS_GCB_OFS        0x0000 /* Global Control Block */
#define MIPS_CLCB_OFS       0x2000 /* Core Local Control Block */
#define MIPS_COCB_OFS       0x4000 /* Core Other Control Block */
#define MIPS_GDB_OFS        0x6000 /* Global Debug Block */

/* Global Control Block Register Map */
#define GCR_CONFIG_OFS      0x0000
#define GCR_BASE_OFS        0x0008
#define GCR_REV_OFS         0x0030
#define GCR_CPC_BASE_OFS    0x0088
#define GCR_CPC_STATUS_OFS  0x00F0
#define GCR_L2_CONFIG_OFS   0x0130

/* Core Local and Core Other Block Register Map */
#define GCR_CL_CONFIG_OFS   0x0010
#define GCR_CL_OTHER_OFS    0x0018

/* GCR_L2_CONFIG register fields */
#define GCR_L2_CONFIG_BYPASS_SHF    20
#define GCR_L2_CONFIG_BYPASS_MSK    ((0x1ULL) << GCR_L2_CONFIG_BYPASS_SHF)

/* GCR_CPC_BASE register fields */
#define GCR_CPC_BASE_CPCEN_MSK   1
#define GCR_CPC_BASE_CPCBASE_MSK 0xFFFFFFFF8000ULL
#define GCR_CPC_BASE_MSK (GCR_CPC_BASE_CPCEN_MSK | GCR_CPC_BASE_CPCBASE_MSK)

typedef struct MIPSGCRState MIPSGCRState;
struct MIPSGCRState {
    SysBusDevice parent_obj;

    int32_t gcr_rev;
    int32_t num_vps;
    hwaddr gcr_base;
    MemoryRegion iomem;
    MemoryRegion *cpc_mr;

    uint64_t cpc_base;
};

#endif /* _MIPS_GCR_H */
