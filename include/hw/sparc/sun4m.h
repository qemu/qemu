#ifndef SUN4M_H
#define SUN4M_H

#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "qapi/qmp/types.h"
#include "hw/sysbus.h"

/* Devices used by sparc32 system.  */

/* iommu.c */
#define TYPE_SUN4M_IOMMU "iommu"
#define SUN4M_IOMMU(obj) OBJECT_CHECK(IOMMUState, (obj), TYPE_SUN4M_IOMMU)

#define IOMMU_NREGS         (4 * 4096 / 4)

typedef struct IOMMUState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t regs[IOMMU_NREGS];
    hwaddr iostart;
    qemu_irq irq;
    uint32_t version;
} IOMMUState;

void sparc_iommu_memory_rw(void *opaque, hwaddr addr,
                                 uint8_t *buf, int len, int is_write);
static inline void sparc_iommu_memory_read(void *opaque,
                                           hwaddr addr,
                                           uint8_t *buf, int len)
{
    sparc_iommu_memory_rw(opaque, addr, buf, len, 0);
}

static inline void sparc_iommu_memory_write(void *opaque,
                                            hwaddr addr,
                                            uint8_t *buf, int len)
{
    sparc_iommu_memory_rw(opaque, addr, buf, len, 1);
}

/* sparc32_dma.c */
#include "hw/sparc/sparc32_dma.h"

#endif
