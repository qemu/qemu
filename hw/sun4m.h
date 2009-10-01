#ifndef SUN4M_H
#define SUN4M_H

#include "qemu-common.h"

/* Devices used by sparc32 system.  */

/* iommu.c */
void sparc_iommu_memory_rw(void *opaque, target_phys_addr_t addr,
                                 uint8_t *buf, int len, int is_write);
static inline void sparc_iommu_memory_read(void *opaque,
                                           target_phys_addr_t addr,
                                           uint8_t *buf, int len)
{
    sparc_iommu_memory_rw(opaque, addr, buf, len, 0);
}

static inline void sparc_iommu_memory_write(void *opaque,
                                            target_phys_addr_t addr,
                                            uint8_t *buf, int len)
{
    sparc_iommu_memory_rw(opaque, addr, buf, len, 1);
}

/* slavio_intctl.c */
void slavio_pic_info(Monitor *mon, DeviceState *dev);
void slavio_irq_info(Monitor *mon, DeviceState *dev);

/* sun4c_intctl.c */
void sun4c_pic_info(Monitor *mon, void *opaque);
void sun4c_irq_info(Monitor *mon, void *opaque);

/* sparc32_dma.c */
#include "sparc32_dma.h"

#endif
