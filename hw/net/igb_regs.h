/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is copied + edited from kernel header files in
 * drivers/net/ethernet/intel/igb
 */

#ifndef HW_IGB_REGS_H_
#define HW_IGB_REGS_H_

#include "e1000x_regs.h"

/* from igb/e1000_hw.h */

#define E1000_DEV_ID_82576                 0x10C9
#define E1000_DEV_ID_82576_FIBER           0x10E6
#define E1000_DEV_ID_82576_SERDES          0x10E7
#define E1000_DEV_ID_82576_QUAD_COPPER             0x10E8
#define E1000_DEV_ID_82576_QUAD_COPPER_ET2 0x1526
#define E1000_DEV_ID_82576_NS                      0x150A
#define E1000_DEV_ID_82576_NS_SERDES               0x1518
#define E1000_DEV_ID_82576_SERDES_QUAD             0x150D

/* Context Descriptor */
struct e1000_adv_tx_context_desc {
    uint32_t vlan_macip_lens;
    uint32_t seqnum_seed;
    uint32_t type_tucmd_mlhl;
    uint32_t mss_l4len_idx;
};

/* Advanced Transmit Descriptor */
union e1000_adv_tx_desc {
    struct {
        uint64_t buffer_addr;     /* Address of descriptor's data buffer */
        uint32_t cmd_type_len;
        uint32_t olinfo_status;
    } read;
    struct {
        uint64_t rsvd;            /* Reserved */
        uint32_t nxtseq_seed;
        uint32_t status;
    } wb;
};

#define E1000_ADVTXD_DTYP_CTXT  0x00200000 /* Advanced Context Descriptor */
#define E1000_ADVTXD_DTYP_DATA  0x00300000 /* Advanced Data Descriptor */
#define E1000_ADVTXD_DCMD_DEXT  0x20000000 /* Descriptor Extension (1=Adv) */
#define E1000_ADVTXD_DCMD_TSE   0x80000000 /* TCP/UDP Segmentation Enable */

#define E1000_ADVTXD_POTS_IXSM  0x00000100 /* Insert TCP/UDP Checksum */
#define E1000_ADVTXD_POTS_TXSM  0x00000200 /* Insert TCP/UDP Checksum */

#define E1000_TXD_POPTS_IXSM 0x00000001 /* Insert IP checksum */
#define E1000_TXD_POPTS_TXSM 0x00000002 /* Insert TCP/UDP checksum */

/* Receive Descriptor - Advanced */
union e1000_adv_rx_desc {
    struct {
        uint64_t pkt_addr;                /* Packet Buffer Address */
        uint64_t hdr_addr;                /* Header Buffer Address */
    } read;
    struct {
        struct {
            struct {
                uint16_t pkt_info;        /* RSS Type, Packet Type */
                uint16_t hdr_info;        /* Split Head, Buffer Length */
            } lo_dword;
            union {
                uint32_t rss;             /* RSS Hash */
                struct {
                        uint16_t ip_id;   /* IP Id */
                        uint16_t csum;    /* Packet Checksum */
                } csum_ip;
            } hi_dword;
        } lower;
        struct {
            uint32_t status_error;        /* Ext Status/Error */
            uint16_t length;              /* Packet Length */
            uint16_t vlan;                /* VLAN tag */
        } upper;
    } wb;  /* writeback */
};

/* from igb/e1000_phy.h */

/* IGP01E1000 Specific Registers */
#define IGP01E1000_PHY_PORT_CONFIG        0x10 /* Port Config */
#define IGP01E1000_PHY_PORT_STATUS        0x11 /* Status */
#define IGP01E1000_PHY_PORT_CTRL          0x12 /* Control */
#define IGP01E1000_PHY_LINK_HEALTH        0x13 /* PHY Link Health */
#define IGP02E1000_PHY_POWER_MGMT         0x19 /* Power Management */
#define IGP01E1000_PHY_PAGE_SELECT        0x1F /* Page Select */
#define IGP01E1000_PHY_PCS_INIT_REG       0x00B4
#define IGP01E1000_PHY_POLARITY_MASK      0x0078
#define IGP01E1000_PSCR_AUTO_MDIX         0x1000
#define IGP01E1000_PSCR_FORCE_MDI_MDIX    0x2000 /* 0=MDI, 1=MDIX */
#define IGP01E1000_PSCFR_SMART_SPEED      0x0080

