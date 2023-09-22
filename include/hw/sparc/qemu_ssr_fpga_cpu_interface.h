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

#ifndef __QEMU_SSR_FPGA_CPU_INTERFACE_H__
#define __QEMU_SSR_FPGA_CPU_INTERFACE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define QEMU_SSR_NUM_REGS 109
#define QEMU_SSR_MEMORY_SIZE 0xFFFFF

#define QEMU_SSR_FPGA_MEMORY_NAME "SSR_FPGA_MEM"
#define QEMU_SSR_FPGA_MEMORY_SIZE sizeof(unsigned int)*QEMU_SSR_MEMORY_SIZE
#define QEMU_SSR_FPGA_MEMORY_REG_NAME "SSR_FPGA_MEM_REG"
#define QEMU_SSR_FPGA_MEMORY_REG_SIZE sizeof(struct qemu_ssr_reg_config)*QEMU_SSR_NUM_REGS
#define QEMU_SSR_FPGA_CPU_SYNC_NAME "SSR_FPGA_CPU_SYNC"
#define QEMU_SSR_FPGA_CPU_SYNC_SIZE sizeof(unsigned int)
#define QEMU_SSR_FPGA_MEMORY_SYNC_NAME "SSR_FPGA_MEM_SYNC"
#define QEMU_SSR_FPGA_MEMORY_SYNC_SIZE sizeof(unsigned int)
#define QEMU_SSR_FPGA_OPERATION_NAME "SSR_FPGA_OPERATION"
#define QEMU_SSR_FPGA_OPERATION_SIZE sizeof(unsigned int)
#define QEMU_SSR_FPGA_OPERATION_TYPE_NAME "SSR_FPGA_OPERATION_TYPE"
#define QEMU_SSR_FPGA_OPERATION_TYPE_SIZE sizeof(unsigned int)

/*******************************************************************************
* Structure: qemu_ssr_reg_config
*
* Description:
* Holds information to define register configuration
*
*
*******************************************************************************/

struct qemu_ssr_reg_config {
		unsigned int reg_offset;
		unsigned int num_words;
		unsigned int read_access;
		unsigned int write_access;
		unsigned int read_mask;
		unsigned int write_mask;
		unsigned int read_trigger;
		unsigned int write_trigger;
};

/*******************************************************************************
* Function: qemu_cpu_ssr_init
*
* Description:
* Initialize QEMU SSR Interface
*
* Return:
*  0 if read successful
* -1 if read not successful
*
*******************************************************************************/

int qemu_cpu_ssr_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __QEMU_SSR_FPGA_CPU_INTERFACE_H__ */