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
#include <stdint.h>


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
static uint64_t srr_fpga_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    uint64_t read_data = 0;
    uint32_t reg = 0;

    // qemu_cpu_ssr_read(addr,&reg);
    printf("read addr is %lu\n", addr);
    printf("read reg is %u\n", reg)
    read_data = (uint64_t) reg;
    return read_data;
}

/*******************************************************************************
* Function:
*
* Description:
*
* Return:
*
*******************************************************************************/
static void srr_fpga_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{

 uint32_t reg = (uint32_t) value;
 // qemu_cpu_ssr_write(addr,reg);
 printf("write addr is %lu\n", addr);
 printf("write reg is %u\n", reg);
}

/*******************************************************************************
* Description:
*
*
*******************************************************************************/
static const MemoryRegionOps srr_fpga_ops = {
    .read       = srr_fpga_read,
    .write      = srr_fpga_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

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

    memory_region_init_io(&pnp->ssr_iomem, OBJECT(pnp), &srr_fpga_ops, pnp,
                          "ssrpnp", QEMU_SSR_MEMORY_SIZE);
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