/* Enable flexible speed on link-up */
#define IGP02E1000_PM_D0_LPLU             0x0002 /* For D0a states */
#define IGP02E1000_PM_D3_LPLU             0x0004 /* For all other states */
#define IGP01E1000_PLHR_SS_DOWNGRADE      0x8000
#define IGP01E1000_PSSR_POLARITY_REVERSED 0x0002
#define IGP01E1000_PSSR_MDIX              0x0800
#define IGP01E1000_PSSR_SPEED_MASK        0xC000
#define IGP01E1000_PSSR_SPEED_1000MBPS    0xC000
#define IGP02E1000_PHY_CHANNEL_NUM        4
#define IGP02E1000_PHY_AGC_A              0x11B1
#define IGP02E1000_PHY_AGC_B              0x12B1
#define IGP02E1000_PHY_AGC_C              0x14B1
#define IGP02E1000_PHY_AGC_D              0x18B1
#define IGP02E1000_AGC_LENGTH_SHIFT       9   /* Course - 15:13, Fine - 12:9 */
#define IGP02E1000_AGC_LENGTH_MASK        0x7F
#define IGP02E1000_AGC_RANGE              15

/* from igb/igb.h */

#define E1000_PCS_CFG_IGN_SD     1

/* Interrupt defines */
#define IGB_START_ITR            648 /* ~6000 ints/sec */
#define IGB_4K_ITR               980
#define IGB_20K_ITR              196
#define IGB_70K_ITR              56

/* TX/RX descriptor defines */
#define IGB_DEFAULT_TXD          256
#define IGB_DEFAULT_TX_WORK      128
#define IGB_MIN_TXD              80
#define IGB_MAX_TXD              4096

#define IGB_DEFAULT_RXD          256
#define IGB_MIN_RXD              80
#define IGB_MAX_RXD              4096

#define IGB_DEFAULT_ITR           3 /* dynamic */
#define IGB_MAX_ITR_USECS         10000
#define IGB_MIN_ITR_USECS         10
#define NON_Q_VECTORS             1
#define MAX_Q_VECTORS             8
#define MAX_MSIX_ENTRIES          10

/* Transmit and receive queues */
#define IGB_MAX_RX_QUEUES          8
#define IGB_MAX_RX_QUEUES_82575    4
#define IGB_MAX_RX_QUEUES_I211     2
#define IGB_MAX_TX_QUEUES          8
#define IGB_MAX_VF_MC_ENTRIES      30
#define IGB_MAX_VF_FUNCTIONS       8
#define IGB_MAX_VFTA_ENTRIES       128
#define IGB_82576_VF_DEV_ID        0x10CA
#define IGB_I350_VF_DEV_ID         0x1520

/* from igb/e1000_82575.h */

#define E1000_MRQC_ENABLE_RSS_MQ            0x00000002
#define E1000_MRQC_ENABLE_VMDQ              0x00000003
#define E1000_MRQC_RSS_FIELD_IPV4_UDP       0x00400000
#define E1000_MRQC_ENABLE_VMDQ_RSS_MQ       0x00000005
#define E1000_MRQC_RSS_FIELD_IPV6_UDP       0x00800000
#define E1000_MRQC_RSS_FIELD_IPV6_UDP_EX    0x01000000

/* Additional Transmit Descriptor Control definitions */
#define E1000_TXDCTL_QUEUE_ENABLE  0x02000000 /* Enable specific Tx Queue */

/* Additional Receive Descriptor Control definitions */
#define E1000_RXDCTL_QUEUE_ENABLE  0x02000000 /* Enable specific Rx Queue */

/* Direct Cache Access (DCA) definitions */
#define E1000_DCA_CTRL_DCA_MODE_DISABLE 0x01 /* DCA Disable */
#define E1000_DCA_CTRL_DCA_MODE_CB2     0x02 /* DCA Mode CB2 */

#define E1000_DCA_RXCTRL_CPUID_MASK 0x0000001F /* Rx CPUID Mask */
#define E1000_DCA_RXCTRL_DESC_DCA_EN BIT(5) /* DCA Rx Desc enable */
#define E1000_DCA_RXCTRL_HEAD_DCA_EN BIT(6) /* DCA Rx Desc header enable */
#define E1000_DCA_RXCTRL_DATA_DCA_EN BIT(7) /* DCA Rx Desc payload enable */
#define E1000_DCA_RXCTRL_DESC_RRO_EN BIT(9) /* DCA Rx rd Desc Relax Order */

#define E1000_DCA_TXCTRL_CPUID_MASK 0x0000001F /* Tx CPUID Mask */
#define E1000_DCA_TXCTRL_DESC_DCA_EN BIT(5) /* DCA Tx Desc enable */
#define E1000_DCA_TXCTRL_DESC_RRO_EN BIT(9) /* Tx rd Desc Relax Order */
#define E1000_DCA_TXCTRL_TX_WB_RO_EN BIT(11) /* Tx Desc writeback RO bit */
#define E1000_DCA_TXCTRL_DATA_RRO_EN BIT(13) /* Tx rd data Relax Order */

