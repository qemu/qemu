/*
 * BCM2838 Gigabit Ethernet emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_GENET_H
#define BCM2838_GENET_H

#include "net/net.h"
#include "hw/core/sysbus.h"

#define TYPE_BCM2838_GENET "bcm2838-genet"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838GenetState, BCM2838_GENET)

#define BCM2838_GENET_REV_MAJOR         6
#define BCM2838_GENET_REV_MINOR         0

#define BCM2838_GENET_DMA_DESC_CNT      256
#define BCM2838_GENET_DMA_RING_CNT      17
#define BCM2838_GENET_DMA_RING_DEFAULT  (BCM2838_GENET_DMA_RING_CNT - 1)

#define BCM2838_GENET_HFB_FILTER_CNT      48
#define BCM2838_GENET_HFB_FILTER_SIZE     128

typedef struct {
    uint32_t rev_ctrl;
    uint32_t port_ctrl;
    uint32_t rbuf_flush_ctrl;
    uint32_t tbuf_flush_ctrl;
    uint32_t reserved_0x10[12];
} BCM2838GenetRegsSys;

typedef struct {
    uint32_t reserved_0x0[16];
} BCM2838GenetRegsGrBridge;

typedef struct {
    uint32_t pwr_mgmt;
    uint32_t reserved_0x4[2];
    uint32_t rgmii_oob_ctrl;
    uint32_t reserved_0x10[3];
    uint32_t gphy_ctrl;
    uint32_t reserved_0x20[24];
} BCM2838GenetRegsExt;

typedef struct {
    uint32_t stat;
    uint32_t set;
    uint32_t clear;
    uint32_t mask_status;
    uint32_t mask_set;
    uint32_t mask_clear;
    uint32_t reserved_0x18[10];
} BCM2838GenetRegsIntrl0;

typedef struct {
    uint32_t stat;
    uint32_t set;
    uint32_t clear;
    uint32_t mask_status;
    uint32_t mask_set;
    uint32_t mask_clear;
    uint32_t reserved_0x18[10];
} BCM2838GenetRegsIntrl1;

typedef struct {
    uint32_t ctrl;
    uint32_t reserved_0x4[2];
    uint32_t status;
    uint32_t reserved_0x10;
    uint32_t chk_ctrl;
    uint32_t reserved_0x18[31];
    uint32_t ovfl_cnt;
    uint32_t err_cnt;
    uint32_t energy_ctrl;
    uint32_t reserved_0xA0[5];
    uint32_t size_ctrl;
    uint32_t reserved_0xB8[18];
} BCM2838GenetRegsRbuf;

typedef struct {
    uint32_t ctrl;
    uint32_t reserved_0x4[2];
    uint32_t bp_mc;
    uint32_t reserved_0x10;
    uint32_t energy_ctrl;
    uint32_t reserved_0x18[58];
} BCM2838GenetRegsTbuf;

typedef struct {
    uint32_t reserved_0x0;
    uint32_t hd_bkp_ctrl;
    uint32_t cmd;
    uint32_t mac0;
    uint32_t mac1;
    uint32_t max_frame_len;
    uint32_t pause_quanta;
    uint32_t reserved_0x1C[10];
    uint32_t mode;
    uint32_t frm_tag0;
    uint32_t frm_tag1;
    uint32_t reserved_0x50[3];
    uint32_t tx_ipg_len;
    uint32_t reserved_0x60;
    uint32_t eee_ctrl;
    uint32_t eee_lpi_timer;
    uint32_t eee_wake_timer;
    uint32_t eee_ref_count;
    uint32_t reserved_0x74;
    uint32_t rx_ipg_inv;
    uint32_t reserved_0x7C[165];
    uint32_t macsec_prog_tx_crc;
    uint32_t macsec_ctrl;
    uint32_t reserved_0x318[6];
    uint32_t pause_ctrl;
    uint32_t tx_flush;
    uint32_t rx_fifo_status;
    uint32_t tx_fifo_status;
    uint32_t reserved_0x340[48];
    uint32_t mib[96];
    uint32_t mib_ctrl;
    uint32_t reserved_0x584[36];
    uint32_t mdio_cmd;
    uint32_t reserved_0x618[2];
    uint32_t mpd_ctrl;
    uint32_t mpd_pw_ms;
    uint32_t mpd_pw_ls;
    uint32_t reserved_0x62C[3];
    uint32_t mdf_err_cnt;
    uint32_t reserved_0x63C[5];
    uint32_t mdf_ctrl;
    uint32_t mdf_addr;
    uint32_t reserved_0x658[106];
} BCM2838GenetRegsUmac;

typedef struct {
    uint32_t length_status;
    uint32_t address_lo;
    uint32_t address_hi;
} BCM2838GenetRdmaDesc;

typedef struct {
    uint32_t write_ptr;
    uint32_t write_ptr_hi;
    uint32_t prod_index;
    uint32_t cons_index;
    uint32_t ring_buf_size;
    uint32_t start_addr;
    uint32_t start_addr_hi;
    uint32_t end_addr;
    uint32_t end_addr_hi;
    uint32_t mbuf_done_tresh;
    uint32_t xon_xoff_tresh;
    uint32_t read_ptr;
    uint32_t read_ptr_hi;
    uint32_t reserved_0x34[3];
} BCM2838GenetRdmaRing;

typedef struct {
    BCM2838GenetRdmaDesc descs[BCM2838_GENET_DMA_DESC_CNT];
    BCM2838GenetRdmaRing rings[BCM2838_GENET_DMA_RING_CNT];
    uint32_t ring_cfg;
    uint32_t ctrl;
    uint32_t status;
    uint32_t scb_burst_size;
    uint32_t reserved_0x1050[7];
    uint32_t ring_timeout[17];
    uint32_t index2ring[8];
    uint32_t reserved_0x10D0[972];
} BCM2838GenetRegsRdma;

typedef struct {
    uint32_t length_status;
    uint32_t address_lo;
    uint32_t address_hi;
} BCM2838GenetTdmaDesc;

typedef struct {
    uint32_t read_ptr;
    uint32_t read_ptr_hi;
    uint32_t cons_index;
    uint32_t prod_index;
    uint32_t ring_buf_size;
    uint32_t start_addr;
    uint32_t start_addr_hi;
    uint32_t end_addr;
    uint32_t end_addr_hi;
    uint32_t mbuf_done_tresh;
    uint32_t flow_period;
    uint32_t write_ptr;
    uint32_t write_ptr_hi;
    uint32_t reserved_0x34[3];
} BCM2838GenetTdmaRing;

typedef struct {
    BCM2838GenetTdmaDesc descs[BCM2838_GENET_DMA_DESC_CNT];
    BCM2838GenetTdmaRing rings[BCM2838_GENET_DMA_RING_CNT];
    uint32_t ring_cfg;
    uint32_t ctrl;
    uint32_t status;
    uint32_t scb_burst_size;
    uint32_t reserved_0x1050[7];
    uint32_t arb_ctrl;
    uint32_t priority[3];
    uint32_t reserved_0x10D0[993];
} BCM2838GenetRegsTdma;

typedef struct {
    uint8_t flt[BCM2838_GENET_HFB_FILTER_CNT * BCM2838_GENET_HFB_FILTER_SIZE
        * sizeof(uint32_t)];
    uint32_t reserved_0x6000[1792];
    uint32_t ctrl;
    uint32_t flt_enable[2];
    uint32_t reserved_0x7C0C[4];
    uint32_t flt_len[BCM2838_GENET_HFB_FILTER_CNT / sizeof(uint32_t)];
    uint32_t reserved_0x7C4C[237];
} BCM2838GenetRegsHfb;

typedef struct {
    BCM2838GenetRegsSys sys;
    BCM2838GenetRegsGrBridge gr_bridge;
    BCM2838GenetRegsExt ext;
    uint32_t reserved_0x100[64];
    BCM2838GenetRegsIntrl0 intrl0;
    BCM2838GenetRegsIntrl1 intrl1;
    uint32_t reserved_0x280[32];
    BCM2838GenetRegsRbuf rbuf;
    uint32_t reserved_0x400[128];
    BCM2838GenetRegsTbuf tbuf;
    uint32_t reserved_0x700[64];
    BCM2838GenetRegsUmac umac;
    uint32_t reserved_0x1000[1024];
    BCM2838GenetRegsRdma rdma;
    BCM2838GenetRegsTdma tdma;
    uint32_t reserved_0x6000[2048];
    BCM2838GenetRegsHfb hfb;
} BCM2838GenetRegs;

typedef struct {
    uint16_t bmcr;
    uint16_t bmsr;
    uint16_t sid1;
    uint16_t sid2;
    uint16_t advertise;
    uint16_t lpa;
    uint16_t expansion;
    uint16_t next_page;
    uint16_t lpa_next_page;
    uint16_t ctrl1000;
    uint16_t stat1000;
    uint16_t reserved_11_12[2];
    uint16_t mmd_ctrl;
    uint16_t mmd_data;
    uint16_t estatus;
    uint16_t ecr;
    uint16_t esr;
    uint16_t dcounter;
    uint16_t fcscounter;
    uint16_t nwaytest;
    uint16_t exp_data;
    uint16_t srevision;
    uint16_t exp_ctrl;
    uint16_t aux_ctl;
    uint16_t phyaddr;
    uint16_t isr;
    uint16_t imr;
    uint16_t shd;
    uint16_t reserved_29;
    uint16_t rdb_addr;
    uint16_t rdb_data;
} BCM2838GenetPhyRegs;

struct BCM2838GenetState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    MemoryRegion regs_mr;
    AddressSpace dma_as;

    BCM2838GenetRegs regs;
    BCM2838GenetPhyRegs phy_regs;

    qemu_irq irq_default;
    qemu_irq irq_prio;
};

#endif /* BCM2838_GENET_H */
