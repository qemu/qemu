/*
 * QEMU sPAPR PCI host for VFIO
 *
 * Copyright (c) 2011-2014 Alexey Kardashevskiy, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/ppc/spapr.h"
#include "hw/pci-host/spapr.h"
#include "linux/vfio.h"
#include "hw/misc/vfio.h"

static Property spapr_phb_vfio_properties[] = {
    DEFINE_PROP_INT32("iommu", sPAPRPHBVFIOState, iommugroupid, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_phb_vfio_finish_realize(sPAPRPHBState *sphb, Error **errp)
{
    sPAPRPHBVFIOState *svphb = SPAPR_PCI_VFIO_HOST_BRIDGE(sphb);
    struct vfio_iommu_spapr_tce_info info = { .argsz = sizeof(info) };
    int ret;
    sPAPRTCETable *tcet;
    uint32_t liobn = svphb->phb.dma_liobn;

    if (svphb->iommugroupid == -1) {
        error_setg(errp, "Wrong IOMMU group ID %d", svphb->iommugroupid);
        return;
    }

    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_CHECK_EXTENSION,
                               (void *) VFIO_SPAPR_TCE_IOMMU);
    if (ret != 1) {
        error_setg_errno(errp, -ret,
                         "spapr-vfio: SPAPR extension is not supported");
        return;
    }

    ret = vfio_container_ioctl(&svphb->phb.iommu_as, svphb->iommugroupid,
                               VFIO_IOMMU_SPAPR_TCE_GET_INFO, &info);
    if (ret) {
        error_setg_errno(errp, -ret,
                         "spapr-vfio: get info from container failed");
        return;
    }

    tcet = spapr_tce_new_table(DEVICE(sphb), liobn, info.dma32_window_start,
                               SPAPR_TCE_PAGE_SHIFT,
                               info.dma32_window_size >> SPAPR_TCE_PAGE_SHIFT,
                               true);
    if (!tcet) {
        error_setg(errp, "spapr-vfio: failed to create VFIO TCE table");
        return;
    }

    /* Register default 32bit DMA window */
    memory_region_add_subregion(&sphb->iommu_root, tcet->bus_offset,
                                spapr_tce_get_iommu(tcet));
}

static void spapr_phb_vfio_reset(DeviceState *qdev)
{
    /* Do nothing */
}

static void spapr_phb_vfio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    sPAPRPHBClass *spc = SPAPR_PCI_HOST_BRIDGE_CLASS(klass);

    dc->props = spapr_phb_vfio_properties;
    dc->reset = spapr_phb_vfio_reset;
    spc->finish_realize = spapr_phb_vfio_finish_realize;
}

static const TypeInfo spapr_phb_vfio_info = {
    .name          = TYPE_SPAPR_PCI_VFIO_HOST_BRIDGE,
    .parent        = TYPE_SPAPR_PCI_HOST_BRIDGE,
    .instance_size = sizeof(sPAPRPHBVFIOState),
    .class_init    = spapr_phb_vfio_class_init,
    .class_size    = sizeof(sPAPRPHBClass),
};

static void spapr_pci_vfio_register_types(void)
{
    type_register_static(&spapr_phb_vfio_info);
}

type_init(spapr_pci_vfio_register_types)