/* Additional DCA related definitions, note change in position of CPUID */
#define E1000_DCA_TXCTRL_CPUID_MASK_82576 0xFF000000 /* Tx CPUID Mask */
#define E1000_DCA_RXCTRL_CPUID_MASK_82576 0xFF000000 /* Rx CPUID Mask */
#define E1000_DCA_TXCTRL_CPUID_SHIFT 24 /* Tx CPUID now in the last byte */
#define E1000_DCA_RXCTRL_CPUID_SHIFT 24 /* Rx CPUID now in the last byte */

#define E1000_DTXSWC_MAC_SPOOF_MASK   0x000000FF /* Per VF MAC spoof control */
#define E1000_DTXSWC_VLAN_SPOOF_MASK  0x0000FF00 /* Per VF VLAN spoof control */
#define E1000_DTXSWC_LLE_MASK         0x00FF0000 /* Per VF Local LB enables */
#define E1000_DTXSWC_VLAN_SPOOF_SHIFT 8
#define E1000_DTXSWC_VMDQ_LOOPBACK_EN BIT(31)  /* global VF LB enable */

/* Easy defines for setting default pool, would normally be left a zero */
#define E1000_VT_CTL_DEFAULT_POOL_SHIFT 7
#define E1000_VT_CTL_DEFAULT_POOL_MASK  (0x7 << E1000_VT_CTL_DEFAULT_POOL_SHIFT)

/* Other useful VMD_CTL register defines */
#define E1000_VT_CTL_IGNORE_MAC         BIT(28)
#define E1000_VT_CTL_DISABLE_DEF_POOL   BIT(29)
#define E1000_VT_CTL_VM_REPL_EN         BIT(30)

/* Per VM Offload register setup */
#define E1000_VMOLR_RLPML_MASK 0x00003FFF /* Long Packet Maximum Length mask */
#define E1000_VMOLR_LPE        0x00010000 /* Accept Long packet */
#define E1000_VMOLR_RSSE       0x00020000 /* Enable RSS */
#define E1000_VMOLR_AUPE       0x01000000 /* Accept untagged packets */
#define E1000_VMOLR_ROMPE      0x02000000 /* Accept overflow multicast */
#define E1000_VMOLR_ROPE       0x04000000 /* Accept overflow unicast */
#define E1000_VMOLR_BAM        0x08000000 /* Accept Broadcast packets */
#define E1000_VMOLR_MPME       0x10000000 /* Multicast promiscuous mode */
#define E1000_VMOLR_STRVLAN    0x40000000 /* Vlan stripping enable */
#define E1000_VMOLR_STRCRC     0x80000000 /* CRC stripping enable */

#define E1000_DVMOLR_HIDEVLAN  0x20000000 /* Hide vlan enable */
#define E1000_DVMOLR_STRVLAN   0x40000000 /* Vlan stripping enable */
#define E1000_DVMOLR_STRCRC    0x80000000 /* CRC stripping enable */

#define E1000_VLVF_ARRAY_SIZE     32
#define E1000_VLVF_VLANID_MASK    0x00000FFF
#define E1000_VLVF_POOLSEL_SHIFT  12
#define E1000_VLVF_POOLSEL_MASK   (0xFF << E1000_VLVF_POOLSEL_SHIFT)
#define E1000_VLVF_LVLAN          0x00100000
#define E1000_VLVF_VLANID_ENABLE  0x80000000

#define E1000_VMVIR_VLANA_DEFAULT      0x40000000 /* Always use default VLAN */
#define E1000_VMVIR_VLANA_NEVER        0x80000000 /* Never insert VLAN tag */

#define E1000_IOVCTL 0x05BBC
#define E1000_IOVCTL_REUSE_VFQ 0x00000001

#define E1000_RPLOLR_STRVLAN   0x40000000
#define E1000_RPLOLR_STRCRC    0x80000000

#define E1000_DTXCTL_8023LL     0x0004
#define E1000_DTXCTL_VLAN_ADDED 0x0008
#define E1000_DTXCTL_OOS_ENABLE 0x0010
#define E1000_DTXCTL_MDP_EN     0x0020
#define E1000_DTXCTL_SPOOF_INT  0x0040

/* from igb/e1000_defines.h */

/* Physical Func Reset Done Indication */
#define E1000_CTRL_EXT_PFRSTD   0x00004000

