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

#define BCM2838_GENET_HFB_FILTER_REGS     offsetof(BCM2838GenetRegs, hfb)
#define BCM2838_GENET_HFB_FILTER_REG(reg) (BCM2838_GENET_HFB_FILTER_REGS \
                                           + offsetof(BCM2838GenetRegsHfb, reg))
#define BCM2838_GENET_HFB_FILTER_CNT      48
#define BCM2838_GENET_HFB_FILTER_SIZE     128

#define BCM2838_GENET_INTRL0_REG(reg)   (offsetof(BCM2838GenetRegs, intrl0) \
                                        + offsetof(BCM2838GenetRegsIntrl0, reg))
#define BCM2838_GENET_INTRL0_SET        BCM2838_GENET_INTRL0_REG(set)
#define BCM2838_GENET_INTRL0_CLEAR      BCM2838_GENET_INTRL0_REG(clear)
#define BCM2838_GENET_INTRL0_MASK_SET   BCM2838_GENET_INTRL0_REG(mask_set)
#define BCM2838_GENET_INTRL0_MASK_CLEAR BCM2838_GENET_INTRL0_REG(mask_clear)

#define BCM2838_GENET_INTRL1_REG(reg)   (offsetof(BCM2838GenetRegs, intrl1) \
                                        + offsetof(BCM2838GenetRegsIntrl1, reg))
#define BCM2838_GENET_INTRL1_SET        BCM2838_GENET_INTRL1_REG(set)
#define BCM2838_GENET_INTRL1_CLEAR      BCM2838_GENET_INTRL1_REG(clear)
#define BCM2838_GENET_INTRL1_MASK_SET   BCM2838_GENET_INTRL1_REG(mask_set)
#define BCM2838_GENET_INTRL1_MASK_CLEAR BCM2838_GENET_INTRL1_REG(mask_clear)

#define BCM2838_GENET_UMAC_REG(reg)     (offsetof(BCM2838GenetRegs, umac) \
                                         + offsetof(BCM2838GenetRegsUmac, reg))
#define BCM2838_GENET_UMAC_CMD          BCM2838_GENET_UMAC_REG(cmd)
#define BCM2838_GENET_UMAC_MAC0         BCM2838_GENET_UMAC_REG(mac0)
#define BCM2838_GENET_UMAC_MAC1         BCM2838_GENET_UMAC_REG(mac1)
#define BCM2838_GENET_UMAC_MDIO_CMD     BCM2838_GENET_UMAC_REG(mdio_cmd)

#define BCM2838_GENET_TDMA_REGS         offsetof(BCM2838GenetRegs, tdma)
#define BCM2838_GENET_TDMA_REG(reg)     (BCM2838_GENET_TDMA_REGS \
                                         + offsetof(BCM2838GenetRegsTdma, reg))
#define BCM2838_GENET_TDMA_RINGS        BCM2838_GENET_TDMA_REG(rings)
#define BCM2838_GENET_TDMA_RING_CFG     BCM2838_GENET_TDMA_REG(ring_cfg)
#define BCM2838_GENET_TDMA_CTRL         BCM2838_GENET_TDMA_REG(ctrl)
#define BCM2838_GENET_TDMA_STATUS       BCM2838_GENET_TDMA_REG(status)

#define BCM2838_GENET_RDMA_REGS         offsetof(BCM2838GenetRegs, rdma)
#define BCM2838_GENET_RDMA_REG(reg)     (BCM2838_GENET_RDMA_REGS \
                                         + offsetof(BCM2838GenetRegsRdma, reg))
#define BCM2838_GENET_RDMA_RINGS        BCM2838_GENET_RDMA_REG(rings)
#define BCM2838_GENET_RDMA_RING_CFG     BCM2838_GENET_RDMA_REG(ring_cfg)
#define BCM2838_GENET_RDMA_CTRL         BCM2838_GENET_RDMA_REG(ctrl)
#define BCM2838_GENET_RDMA_STATUS       BCM2838_GENET_RDMA_REG(status)

