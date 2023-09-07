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

#ifndef _SSR_FPGA_H_
#define _SSR_FPGA_H_

#include "hw/qdev-core.h"
#include "hw/sysbus.h"


#define TYPE_SSR_FPGA_PNP "ssr_fpga"

/*******************************************************************************
* Function:
*
* Description:
*
* Return:
*
*******************************************************************************/
static inline
DeviceState *ssr_fpga_create(hwaddr pci_base)
{
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_SSR_FPGA_PNP);

    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, pci_base);
 
    return dev;
}

#endif /* ! _SSRFPGA_H_ */
