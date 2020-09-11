/*
 * QEMU VMWARE VMXNET3 paravirtual NIC
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Tamir Shomer <tamirs@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_NET_VMXNET3_DEFS_H
#define HW_NET_VMXNET3_DEFS_H

#include "net/net.h"
#include "hw/net/vmxnet3.h"
#include "qom/object.h"

#define TYPE_VMXNET3 "vmxnet3"
typedef struct VMXNET3State VMXNET3State;
DECLARE_INSTANCE_CHECKER(VMXNET3State, VMXNET3,
                         TYPE_VMXNET3)

/* Device state and helper functions */
#define VMXNET3_RX_RINGS_PER_QUEUE (2)

/* Cyclic ring abstraction */
typedef struct {
    hwaddr pa;
    uint32_t size;
    uint32_t cell_size;
    uint32_t next;
    uint8_t gen;
} Vmxnet3Ring;

typedef struct {
    Vmxnet3Ring tx_ring;
    Vmxnet3Ring comp_ring;

    uint8_t intr_idx;
    hwaddr tx_stats_pa;
    struct UPT1_TxStats txq_stats;
} Vmxnet3TxqDescr;

typedef struct {
    Vmxnet3Ring rx_ring[VMXNET3_RX_RINGS_PER_QUEUE];
    Vmxnet3Ring comp_ring;
    uint8_t intr_idx;
    hwaddr rx_stats_pa;
    struct UPT1_RxStats rxq_stats;
} Vmxnet3RxqDescr;

typedef struct {
    bool is_masked;
    bool is_pending;
    bool is_asserted;
} Vmxnet3IntState;

struct VMXNET3State {
        PCIDevice parent_obj;
        NICState *nic;
        NICConf conf;
        MemoryRegion bar0;
        MemoryRegion bar1;
        MemoryRegion msix_bar;

        Vmxnet3RxqDescr rxq_descr[VMXNET3_DEVICE_MAX_RX_QUEUES];
        Vmxnet3TxqDescr txq_descr[VMXNET3_DEVICE_MAX_TX_QUEUES];

        /* Whether MSI-X support was installed successfully */
        bool msix_used;
        hwaddr drv_shmem;
        hwaddr temp_shared_guest_driver_memory;

        uint8_t txq_num;

        /* This boolean tells whether RX packet being indicated has to */
        /* be split into head and body chunks from different RX rings  */
        bool rx_packets_compound;

        bool rx_vlan_stripping;
        bool lro_supported;

        uint8_t rxq_num;

        /* Network MTU */
        uint32_t mtu;

        /* Maximum number of fragments for indicated TX packets */
        uint32_t max_tx_frags;

        /* Maximum number of fragments for indicated RX packets */
        uint16_t max_rx_frags;

        /* Index for events interrupt */
        uint8_t event_int_idx;

        /* Whether automatic interrupts masking enabled */
        bool auto_int_masking;

        bool peer_has_vhdr;

        /* TX packets to QEMU interface */
        struct NetTxPkt *tx_pkt;
        uint32_t offload_mode;
        uint32_t cso_or_gso_size;
        uint16_t tci;
        bool needs_vlan;

        struct NetRxPkt *rx_pkt;

        bool tx_sop;
        bool skip_current_tx_pkt;

        uint32_t device_active;
        uint32_t last_command;

        uint32_t link_status_and_speed;

        Vmxnet3IntState interrupt_states[VMXNET3_MAX_INTRS];

        uint32_t temp_mac;   /* To store the low part first */

        MACAddr perm_mac;
        uint32_t vlan_table[VMXNET3_VFT_SIZE];
        uint32_t rx_mode;
        MACAddr *mcast_list;
        uint32_t mcast_list_len;
        uint32_t mcast_list_buff_size; /* needed for live migration. */

        /* Compatibility flags for migration */
        uint32_t compat_flags;
};

#endif