#define E1000_IVAR_VALID     0x80
#define E1000_GPIE_NSICR     0x00000001
#define E1000_GPIE_MSIX_MODE 0x00000010
#define E1000_GPIE_EIAME     0x40000000
#define E1000_GPIE_PBA       0x80000000

/* Transmit Control */
#define E1000_TCTL_EN     0x00000002    /* enable tx */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */
#define E1000_TCTL_RTLC   0x01000000    /* Re-transmit on late collision */

/* Collision related configuration parameters */
#define E1000_COLLISION_THRESHOLD       15
#define E1000_CT_SHIFT                  4
#define E1000_COLLISION_DISTANCE        63
#define E1000_COLD_SHIFT                12

#define E1000_RAH_POOL_MASK 0x03FC0000
#define E1000_RAH_POOL_1 0x00040000

#define E1000_ICR_VMMB         0x00000100 /* VM MB event */
#define E1000_ICR_TS           0x00080000 /* Time Sync Interrupt */
#define E1000_ICR_DRSTA        0x40000000 /* Device Reset Asserted */
/* If this bit asserted, the driver should claim the interrupt */
#define E1000_ICR_INT_ASSERTED 0x80000000
/* LAN connected device generates an interrupt */
#define E1000_ICR_DOUTSYNC     0x10000000 /* NIC DMA out of sync */

/* Extended Interrupt Cause Read */
#define E1000_EICR_RX_QUEUE0    0x00000001 /* Rx Queue 0 Interrupt */
#define E1000_EICR_RX_QUEUE1    0x00000002 /* Rx Queue 1 Interrupt */
#define E1000_EICR_RX_QUEUE2    0x00000004 /* Rx Queue 2 Interrupt */
#define E1000_EICR_RX_QUEUE3    0x00000008 /* Rx Queue 3 Interrupt */
#define E1000_EICR_TX_QUEUE0    0x00000100 /* Tx Queue 0 Interrupt */
#define E1000_EICR_TX_QUEUE1    0x00000200 /* Tx Queue 1 Interrupt */
#define E1000_EICR_TX_QUEUE2    0x00000400 /* Tx Queue 2 Interrupt */
#define E1000_EICR_TX_QUEUE3    0x00000800 /* Tx Queue 3 Interrupt */
#define E1000_EICR_OTHER        0x80000000 /* Interrupt Cause Active */

/* Extended Interrupt Cause Set */
/* E1000_EITR_CNT_IGNR is only for 82576 and newer */
#define E1000_EITR_CNT_IGNR     0x80000000 /* Don't reset counters on write */

/* PCI Express Control */
#define E1000_GCR_CMPL_TMOUT_MASK       0x0000F000
#define E1000_GCR_CMPL_TMOUT_10ms       0x00001000
#define E1000_GCR_CMPL_TMOUT_RESEND     0x00010000
#define E1000_GCR_CAP_VER2              0x00040000

#define PHY_REVISION_MASK      0xFFFFFFF0
#define MAX_PHY_REG_ADDRESS    0x1F  /* 5 bit address bus (0-0x1F) */
#define MAX_PHY_MULTI_PAGE_REG 0xF

#define IGP03E1000_E_PHY_ID 0x02A80390

/* from igb/e1000_mbox.h */

#define E1000_P2VMAILBOX_STS  0x00000001 /* Initiate message send to VF */
#define E1000_P2VMAILBOX_ACK  0x00000002 /* Ack message recv'd from VF */
#define E1000_P2VMAILBOX_VFU  0x00000004 /* VF owns the mailbox buffer */
#define E1000_P2VMAILBOX_PFU  0x00000008 /* PF owns the mailbox buffer */
#define E1000_P2VMAILBOX_RVFU 0x00000010 /* Reset VFU - used when VF stuck */

#define E1000_MBVFICR_VFREQ_MASK 0x000000FF /* bits for VF messages */
#define E1000_MBVFICR_VFREQ_VF1  0x00000001 /* bit for VF 1 message */
#define E1000_MBVFICR_VFACK_MASK 0x00FF0000 /* bits for VF acks */
#define E1000_MBVFICR_VFACK_VF1  0x00010000 /* bit for VF 1 ack */

#define E1000_V2PMAILBOX_SIZE 16 /* 16 32 bit words - 64 bytes */

