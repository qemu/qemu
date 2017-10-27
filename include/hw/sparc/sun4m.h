#ifndef SUN4M_H
#define SUN4M_H

#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "qapi/qmp/types.h"
#include "hw/sysbus.h"

/* Devices used by sparc32 system.  */

/* iommu.c */
#define TYPE_SUN4M_IOMMU "sun4m-iommu"
#define SUN4M_IOMMU(obj) OBJECT_CHECK(IOMMUState, (obj), TYPE_SUN4M_IOMMU)

#define TYPE_SUN4M_IOMMU_MEMORY_REGION "sun4m-iommu-memory-region"

#define IOMMU_NREGS         (4 * 4096 / 4)

typedef struct IOMMUState {
    SysBusDevice parent_obj;

    AddressSpace iommu_as;
    IOMMUMemoryRegion iommu;

    MemoryRegion iomem;
    uint32_t regs[IOMMU_NREGS];
    hwaddr iostart;
    qemu_irq irq;
    uint32_t version;
} IOMMUState;

/* sparc32_dma.c */
#include "hw/sparc/sparc32_dma.h"

#endif
