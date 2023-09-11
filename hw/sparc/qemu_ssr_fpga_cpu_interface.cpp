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

#include <stdlib.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>

#include "hw/sparc/qemu_ssr_fpga_cpu_interface.h"

//static QemuWorkerProcess *listener;
//static QemuMmioWriter *snorkel_adc_css1 = new QemuMmioWriter("LSB_css_packet");

static unsigned char * qemu_ssr_fpga_memory;
static struct qemu_ssr_reg_config * qemu_ssr_fpga_regs;
static unsigned int * qemu_ssr_fpga_cpu_sync;
static unsigned int * qemu_ssr_fpga_memory_sync;
static unsigned int * qemu_ssr_fpga_operaton;
static unsigned int * qemu_ssr_fpga_operaton_type;
static short qemu_ssr_fpga_memory_op[QEMU_SSR_MEMORY_SIZE];
static unsigned int qemu_ssr_cpu_init_status = 0;

static struct qemu_ssr_reg_config qemu_ssr_fpga_regs_data[QEMU_SSR_NUM_REGS] = {
    { 0x0, 1, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 },      /*version */
    { 0x4, 1, 1, 1, 0xffffffff, 0xffffffff,0x0,0x0 },      /*stratch */
    { 0xc, 1, 1, 1, 0x00000fff, 0x00000fff,0x0,0x0 },      /* loopback */
    { 0x10, 1, 1, 1, 0xffffffff, 0xc03fffff,0x0,0x0 },     /* ssr_interrupt_enable_reg */
    { 0x14, 1, 1, 1, 0xffffffff, 0xc03fffff,0x0,0x0 },     /* ssr_interrupt_status_reg */
    { 0x20, 1, 1, 1, 0x0fffffff, 0x0fffffff,0x0,0x0 },     /* ssr_discrete_input_interrupt_enable_reg */
    { 0x24, 1, 1, 1, 0x0fffffff, 0x0fffffff,0x0,0x0 },     /* ssr_discrete_input_interrupt_status_reg */
    { 0x28, 1, 0, 0, 0x0fffffff, 0x0fffffff,0x0,0x0 },     /* discrete_intput */
    { 0x2c, 1, 1, 0, 0x0000000f, 0x0000000f,0x0,0x0 },     /* discrete_output */
    { 0x100, 8, 1, 0, 0x00000fff, 0x00000fff,0x0,0x0 },    /* ssr_units_manager_regs */
    { 0x200, 8, 1, 0, 0x00000fff, 0x00000fff,0x0,0x0 },    /* adc_css1 */
    { 0x210, 8, 1, 0, 0x00000fff, 0x00000fff,0x0,0x0 },    /* adc_css2 */
    { 0x300, 8, 1, 0, 0x00000fff, 0x00000fff,0x0,0x0 },    /* adc_sm1 */
    { 0x310, 8, 1, 0, 0x00000fff, 0x00000fff,0x0,0x0 },    /* adc_sm2 */
    { 0x400, 1, 0, 0, 0xffffffff, 0xffffffff,0x0,0x0 },    /* ssr_reaction_wheel_regs */
    { 0x2000, 1, 1, 0, 0x0000000f, 0x0000000f,0x0,0x0 },   /* ipc_pci2up */
    { 0x2004, 1, 1, 0, 0x0000000f, 0x0000000f,0x0,0x0 },   /* ipc_up2pci */
    { 0x3000, 512, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* ssr_ipc_pci2up_write_ram */
    { 0x3800, 512, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* ssr_ipc_pci2up_read_ram */
    { 0x4008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_hk_pkt_tx_desc_0 */
    { 0x400c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_hk_pkt_tx_desc_1 */
    { 0x4010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* sbc_hk_pkt_tx_int_enable */
    { 0x4014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* sbc_hk_pkt_tx_int_status */
    { 0x4800, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_hk_pkt_tx_ram_0 */
    { 0x4c00, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_hk_pkt_tx_ram_1 */
    { 0x5008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_hk_pkt_rx_desc_0 */
    { 0x500c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_hk_pkt_rx_desc_1 */
    { 0x5010, 1, 1, 1, 0x00003f30, 0x00003f30,0x0,0x0 },   /* sbc_hk_pkt_rx_int_enable */
    { 0x5014, 1, 1, 1, 0x00003f3f, 0x00003f30,0x0,0x0 },   /* sbc_hk_pkt_rx_int_status */
    { 0x5800, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_hk_pkt_rx_ram_0 */
    { 0x5c00, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_hk_pkt_rx_ram_1 */
    { 0x6008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emirs_pkt_tx_desc_0 */
    { 0x600c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emirs_pkt_tx_desc_1 */
    { 0x6010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* emirs_pkt_tx_int_enable */
    { 0x6014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* emirs_pkt_tx_int_status */
    { 0x6800, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emirs_pkt_tx_ram_0 */
    { 0x6c00, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emirs_pkt_tx_ram_1 */
    { 0x7008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emirs_pkt_rx_desc_0 */
    { 0x700c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emirs_pkt_rx_desc_1 */
    { 0x7010, 1, 1, 1, 0x00003f30, 0x00003f30,0x0,0x0 },   /* emirs_pkt_rx_int_enable */
    { 0x7014, 1, 1, 1, 0x00003f3f, 0x00003f30,0x0,0x0 },   /* emirs_pkt_rx_int_status */
    { 0x7800, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emirs_pkt_rx_ram_0 */
    { 0x7c00, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emirs_pkt_rx_ram_1 */
    { 0x8008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emus_pkt_tx_desc_0 */
    { 0x800c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emus_pkt_tx_desc_1 */
    { 0x8010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* emus_pkt_tx_int_enable */
    { 0x8014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* emus_pkt_tx_int_status */
    { 0x8800, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emus_pkt_tx_ram_0 */
    { 0x8c00, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emus_pkt_tx_ram_1 */
    { 0x9008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emus_pkt_rx_desc_0 */
    { 0x900c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* emus_pkt_rx_desc_1 */
    { 0x9010, 1, 1, 1, 0x00003f30, 0x00003f30,0x0,0x0 },   /* emus_pkt_rx_int_enable */
    { 0x9014, 1, 1, 1, 0x00003f3f, 0x00003f30,0x0,0x0 },   /* emus_pkt_rx_int_status */
    { 0x9800, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emus_pkt_rx_ram_0 */
    { 0x9c00, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* emus_pkt_rx_ram_1 */
    { 0xa008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* exi_pkt_tx_desc_0 */
    { 0xa00c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* exi_pkt_tx_desc_1 */
    { 0xa010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* exi_pkt_tx_int_enable */
    { 0xa014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* exi_pkt_tx_int_status */
    { 0xa800, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* exi_pkt_tx_ram_0 */
    { 0xac00, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* exi_pkt_tx_ram_1 */
    { 0xb008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* exi_pkt_rx_desc_0 */
    { 0xb00c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* exi_pkt_rx_desc_1 */
    { 0xb010, 1, 1, 1, 0x00003f30, 0x00003f30,0x0,0x0 },   /* exi_pkt_rx_int_enable */
    { 0xb014, 1, 1, 1, 0x00003f3f, 0x00003f30,0x0,0x0 },   /* exi_pkt_rx_int_status */
    { 0xb800, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* exi_pkt_rx_ram_0 */
    { 0xbc00, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* exi_pkt_rx_ram_1 */
    { 0xc008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_rt_pkt_tx_desc_0 */
    { 0xc00c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_rt_pkt_tx_desc_1 */
    { 0xc010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* sbc_rt_pkt_tx_int_enable */
    { 0xc014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* sbc_rt_pkt_tx_int_status */
    { 0xc800, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_rt_pkt_tx_ram_0 */
    { 0xcc00, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_rt_pkt_tx_ram_1 */
    { 0xd008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_rt_pkt_rx_desc_0 */
    { 0xd00c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* sbc_rt_pkt_rx_desc_1 */
    { 0xd010, 1, 1, 1, 0x00003f30, 0x00003f30,0x0,0x0 },   /* sbc_rt_pkt_rx_int_enable */
    { 0xd014, 1, 1, 1, 0x00003f3f, 0x00003f30,0x0,0x0 },   /* sbc_rt_pkt_rx_int_status */
    { 0xd800, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_rt_pkt_rx_ram_0 */
    { 0xdc00, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* sbc_rt_pkt_rx_ram_1 */
    { 0xe008, 1, 1, 1, 0x000083ff, 0x000083ff,0x0,0x0 },   /* star_tracker_prime_pkt_tx_desc_0 */
    { 0xe00c, 1, 1, 1, 0x000083ff, 0x000083ff,0x0,0x0 },   /* star_tracker_prime_pkt_tx_desc_1 */
    { 0xe010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* star_tracker_prime_pkt_tx_int_enable */
    { 0xe014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* star_tracker_prime_tx_int_status */
    { 0xe400, 128, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* star_tracker_prime_pkt_tx_ram_0 */
    { 0xe600, 128, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* star_tracker_prime_pkt_tx_ram_1 */
    { 0xe800, 1, 1, 0, 0x0000001f, 0x0000001f,0x0,0x0 },   /* star_tracker_prime_rx_status */
    { 0xe804, 1, 1, 1, 0x0000000f, 0x0000000f,0x0,0x0 },   /* star_tracker_prime_rx_config */
    { 0xe808, 1, 1, 0, 0x0000000f, 0x0000000f,0x0,0x0 },   /* star_tracker_prime_rx_data */
    { 0xf008, 1, 1, 1, 0x000083ff, 0x000083ff,0x0,0x0 },   /* star_tracker_sec_pkt_tx_desc_0 */
    { 0xf00c, 1, 1, 1, 0x000083ff, 0x000083ff,0x0,0x0 },   /* star_tracker_sec_pkt_tx_desc_1 */
    { 0xf010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* star_tracker_sec_pkt_tx_int_enable */
    { 0xf014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* star_tracker_sec_tx_int_status */
    { 0xf400, 128, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* star_tracker_sec_pkt_tx_ram_0 */
    { 0xf600, 128, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* star_tracker_sec_pkt_tx_ram_1 */
    { 0xf800, 1, 1, 0, 0x0000001f, 0x0000001f,0x0,0x0 },   /* star_tracker_sec_rx_status */
    { 0xf804, 1, 1, 1, 0x0000000f, 0x0000000f,0x0,0x0 },   /* star_tracker_sec_rx_config */
    { 0xf808, 1, 1, 0, 0x0000000f, 0x0000000f,0x0,0x0 },   /* star_tracker_sec_rx_data */
    { 0xf0008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },  /* debug_pkt_tx_desc_0 */
    { 0xf000c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },  /* debug_pkt_tx_desc_1 */
    { 0xf0010, 1, 1, 1, 0x00000030, 0x00000030,0x0,0x0 },   /* debug_pkt_tx_int_enable */
    { 0xf0014, 1, 1, 1, 0x0000003f, 0x00000030,0x0,0x0 },   /* debug_pkt_tx_int_status */
    { 0xf0800, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* debug_pkt_tx_ram_0 */
    { 0xf0c00, 256, 0, 1, 0xffffffff, 0xffffffff,0x0,0x0 }, /* debug_pkt_tx_ram_1 */
    { 0xf1008, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* debug_pkt_rx_desc_0 */
    { 0xf100c, 1, 1, 1, 0x000087ff, 0x000087ff,0x0,0x0 },   /* debug_pkt_rx_desc_1 */
    { 0xf1010, 1, 1, 1, 0x00003f30, 0x00003f30,0x0,0x0 },   /* debug_pkt_rx_int_enable */
    { 0xf1014, 1, 1, 1, 0x00003f3f, 0x00003f30,0x0,0x0 },   /* debug_pkt_rx_int_status */
    { 0xf1800, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }, /* debug_pkt_rx_ram_0 */
    { 0xf1c00, 256, 1, 0, 0xffffffff, 0xffffffff,0x0,0x0 }  /* debug_pkt_rx_ram_1 */
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
int qemu_cpu_ssr_init(void) {

    int sm_fpga_mem = 0;
    int sm_fpga_mem_op = 0;
    int sm_fpga_mem_sync = 0;
    int sm_fpga_operation = 0;
    int sm_fpga_operation_type = 0;
    int sm_fpga_cpu_sync = 0;

    sm_fpga_mem = shm_open(QEMU_SSR_FPGA_MEMORY_NAME,O_CREAT|O_RDWR,0660);
    if(sm_fpga_mem == -1) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    ftruncate(sm_fpga_mem,QEMU_SSR_FPGA_MEMORY_SIZE);
    qemu_ssr_fpga_memory = (unsigned char *) mmap(0,QEMU_SSR_FPGA_MEMORY_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,sm_fpga_mem,0);
    if(qemu_ssr_fpga_memory == MAP_FAILED) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    sm_fpga_mem_op = shm_open(QEMU_SSR_FPGA_MEMORY_REG_NAME,O_CREAT|O_RDWR,0660);
    if(sm_fpga_mem_op == -1) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    ftruncate(sm_fpga_mem_op,QEMU_SSR_FPGA_MEMORY_REG_SIZE);
    qemu_ssr_fpga_regs = (struct qemu_ssr_reg_config *) mmap(0,QEMU_SSR_FPGA_MEMORY_REG_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,sm_fpga_mem_op,0);
    if(qemu_ssr_fpga_regs == MAP_FAILED) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    sm_fpga_cpu_sync = shm_open(QEMU_SSR_FPGA_CPU_SYNC_NAME,O_CREAT|O_RDWR,0660);
    if(sm_fpga_cpu_sync == -1) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    ftruncate(sm_fpga_cpu_sync,QEMU_SSR_FPGA_CPU_SYNC_SIZE);
    qemu_ssr_fpga_cpu_sync = (unsigned int *) mmap(0,QEMU_SSR_FPGA_CPU_SYNC_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,sm_fpga_cpu_sync,0);
    if(qemu_ssr_fpga_cpu_sync == MAP_FAILED) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    sm_fpga_mem_sync = shm_open(QEMU_SSR_FPGA_MEMORY_SYNC_NAME,O_CREAT|O_RDWR,0660);
    if(sm_fpga_mem_sync == -1) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    ftruncate(sm_fpga_mem_sync,QEMU_SSR_FPGA_MEMORY_SYNC_SIZE);
    qemu_ssr_fpga_memory_sync = (unsigned int *) mmap(0,QEMU_SSR_FPGA_MEMORY_SYNC_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,sm_fpga_mem_sync,0);
    if(qemu_ssr_fpga_memory_sync == MAP_FAILED) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    sm_fpga_operation = shm_open(QEMU_SSR_FPGA_OPERATION_NAME,O_CREAT|O_RDWR,0660);
    if(sm_fpga_operation == -1) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    ftruncate(sm_fpga_operation,QEMU_SSR_FPGA_OPERATION_SIZE);
    qemu_ssr_fpga_operaton = (unsigned int *) mmap(0,QEMU_SSR_FPGA_OPERATION_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,sm_fpga_operation,0);
    if(qemu_ssr_fpga_operaton == MAP_FAILED) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    sm_fpga_operation_type = shm_open(QEMU_SSR_FPGA_OPERATION_TYPE_NAME,O_CREAT|O_RDWR,0660);
    if(sm_fpga_operation_type == -1) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    ftruncate(sm_fpga_operation_type,QEMU_SSR_FPGA_OPERATION_TYPE_SIZE);
    qemu_ssr_fpga_operaton_type = (unsigned int *) mmap(0,QEMU_SSR_FPGA_OPERATION_TYPE_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,sm_fpga_operation_type,0);
    if(qemu_ssr_fpga_operaton_type == MAP_FAILED) {
    	fprintf(stderr, "ERROR - [%s] - [%s][%d]\r\n", strerror( errno ), __FILE__, __LINE__);
    	return -1;
    }

    *qemu_ssr_fpga_memory_sync = 0x0;
    *qemu_ssr_fpga_operaton = 0x0;
    *qemu_ssr_fpga_operaton_type = 0x0;
    *qemu_ssr_fpga_cpu_sync = 0x0;

    for (int index = 0; index < QEMU_SSR_MEMORY_SIZE; index++) {
        qemu_ssr_fpga_memory[index] = 0;
        qemu_ssr_fpga_memory_op[index] = -1;
    }

    for (int index = 0; index < QEMU_SSR_NUM_REGS; index++) {
        qemu_ssr_fpga_regs[index] = qemu_ssr_fpga_regs_data[index];
        for (int index_reg = 0; index_reg < qemu_ssr_fpga_regs[index].num_words*4; index_reg++) {
            qemu_ssr_fpga_memory_op[qemu_ssr_fpga_regs[index].reg_offset+index_reg] = index;
        }
    }
  qemu_ssr_cpu_init_status = 1;

  return 0;
}