/*
 * If it's a E1000_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is E1000_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
/* Messages below or'd with this are the ACK */
#define E1000_VT_MSGTYPE_ACK 0x80000000
/* Messages below or'd with this are the NACK */
#define E1000_VT_MSGTYPE_NACK 0x40000000
/* Indicates that VF is still clear to send requests */
#define E1000_VT_MSGTYPE_CTS 0x20000000
#define E1000_VT_MSGINFO_SHIFT 16
/* bits 23:16 are used for exra info for certain messages */
#define E1000_VT_MSGINFO_MASK (0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_RESET                 0x01 /* VF requests reset */
#define E1000_VF_SET_MAC_ADDR          0x02 /* VF requests to set MAC addr */
/* VF requests to clear all unicast MAC filters */
#define E1000_VF_MAC_FILTER_CLR        (0x01 << E1000_VT_MSGINFO_SHIFT)
/* VF requests to add unicast MAC filter */
#define E1000_VF_MAC_FILTER_ADD        (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_MULTICAST         0x03 /* VF requests to set MC addr */
#define E1000_VF_SET_VLAN              0x04 /* VF requests to set VLAN */
#define E1000_VF_SET_LPE               0x05 /* VF requests to set VMOLR.LPE */
#define E1000_VF_SET_PROMISC           0x06 /*VF requests to clear VMOLR.ROPE/MPME*/
#define E1000_VF_SET_PROMISC_MULTICAST (0x02 << E1000_VT_MSGINFO_SHIFT)

#define E1000_PF_CONTROL_MSG 0x0100 /* PF control message */

/* from igb/e1000_regs.h */

#define E1000_EICR      0x01580  /* Ext. Interrupt Cause Read - R/clr */
#define E1000_EITR(_n)  (0x01680 + (0x4 * (_n)))
#define E1000_EICS      0x01520  /* Ext. Interrupt Cause Set - W0 */
#define E1000_EIMS      0x01524  /* Ext. Interrupt Mask Set/Read - RW */
#define E1000_EIMC      0x01528  /* Ext. Interrupt Mask Clear - WO */
#define E1000_EIAC      0x0152C  /* Ext. Interrupt Auto Clear - RW */
#define E1000_EIAM      0x01530  /* Ext. Interrupt Ack Auto Clear Mask - RW */
#define E1000_GPIE      0x01514  /* General Purpose Interrupt Enable; RW */
#define E1000_IVAR0     0x01700  /* Interrupt Vector Allocation Register - RW */
#define E1000_IVAR_MISC 0x01740  /* Interrupt Vector Allocation Register (last) - RW */
#define E1000_FRTIMER   0x01048  /* Free Running Timer - RW */
#define E1000_FCRTV     0x02460  /* Flow Control Refresh Timer Value - RW */

#define E1000_RQDPC(_n) (0x0C030 + ((_n) * 0x40))

#define E1000_RXPBS 0x02404  /* Rx Packet Buffer Size - RW */
#define E1000_TXPBS 0x03404  /* Tx Packet Buffer Size - RW */

#define E1000_DTXCTL 0x03590  /* DMA TX Control - RW */

#define E1000_HTCBDPC     0x04124  /* Host TX Circuit Breaker Dropped Count */
#define E1000_RLPML       0x05004  /* RX Long Packet Max Length */
#define E1000_RA2         0x054E0  /* 2nd half of Rx address array - RW Array */
#define E1000_PSRTYPE(_i) (0x05480 + ((_i) * 4))
#define E1000_VT_CTL   0x0581C  /* VMDq Control - RW */

/* VT Registers */
#define E1000_MBVFICR   0x00C80 /* Mailbox VF Cause - RWC */
#define E1000_MBVFIMR   0x00C84 /* Mailbox VF int Mask - RW */
#define E1000_VFLRE     0x00C88 /* VF Register Events - RWC */
#define E1000_VFRE      0x00C8C /* VF Receive Enables */
#define E1000_VFTE      0x00C90 /* VF Transmit Enables */
#define E1000_QDE       0x02408 /* Queue Drop Enable - RW */
#define E1000_DTXSWC    0x03500 /* DMA Tx Switch Control - RW */
#define E1000_WVBR      0x03554 /* VM Wrong Behavior - RWS */
#define E1000_RPLOLR    0x05AF0 /* Replication Offload - RW */
#define E1000_UTA       0x0A000 /* Unicast Table Array - RW */
#define E1000_IOVTCL    0x05BBC /* IOV Control Register */
#define E1000_TXSWC     0x05ACC /* Tx Switch Control */
#define E1000_LVMMC     0x03548 /* Last VM Misbehavior cause */
/* These act per VF so an array friendly macro is used */
#define E1000_P2VMAILBOX(_n)   (0x00C00 + (4 * (_n)))
#define E1000_VMBMEM(_n)       (0x00800 + (64 * (_n)))
#define E1000_VMOLR(_n)        (0x05AD0 + (4 * (_n)))
#define E1000_DVMOLR(_n)       (0x0C038 + (64 * (_n)))
#define E1000_VLVF(_n)         (0x05D00 + (4 * (_n))) /* VLAN VM Filter */
#define E1000_VMVIR(_n)        (0x03700 + (4 * (_n)))

