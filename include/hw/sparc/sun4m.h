#ifndef SUN4M_H
#define SUN4M_H

#include "qemu-common.h"
#include "exec/hwaddr.h"
#include "qapi/qmp/types.h"

/* Devices used by sparc32 system.  */

/* iommu.c */
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

/* slavio_intctl.c */
void slavio_pic_info(Monitor *mon, DeviceState *dev);
void slavio_irq_info(Monitor *mon, DeviceState *dev);

/* sun4m.c */
void sun4m_hmp_info_pic(Monitor *mon, const QDict *qdict);
void sun4m_hmp_info_irq(Monitor *mon, const QDict *qdict);

/* sparc32_dma.c */
#include "hw/sparc/sparc32_dma.h"

#endif