#define BCM2838_GENET_TRING_REG(reg)    offsetof(BCM2838GenetTdmaRing, reg)
#define BCM2838_GENET_TRING_WRITE_PTR BCM2838_GENET_TRING_REG(write_ptr)
#define BCM2838_GENET_TRING_WRITE_PTR_HI BCM2838_GENET_TRING_REG(write_ptr_hi)
#define BCM2838_GENET_TRING_PROD_INDEX BCM2838_GENET_TRING_REG(prod_index)
#define BCM2838_GENET_TRING_CONS_INDEX BCM2838_GENET_TRING_REG(cons_index)
#define BCM2838_GENET_TRING_RING_BUF_SIZE BCM2838_GENET_TRING_REG(ring_buf_size)
#define BCM2838_GENET_TRING_RING_START_ADDR BCM2838_GENET_TRING_REG(start_addr)
#define BCM2838_GENET_TRING_RING_START_ADDR_HI BCM2838_GENET_TRING_REG(start_addr_hi)
#define BCM2838_GENET_TRING_RING_END_ADDR BCM2838_GENET_TRING_REG(end_addr)
#define BCM2838_GENET_TRING_RING_END_ADDR_HI BCM2838_GENET_TRING_REG(end_addr_hi)
#define BCM2838_GENET_TRING_RING_MBUF_DONE_TRESH BCM2838_GENET_TRING_REG(mbuf_done_tresh)
#define BCM2838_GENET_TRING_RING_FLOW_PERIOD BCM2838_GENET_TRING_REG(flow_period)
#define BCM2838_GENET_TRING_RING_READ_PTR BCM2838_GENET_TRING_REG(read_ptr)
#define BCM2838_GENET_TRING_RING_READ_PTR_HI BCM2838_GENET_TRING_REG(read_ptr_hi)

#define BCM2838_GENET_RRING_REG(reg)    offsetof(BCM2838GenetRdmaRing, reg)
#define BCM2838_GENET_RRING_WRITE_PTR BCM2838_GENET_RRING_REG(write_ptr)
#define BCM2838_GENET_RRING_WRITE_PTR_HI BCM2838_GENET_RRING_REG(write_ptr_hi)
#define BCM2838_GENET_RRING_PROD_INDEX BCM2838_GENET_RRING_REG(prod_index)
#define BCM2838_GENET_RRING_CONS_INDEX BCM2838_GENET_RRING_REG(cons_index)
#define BCM2838_GENET_RRING_RING_BUF_SIZE BCM2838_GENET_RRING_REG(ring_buf_size)
#define BCM2838_GENET_RRING_RING_START_ADDR BCM2838_GENET_RRING_REG(start_addr)
#define BCM2838_GENET_RRING_RING_START_ADDR_HI BCM2838_GENET_RRING_REG(start_addr_hi)
#define BCM2838_GENET_RRING_RING_END_ADDR BCM2838_GENET_RRING_REG(end_addr)
#define BCM2838_GENET_RRING_RING_END_ADDR_HI BCM2838_GENET_RRING_REG(end_addr_hi)
#define BCM2838_GENET_RRING_RING_MBUF_DONE_TRESH BCM2838_GENET_RRING_REG(mbuf_done_tresh)
#define BCM2838_GENET_RRING_RING_XON_XOFF_TRESH BCM2838_GENET_RRING_REG(xon_xoff_tresh)
#define BCM2838_GENET_RRING_RING_READ_PTR BCM2838_GENET_RRING_REG(read_ptr)
#define BCM2838_GENET_RRING_RING_READ_PTR_HI BCM2838_GENET_RRING_REG(read_ptr_hi)