/* from igbvf/defines.h */

/* SRRCTL bit definitions */
#define E1000_SRRCTL_BSIZEPKT_SHIFT            10 /* Shift _right_ */
#define E1000_SRRCTL_BSIZEHDRSIZE_MASK         0x00000F00
#define E1000_SRRCTL_BSIZEHDRSIZE_SHIFT        2  /* Shift _left_ */
#define E1000_SRRCTL_DESCTYPE_ADV_ONEBUF       0x02000000
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS 0x0A000000
#define E1000_SRRCTL_DESCTYPE_MASK             0x0E000000
#define E1000_SRRCTL_DROP_EN                   0x80000000

#define E1000_SRRCTL_BSIZEPKT_MASK             0x0000007F
#define E1000_SRRCTL_BSIZEHDR_MASK             0x00003F00

/* from igbvf/mbox.h */

#define E1000_V2PMAILBOX_REQ      0x00000001 /* Request for PF Ready bit */
#define E1000_V2PMAILBOX_ACK      0x00000002 /* Ack PF message received */
#define E1000_V2PMAILBOX_VFU      0x00000004 /* VF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFU      0x00000008 /* PF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFSTS    0x00000010 /* PF wrote a message in the MB */
#define E1000_V2PMAILBOX_PFACK    0x00000020 /* PF ack the previous VF msg */
#define E1000_V2PMAILBOX_RSTI     0x00000040 /* PF has reset indication */
#define E1000_V2PMAILBOX_RSTD     0x00000080 /* PF has indicated reset done */
#define E1000_V2PMAILBOX_R2C_BITS 0x000000B0 /* All read to clear bits */

#define E1000_VFMAILBOX_SIZE      16 /* 16 32 bit words - 64 bytes */

/*
 * If it's a E1000_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is E1000_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
/* Messages below or'd with this are the ACK */
#define E1000_VT_MSGTYPE_ACK      0x80000000
/* Messages below or'd with this are the NACK */
#define E1000_VT_MSGTYPE_NACK     0x40000000
/* Indicates that VF is still clear to send requests */
#define E1000_VT_MSGTYPE_CTS      0x20000000

/* We have a total wait time of 1s for vf mailbox posted messages */
#define E1000_VF_MBX_INIT_TIMEOUT 2000 /* retry count for mbx timeout */
#define E1000_VF_MBX_INIT_DELAY   500  /* usec delay between retries */

#define E1000_VT_MSGINFO_SHIFT    16
/* bits 23:16 are used for exra info for certain messages */
#define E1000_VT_MSGINFO_MASK     (0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_RESET            0x01 /* VF requests reset */
#define E1000_VF_SET_MAC_ADDR     0x02 /* VF requests PF to set MAC addr */
/* VF requests PF to clear all unicast MAC filters */
#define E1000_VF_MAC_FILTER_CLR   (0x01 << E1000_VT_MSGINFO_SHIFT)
/* VF requests PF to add unicast MAC filter */
#define E1000_VF_MAC_FILTER_ADD   (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_SET_MULTICAST    0x03 /* VF requests PF to set MC addr */
#define E1000_VF_SET_VLAN         0x04 /* VF requests PF to set VLAN */
#define E1000_VF_SET_LPE          0x05 /* VF requests PF to set VMOLR.LPE */

#define E1000_PF_CONTROL_MSG      0x0100 /* PF control message */

/* from igbvf/regs.h */

/* Statistics registers */
#define E1000_VFGPRC   0x00F10
#define E1000_VFGORC   0x00F18
#define E1000_VFMPRC   0x00F3C
#define E1000_VFGPTC   0x00F14
#define E1000_VFGOTC   0x00F34
#define E1000_VFGOTLBC 0x00F50
#define E1000_VFGPTLBC 0x00F44
#define E1000_VFGORLBC 0x00F48
#define E1000_VFGPRLBC 0x00F40

/* These act per VF so an array friendly macro is used */
#define E1000_V2PMAILBOX(_n) (0x00C40 + (4 * (_n)))
#define E1000_VMBMEM(_n)     (0x00800 + (64 * (_n)))

/* from igbvf/vf.h */

#define E1000_DEV_ID_82576_VF 0x10CA

/* new */

/* Receive Registers */

