/***********************************************************************************
 * Copyright (c) 2017, Odyssey Space Research, L.L.C.
 *   Software developed under contract for University of Colorado Boulder
 *   Laboratory for Atmospheric and Space Physics (LASP)
 *   under contract number 148576.
 *
 *   This software is jointly owned by Odyssey Space Research, L.L.C. and
 *   the University of Colorado Boulder, LASP.  All rights reserved.
 *   This software may not be released or licensed for open source use,
 *   in whole or in part, without permission from Odyssey Space Research, L.L.C.
 *
 *   Corporate Contact: info@odysseysr.com (281) 488-7953
 *
 * Notice:
 *   This source code constitutes technology controlled by the U.S. Export
 *   Administration Regulations, 15 C.F.R. Parts 730-774 (EAR).  Transfer,
 *   disclosure, or export to foreign persons without prior U.S. Government
 *   approval may be prohibited.  Violations of these export laws and
 *   regulations are subject to severe civil and criminal penalties.
 ************************************************************************************/

/* 
    EMA Specific Addition
    This is mostly taken from EMM, but slight modifications made for EMA
*/

#include "hw/sysbus.h"
#include "hw/sparc/qemu_ssr_fpga_cpu_interface.h"
#include "ssr_fpga.h"


/*******************************************************************************
* Description:
*
*
*******************************************************************************/
#define SSR_FPGA_PNP(obj) \
    OBJECT_CHECK(SSR_FPGA, (obj), TYPE_SSR_FPGA_PNP)

/*******************************************************************************
* Description:
*
*
*******************************************************************************/
typedef struct SSR_FPGA {
    SysBusDevice parent_obj;
    MemoryRegion ssr_iomem;
} SSR_FPGA;

/*******************************************************************************
* Function:
*
* Description:
*
* Return:
*
*******************************************************************************/
static void ssr_fpga_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SSR_FPGA *pnp = SSR_FPGA_PNP(obj);

    qemu_cpu_ssr_init();

    /* memory_region_init_io(&pnp->ssr_iomem, OBJECT(pnp), &srr_fpga_ops, pnp,
                          "ssrpnp", QEMU_SSR_MEMORY_SIZE); */
    sysbus_init_mmio(sbd, &pnp->ssr_iomem);
}

/*******************************************************************************
* Description:
*
*
*******************************************************************************/
static const TypeInfo ssr_fpga_info = {
    .name          = TYPE_SSR_FPGA_PNP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SSR_FPGA),
    .instance_init = ssr_fpga_init,
};

/*******************************************************************************
* Function:
*
* Description:
*
* Return:
*
*******************************************************************************/
static void ssr_fpga_register_types(void)
{
      type_register_static(&ssr_fpga_info);
}

/*******************************************************************************
* Description:
*
*
*******************************************************************************/
type_init(ssr_fpga_register_types)