#define BCM2838_GENET_PHY_REG(reg)      (offsetof(BCM2838GenetPhyRegs, reg) / 2)
#define BCM2838_GENET_PHY_BMCR          BCM2838_GENET_PHY_REG(bmcr)
#define BCM2838_GENET_PHY_AUX_CTL       BCM2838_GENET_PHY_REG(aux_ctl)
#define BCM2838_GENET_PHY_SHD           BCM2838_GENET_PHY_REG(shd)
#define BCM2838_GENET_EXP_DATA          BCM2838_GENET_PHY_REG(exp_data)
#define BCM2838_GENET_EXP_SEL           BCM2838_GENET_PHY_REG(exp_ctrl)

#define BCM2838_GENET_PHY_AUX_CTL_AUXCTL    0x0
#define BCM2838_GENET_PHY_AUX_CTL_MISC      0x7
#define BCM2838_GENET_PHY_AUX_CTL_REGS_SIZE 8

#define BCM2838_GENET_PHY_EXP_SHD_BLOCKS_CNT 256
#define BCM2838_GENET_PHY_EXP_SHD_REGS_CNT   256

#define MAX_FRAME_SIZE                  0xFFF
#define MAX_PACKET_SIZE                 1518
#define MAX_PAYLOAD_SIZE                1500
#define TX_MIN_PKT_SIZE                 60


typedef struct BCM2838GenetXmitStatus {
    uint32_t  length_status;  /* length and peripheral status */
    uint32_t  ext_status;     /* Extended status */
    uint32_t  rx_csum;        /* partial rx checksum */
    uint32_t  unused1[9];     /* unused */
    uint32_t  tx_csum_info;   /* Tx checksum info. */
    uint32_t  unused2[3];     /* unused */
} BCM2838GenetXmitStatus;

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
    uint32_t flt[BCM2838_GENET_HFB_FILTER_CNT * BCM2838_GENET_HFB_FILTER_SIZE];
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

typedef struct {
    uint16_t reserved_0_2[3];
    uint16_t clk_ctl;
    uint16_t scr2;
    uint16_t scr3;
    uint16_t reserved_6_9[4];
    uint16_t apd;
    uint16_t rgmii_mode;
    uint16_t reserved_12;
    uint16_t leds1;
    uint16_t reserved_14_18[5];
    uint16_t _100fx_ctrl;
    uint16_t ssd;
    uint16_t reserved_21_30[10];
    uint16_t mode;
} BCM2838GenetPhyShdRegs;

typedef struct {
    uint16_t regs[BCM2838_GENET_PHY_AUX_CTL_REGS_SIZE];
} BCM2838GenetPhyAuxShdRegs;

typedef struct {
    uint16_t regs[BCM2838_GENET_PHY_EXP_SHD_BLOCKS_CNT]
                 [BCM2838_GENET_PHY_EXP_SHD_REGS_CNT];
} BCM2838GenetPhyExpShdRegs;

struct BCM2838GenetState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    NICState *nic;
    NICConf nic_conf;

    MemoryRegion regs_mr;
    AddressSpace dma_as;

    BCM2838GenetRegs regs;
    BCM2838GenetPhyRegs phy_regs;
    BCM2838GenetPhyShdRegs phy_shd_regs;
    BCM2838GenetPhyAuxShdRegs phy_aux_ctl_shd_regs;
    BCM2838GenetPhyExpShdRegs phy_exp_shd_regs;

    qemu_irq irq_default;
    qemu_irq irq_prio;

    uint8_t tx_packet[MAX_FRAME_SIZE];
};

/*
 * Every field of BCM2838GenetRegs is a uint32_t and the MMIO ops only allow
 * naturally aligned 4-byte accesses, so a register can be accessed as a
 * host-order uint32_t at its byte offset; the memory API takes care of the
 * guest/host byte order difference.
 */
static inline uint32_t *bcm2838_genet_reg(BCM2838GenetState *s, hwaddr offset)
{
    QEMU_BUILD_BUG_ON(sizeof(BCM2838GenetRegs) % sizeof(uint32_t) != 0);
    assert(offset % sizeof(uint32_t) == 0);

    return (uint32_t *)((uint8_t *)&s->regs + offset);
}

#endif /* BCM2838_GENET_H */