/* RX Descriptor Base Low; RW */
#define E1000_RDBAL(_n)    (0x0C000 + (0x40  * (_n)))
#define E1000_RDBAL_A(_n)  (0x02800 + (0x100 * (_n)))

/* RX Descriptor Base High; RW */
#define E1000_RDBAH(_n)    (0x0C004 + (0x40  * (_n)))
#define E1000_RDBAH_A(_n)  (0x02804 + (0x100 * (_n)))

/* RX Descriptor Ring Length; RW */
#define E1000_RDLEN(_n)    (0x0C008 + (0x40  * (_n)))
#define E1000_RDLEN_A(_n)  (0x02808 + (0x100 * (_n)))

/* Split and Replication Receive Control; RW */
#define E1000_SRRCTL(_n)   (0x0C00C + (0x40  * (_n)))
#define E1000_SRRCTL_A(_n) (0x0280C + (0x100 * (_n)))

/* RX Descriptor Head; RW */
#define E1000_RDH(_n)      (0x0C010 + (0x40  * (_n)))
#define E1000_RDH_A(_n)    (0x02810 + (0x100 * (_n)))

/* RX DCA Control; RW */
#define E1000_RXCTL(_n)    (0x0C014 + (0x40  * (_n)))
#define E1000_RXCTL_A(_n)  (0x02814 + (0x100 * (_n)))

/* RX Descriptor Tail; RW */
#define E1000_RDT(_n)      (0x0C018 + (0x40  * (_n)))
#define E1000_RDT_A(_n)    (0x02818 + (0x100 * (_n)))

/* RX Descriptor Control; RW */
#define E1000_RXDCTL(_n)   (0x0C028 + (0x40  * (_n)))
#define E1000_RXDCTL_A(_n) (0x02828 + (0x100 * (_n)))

/* RX Queue Drop Packet Count; RC */
#define E1000_RQDPC_A(_n)  (0x02830 + (0x100 * (_n)))

/* Transmit Registers */

/* TX Descriptor Base Low; RW */
#define E1000_TDBAL(_n)    (0x0E000 + (0x40  * (_n)))
#define E1000_TDBAL_A(_n)  (0x03800 + (0x100 * (_n)))

/* TX Descriptor Base High; RW */
#define E1000_TDBAH(_n)    (0x0E004 + (0x40  * (_n)))
#define E1000_TDBAH_A(_n)  (0x03804 + (0x100 * (_n)))

/* TX Descriptor Ring Length; RW */
#define E1000_TDLEN(_n)    (0x0E008 + (0x40  * (_n)))
#define E1000_TDLEN_A(_n)  (0x03808 + (0x100 * (_n)))

/* TX Descriptor Head; RW */
#define E1000_TDH(_n)      (0x0E010 + (0x40  * (_n)))
#define E1000_TDH_A(_n)    (0x03810 + (0x100 * (_n)))

/* TX DCA Control; RW */
#define E1000_TXCTL(_n)    (0x0E014 + (0x40  * (_n)))
#define E1000_TXCTL_A(_n)  (0x03814 + (0x100 * (_n)))

/* TX Descriptor Tail; RW */
#define E1000_TDT(_n)      (0x0E018 + (0x40  * (_n)))
#define E1000_TDT_A(_n)    (0x03818 + (0x100 * (_n)))

/* TX Descriptor Control; RW */
#define E1000_TXDCTL(_n)   (0x0E028 + (0x40  * (_n)))
#define E1000_TXDCTL_A(_n) (0x03828 + (0x100 * (_n)))

/* TX Descriptor Completion Write–Back Address Low; RW */
#define E1000_TDWBAL(_n)   (0x0E038 + (0x40  * (_n)))
#define E1000_TDWBAL_A(_n) (0x03838 + (0x100 * (_n)))

/* TX Descriptor Completion Write–Back Address High; RW */
#define E1000_TDWBAH(_n)   (0x0E03C + (0x40  * (_n)))
#define E1000_TDWBAH_A(_n) (0x0383C + (0x100 * (_n)))

#define E1000_MTA_A        0x0200

#define E1000_XDBAL_MASK (~(BIT(5) - 1)) /* TDBAL and RDBAL Registers Mask */

#define E1000_ICR_MACSEC   0x00000020 /* MACSec */
#define E1000_ICR_RX0      0x00000040 /* Receiver Overrun */
#define E1000_ICR_GPI_SDP0 0x00000800 /* General Purpose, SDP0 pin */
#define E1000_ICR_GPI_SDP1 0x00001000 /* General Purpose, SDP1 pin */
#define E1000_ICR_GPI_SDP2 0x00002000 /* General Purpose, SDP2 pin */
#define E1000_ICR_GPI_SDP3 0x00004000 /* General Purpose, SDP3 pin */
#define E1000_ICR_PTRAP    0x00008000 /* Probe Trap */
#define E1000_ICR_MNG      0x00040000 /* Management Event */
#define E1000_ICR_OMED     0x00100000 /* Other Media Energy Detected */
#define E1000_ICR_FER      0x00400000 /* Fatal Error */
#define E1000_ICR_NFER     0x00800000 /* Non Fatal Error */
#define E1000_ICR_CSRTO    0x01000000 /* CSR access Time Out Indication */
#define E1000_ICR_SCE      0x02000000 /* Storm Control Event */
#define E1000_ICR_SW_WD    0x04000000 /* Software Watchdog */

/* Extended Interrupts */

#define E1000_EICR_MSIX_MASK   0x01FFFFFF /* Bits used in MSI-X mode */
#define E1000_EICR_LEGACY_MASK 0x4000FFFF /* Bits used in non MSI-X mode */

/* Mirror VF Control (only RST bit); RW */
#define E1000_PVTCTRL(_n) (0x10000 + (_n) * 0x100)

/* Mirror Good Packets Received Count; RO */
#define E1000_PVFGPRC(_n) (0x10010 + (_n) * 0x100)

/* Mirror Good Packets Transmitted Count; RO */
#define E1000_PVFGPTC(_n) (0x10014 + (_n) * 0x100)

/* Mirror Good Octets Received Count; RO */
#define E1000_PVFGORC(_n) (0x10018 + (_n) * 0x100)

/* Mirror Extended Interrupt Cause Set; WO */
#define E1000_PVTEICS(_n) (0x10020 + (_n) * 0x100)

/* Mirror Extended Interrupt Mask Set/Read; RW */
#define E1000_PVTEIMS(_n) (0x10024 + (_n) * 0x100)

/* Mirror Extended Interrupt Mask Clear; WO */
#define E1000_PVTEIMC(_n) (0x10028 + (_n) * 0x100)

/* Mirror Extended Interrupt Auto Clear; RW */
#define E1000_PVTEIAC(_n) (0x1002C + (_n) * 0x100)

/* Mirror Extended Interrupt Auto Mask Enable; RW */
#define E1000_PVTEIAM(_n) (0x10030 + (_n) * 0x100)

/* Mirror Good Octets Transmitted Count; RO */
#define E1000_PVFGOTC(_n) (0x10034 + (_n) * 0x100)

/* Mirror Multicast Packets Received Count; RO */
#define E1000_PVFMPRC(_n) (0x1003C + (_n) * 0x100)

/* Mirror Good RX Packets loopback Count; RO */
#define E1000_PVFGPRLBC(_n) (0x10040 + (_n) * 0x100)

/* Mirror Good TX packets loopback Count; RO */
#define E1000_PVFGPTLBC(_n) (0x10044 + (_n) * 0x100)

/* Mirror Good RX Octets loopback Count; RO */
#define E1000_PVFGORLBC(_n) (0x10048 + (_n) * 0x100)

/* Mirror Good TX Octets loopback Count; RO */
#define E1000_PVFGOTLBC(_n) (0x10050 + (_n) * 0x100)

/* Mirror Extended Interrupt Cause Set; RC/W1C */
#define E1000_PVTEICR(_n) (0x10080 + (_n) * 0x100)

/*
 * These are fake addresses that, according to the specification, the device
 * is not using. They are used to distinguish between the PF and the VFs
 * accessing their VTIVAR register (which is the same address, 0x1700)
 */
#define E1000_VTIVAR      0x11700
#define E1000_VTIVAR_MISC 0x11720

#define E1000_RSS_QUEUE(reta, hash) (E1000_RETA_VAL(reta, hash) & 0x0F)

#define E1000_STATUS_IOV_MODE 0x00040000

#define E1000_STATUS_NUM_VFS_SHIFT 14

#define E1000_ADVRXD_PKT_IP4 BIT(4)
#define E1000_ADVRXD_PKT_IP6 BIT(6)
#define E1000_ADVRXD_PKT_TCP BIT(8)
#define E1000_ADVRXD_PKT_UDP BIT(9)

static inline uint8_t igb_ivar_entry_rx(uint8_t i)
{
    return i < 8 ? i * 4 : (i - 8) * 4 + 2;
}

static inline uint8_t igb_ivar_entry_tx(uint8_t i)
{
    return i < 8 ? i * 4 + 1 : (i - 8) * 4 + 3;
}

#endif
