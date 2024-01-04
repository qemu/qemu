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
 *
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "net/tap.h"
#include "net/checksum.h"
#include "sysemu/sysemu.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/pci/msix.h"
#include "hw/pci/msi.h"
#include "migration/register.h"
#include "migration/vmstate.h"

#include "vmxnet3.h"
#include "vmxnet3_defs.h"
#include "vmxnet_debug.h"
#include "vmware_utils.h"
#include "net_tx_pkt.h"
#include "net_rx_pkt.h"
#include "qom/object.h"

#define PCI_DEVICE_ID_VMWARE_VMXNET3_REVISION 0x1
#define VMXNET3_MSIX_BAR_SIZE 0x2000

/* Compatibility flags for migration */
#define VMXNET3_COMPAT_FLAG_OLD_MSI_OFFSETS_BIT 0
#define VMXNET3_COMPAT_FLAG_OLD_MSI_OFFSETS \
    (1 << VMXNET3_COMPAT_FLAG_OLD_MSI_OFFSETS_BIT)
#define VMXNET3_COMPAT_FLAG_DISABLE_PCIE_BIT 1
#define VMXNET3_COMPAT_FLAG_DISABLE_PCIE \
    (1 << VMXNET3_COMPAT_FLAG_DISABLE_PCIE_BIT)

#define VMXNET3_EXP_EP_OFFSET (0x48)
#define VMXNET3_MSI_OFFSET(s) \
    ((s)->compat_flags & VMXNET3_COMPAT_FLAG_OLD_MSI_OFFSETS ? 0x50 : 0x84)
#define VMXNET3_MSIX_OFFSET(s) \
    ((s)->compat_flags & VMXNET3_COMPAT_FLAG_OLD_MSI_OFFSETS ? 0 : 0x9c)
#define VMXNET3_DSN_OFFSET     (0x100)

#define VMXNET3_BAR0_IDX      (0)
#define VMXNET3_BAR1_IDX      (1)
#define VMXNET3_MSIX_BAR_IDX  (2)

#define VMXNET3_OFF_MSIX_TABLE (0x000)
#define VMXNET3_OFF_MSIX_PBA(s) \
    ((s)->compat_flags & VMXNET3_COMPAT_FLAG_OLD_MSI_OFFSETS ? 0x800 : 0x1000)

/* Link speed in Mbps should be shifted by 16 */
#define VMXNET3_LINK_SPEED      (1000 << 16)

/* Link status: 1 - up, 0 - down. */
#define VMXNET3_LINK_STATUS_UP  0x1

/* Least significant bit should be set for revision and version */
#define VMXNET3_UPT_REVISION      0x1
#define VMXNET3_DEVICE_REVISION   0x1

/* Number of interrupt vectors for non-MSIx modes */
#define VMXNET3_MAX_NMSIX_INTRS   (1)

/* Macros for rings descriptors access */
#define VMXNET3_READ_TX_QUEUE_DESCR8(_d, dpa, field) \
    (vmw_shmem_ld8(_d, dpa + offsetof(struct Vmxnet3_TxQueueDesc, field)))

#define VMXNET3_WRITE_TX_QUEUE_DESCR8(_d, dpa, field, value) \
    (vmw_shmem_st8(_d, dpa + offsetof(struct Vmxnet3_TxQueueDesc, field, value)))

#define VMXNET3_READ_TX_QUEUE_DESCR32(_d, dpa, field) \
    (vmw_shmem_ld32(_d, dpa + offsetof(struct Vmxnet3_TxQueueDesc, field)))

#define VMXNET3_WRITE_TX_QUEUE_DESCR32(_d, dpa, field, value) \
    (vmw_shmem_st32(_d, dpa + offsetof(struct Vmxnet3_TxQueueDesc, field), value))

#define VMXNET3_READ_TX_QUEUE_DESCR64(_d, dpa, field) \
    (vmw_shmem_ld64(_d, dpa + offsetof(struct Vmxnet3_TxQueueDesc, field)))

#define VMXNET3_WRITE_TX_QUEUE_DESCR64(_d, dpa, field, value) \
    (vmw_shmem_st64(_d, dpa + offsetof(struct Vmxnet3_TxQueueDesc, field), value))

#define VMXNET3_READ_RX_QUEUE_DESCR64(_d, dpa, field) \
    (vmw_shmem_ld64(_d, dpa + offsetof(struct Vmxnet3_RxQueueDesc, field)))

#define VMXNET3_READ_RX_QUEUE_DESCR32(_d, dpa, field) \
    (vmw_shmem_ld32(_d, dpa + offsetof(struct Vmxnet3_RxQueueDesc, field)))

#define VMXNET3_WRITE_RX_QUEUE_DESCR64(_d, dpa, field, value) \
    (vmw_shmem_st64(_d, dpa + offsetof(struct Vmxnet3_RxQueueDesc, field), value))

#define VMXNET3_WRITE_RX_QUEUE_DESCR8(_d, dpa, field, value) \
    (vmw_shmem_st8(_d, dpa + offsetof(struct Vmxnet3_RxQueueDesc, field), value))

/* Macros for guest driver shared area access */
#define VMXNET3_READ_DRV_SHARED64(_d, shpa, field) \
    (vmw_shmem_ld64(_d, shpa + offsetof(struct Vmxnet3_DriverShared, field)))

#define VMXNET3_READ_DRV_SHARED32(_d, shpa, field) \
    (vmw_shmem_ld32(_d, shpa + offsetof(struct Vmxnet3_DriverShared, field)))

#define VMXNET3_WRITE_DRV_SHARED32(_d, shpa, field, val) \
    (vmw_shmem_st32(_d, shpa + offsetof(struct Vmxnet3_DriverShared, field), val))

#define VMXNET3_READ_DRV_SHARED16(_d, shpa, field) \
    (vmw_shmem_ld16(_d, shpa + offsetof(struct Vmxnet3_DriverShared, field)))

#define VMXNET3_READ_DRV_SHARED8(_d, shpa, field) \
    (vmw_shmem_ld8(_d, shpa + offsetof(struct Vmxnet3_DriverShared, field)))

#define VMXNET3_READ_DRV_SHARED(_d, shpa, field, b, l) \
    (vmw_shmem_read(_d, shpa + offsetof(struct Vmxnet3_DriverShared, field), b, l))

#define VMXNET_FLAG_IS_SET(field, flag) (((field) & (flag)) == (flag))

struct VMXNET3Class {
    PCIDeviceClass parent_class;
    DeviceRealize parent_dc_realize;
};
typedef struct VMXNET3Class VMXNET3Class;

DECLARE_CLASS_CHECKERS(VMXNET3Class, VMXNET3_DEVICE,
                       TYPE_VMXNET3)

static inline void vmxnet3_ring_init(PCIDevice *d,
                                     Vmxnet3Ring *ring,
                                     hwaddr pa,
                                     uint32_t size,
                                     uint32_t cell_size,
                                     bool zero_region)
{
    ring->pa = pa;
    ring->size = size;
    ring->cell_size = cell_size;
    ring->gen = VMXNET3_INIT_GEN;
    ring->next = 0;

    if (zero_region) {
        vmw_shmem_set(d, pa, 0, size * cell_size);
    }
}

#define VMXNET3_RING_DUMP(macro, ring_name, ridx, r)                         \
    macro("%s#%d: base %" PRIx64 " size %u cell_size %u gen %d next %u",  \
          (ring_name), (ridx),                                               \
          (r)->pa, (r)->size, (r)->cell_size, (r)->gen, (r)->next)

static inline void vmxnet3_ring_inc(Vmxnet3Ring *ring)
{
    if (++ring->next >= ring->size) {
        ring->next = 0;
        ring->gen ^= 1;
    }
}

static inline void vmxnet3_ring_dec(Vmxnet3Ring *ring)
{
    if (ring->next-- == 0) {
        ring->next = ring->size - 1;
        ring->gen ^= 1;
    }
}

static inline hwaddr vmxnet3_ring_curr_cell_pa(Vmxnet3Ring *ring)
{
    return ring->pa + ring->next * ring->cell_size;
}

static inline void vmxnet3_ring_read_curr_cell(PCIDevice *d, Vmxnet3Ring *ring,
                                               void *buff)
{
    vmw_shmem_read(d, vmxnet3_ring_curr_cell_pa(ring), buff, ring->cell_size);
}

static inline void vmxnet3_ring_write_curr_cell(PCIDevice *d, Vmxnet3Ring *ring,
                                                void *buff)
{
    vmw_shmem_write(d, vmxnet3_ring_curr_cell_pa(ring), buff, ring->cell_size);
}

static inline size_t vmxnet3_ring_curr_cell_idx(Vmxnet3Ring *ring)
{
    return ring->next;
}

static inline uint8_t vmxnet3_ring_curr_gen(Vmxnet3Ring *ring)
{
    return ring->gen;
}

/* Debug trace-related functions */
static inline void
vmxnet3_dump_tx_descr(struct Vmxnet3_TxDesc *descr)
{
    VMW_PKPRN("TX DESCR: "
              "addr %" PRIx64 ", len: %d, gen: %d, rsvd: %d, "
              "dtype: %d, ext1: %d, msscof: %d, hlen: %d, om: %d, "
              "eop: %d, cq: %d, ext2: %d, ti: %d, tci: %d",
              descr->addr, descr->len, descr->gen, descr->rsvd,
              descr->dtype, descr->ext1, descr->msscof, descr->hlen, descr->om,
              descr->eop, descr->cq, descr->ext2, descr->ti, descr->tci);
}

static inline void
vmxnet3_dump_virt_hdr(struct virtio_net_hdr *vhdr)
{
    VMW_PKPRN("VHDR: flags 0x%x, gso_type: 0x%x, hdr_len: %d, gso_size: %d, "
              "csum_start: %d, csum_offset: %d",
              vhdr->flags, vhdr->gso_type, vhdr->hdr_len, vhdr->gso_size,
              vhdr->csum_start, vhdr->csum_offset);
}

static inline void
vmxnet3_dump_rx_descr(struct Vmxnet3_RxDesc *descr)
{
    VMW_PKPRN("RX DESCR: addr %" PRIx64 ", len: %d, gen: %d, rsvd: %d, "
              "dtype: %d, ext1: %d, btype: %d",
              descr->addr, descr->len, descr->gen,
              descr->rsvd, descr->dtype, descr->ext1, descr->btype);
}

/* Interrupt management */

/*
 * This function returns sign whether interrupt line is in asserted state
 * This depends on the type of interrupt used. For INTX interrupt line will
 * be asserted until explicit deassertion, for MSI(X) interrupt line will
 * be deasserted automatically due to notification semantics of the MSI(X)
 * interrupts
 */
static bool _vmxnet3_assert_interrupt_line(VMXNET3State *s, uint32_t int_idx)
{
    PCIDevice *d = PCI_DEVICE(s);

    if (s->msix_used && msix_enabled(d)) {
        VMW_IRPRN("Sending MSI-X notification for vector %u", int_idx);
        msix_notify(d, int_idx);
        return false;
    }
    if (msi_enabled(d)) {
        VMW_IRPRN("Sending MSI notification for vector %u", int_idx);
        msi_notify(d, int_idx);
        return false;
    }

    VMW_IRPRN("Asserting line for interrupt %u", int_idx);
    pci_irq_assert(d);
    return true;
}

static void _vmxnet3_deassert_interrupt_line(VMXNET3State *s, int lidx)
{
    PCIDevice *d = PCI_DEVICE(s);

    /*
     * This function should never be called for MSI(X) interrupts
     * because deassertion never required for message interrupts
     */
    assert(!s->msix_used || !msix_enabled(d));
    /*
     * This function should never be called for MSI(X) interrupts
     * because deassertion never required for message interrupts
     */
    assert(!msi_enabled(d));

    VMW_IRPRN("Deasserting line for interrupt %u", lidx);
    pci_irq_deassert(d);
}

static void vmxnet3_update_interrupt_line_state(VMXNET3State *s, int lidx)
{
    if (!s->interrupt_states[lidx].is_pending &&
       s->interrupt_states[lidx].is_asserted) {
        VMW_IRPRN("New interrupt line state for index %d is DOWN", lidx);
        _vmxnet3_deassert_interrupt_line(s, lidx);
        s->interrupt_states[lidx].is_asserted = false;
        return;
    }

    if (s->interrupt_states[lidx].is_pending &&
       !s->interrupt_states[lidx].is_masked &&
       !s->interrupt_states[lidx].is_asserted) {
        VMW_IRPRN("New interrupt line state for index %d is UP", lidx);
        s->interrupt_states[lidx].is_asserted =
            _vmxnet3_assert_interrupt_line(s, lidx);
        s->interrupt_states[lidx].is_pending = false;
        return;
    }
}

static void vmxnet3_trigger_interrupt(VMXNET3State *s, int lidx)
{
    PCIDevice *d = PCI_DEVICE(s);
    s->interrupt_states[lidx].is_pending = true;
    vmxnet3_update_interrupt_line_state(s, lidx);

    if (s->msix_used && msix_enabled(d) && s->auto_int_masking) {
        goto do_automask;
    }

    if (msi_enabled(d) && s->auto_int_masking) {
        goto do_automask;
    }

    return;

do_automask:
    s->interrupt_states[lidx].is_masked = true;
    vmxnet3_update_interrupt_line_state(s, lidx);
}

static bool vmxnet3_interrupt_asserted(VMXNET3State *s, int lidx)
{
    return s->interrupt_states[lidx].is_asserted;
}

static void vmxnet3_clear_interrupt(VMXNET3State *s, int int_idx)
{
    s->interrupt_states[int_idx].is_pending = false;
    if (s->auto_int_masking) {
        s->interrupt_states[int_idx].is_masked = true;
    }
    vmxnet3_update_interrupt_line_state(s, int_idx);
}

static void
vmxnet3_on_interrupt_mask_changed(VMXNET3State *s, int lidx, bool is_masked)
{
    s->interrupt_states[lidx].is_masked = is_masked;
    vmxnet3_update_interrupt_line_state(s, lidx);
}

static bool vmxnet3_verify_driver_magic(PCIDevice *d, hwaddr dshmem)
{
    return (VMXNET3_READ_DRV_SHARED32(d, dshmem, magic) == VMXNET3_REV1_MAGIC);
}

#define VMXNET3_GET_BYTE(x, byte_num) (((x) >> (byte_num)*8) & 0xFF)
#define VMXNET3_MAKE_BYTE(byte_num, val) \
    (((uint32_t)((val) & 0xFF)) << (byte_num)*8)

static void vmxnet3_set_variable_mac(VMXNET3State *s, uint32_t h, uint32_t l)
{
    s->conf.macaddr.a[0] = VMXNET3_GET_BYTE(l,  0);
    s->conf.macaddr.a[1] = VMXNET3_GET_BYTE(l,  1);
    s->conf.macaddr.a[2] = VMXNET3_GET_BYTE(l,  2);
    s->conf.macaddr.a[3] = VMXNET3_GET_BYTE(l,  3);
    s->conf.macaddr.a[4] = VMXNET3_GET_BYTE(h, 0);
    s->conf.macaddr.a[5] = VMXNET3_GET_BYTE(h, 1);

    VMW_CFPRN("Variable MAC: " MAC_FMT, MAC_ARG(s->conf.macaddr.a));

    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static uint64_t vmxnet3_get_mac_low(MACAddr *addr)
{
    return VMXNET3_MAKE_BYTE(0, addr->a[0]) |
           VMXNET3_MAKE_BYTE(1, addr->a[1]) |
           VMXNET3_MAKE_BYTE(2, addr->a[2]) |
           VMXNET3_MAKE_BYTE(3, addr->a[3]);
}

static uint64_t vmxnet3_get_mac_high(MACAddr *addr)
{
    return VMXNET3_MAKE_BYTE(0, addr->a[4]) |
           VMXNET3_MAKE_BYTE(1, addr->a[5]);
}

static void
vmxnet3_inc_tx_consumption_counter(VMXNET3State *s, int qidx)
{
    vmxnet3_ring_inc(&s->txq_descr[qidx].tx_ring);
}

static inline void
vmxnet3_inc_rx_consumption_counter(VMXNET3State *s, int qidx, int ridx)
{
    vmxnet3_ring_inc(&s->rxq_descr[qidx].rx_ring[ridx]);
}

static inline void
vmxnet3_inc_tx_completion_counter(VMXNET3State *s, int qidx)
{
    vmxnet3_ring_inc(&s->txq_descr[qidx].comp_ring);
}

static void
vmxnet3_inc_rx_completion_counter(VMXNET3State *s, int qidx)
{
    vmxnet3_ring_inc(&s->rxq_descr[qidx].comp_ring);
}

static void
vmxnet3_dec_rx_completion_counter(VMXNET3State *s, int qidx)
{
    vmxnet3_ring_dec(&s->rxq_descr[qidx].comp_ring);
}

static void vmxnet3_complete_packet(VMXNET3State *s, int qidx, uint32_t tx_ridx)
{
    struct Vmxnet3_TxCompDesc txcq_descr;
    PCIDevice *d = PCI_DEVICE(s);

    VMXNET3_RING_DUMP(VMW_RIPRN, "TXC", qidx, &s->txq_descr[qidx].comp_ring);

    memset(&txcq_descr, 0, sizeof(txcq_descr));
    txcq_descr.txdIdx = tx_ridx;
    txcq_descr.gen = vmxnet3_ring_curr_gen(&s->txq_descr[qidx].comp_ring);
    txcq_descr.val1 = cpu_to_le32(txcq_descr.val1);
    txcq_descr.val2 = cpu_to_le32(txcq_descr.val2);
    vmxnet3_ring_write_curr_cell(d, &s->txq_descr[qidx].comp_ring, &txcq_descr);

    /* Flush changes in TX descriptor before changing the counter value */
    smp_wmb();

    vmxnet3_inc_tx_completion_counter(s, qidx);
    vmxnet3_trigger_interrupt(s, s->txq_descr[qidx].intr_idx);
}

static bool
vmxnet3_setup_tx_offloads(VMXNET3State *s)
{
    switch (s->offload_mode) {
    case VMXNET3_OM_NONE:
        return net_tx_pkt_build_vheader(s->tx_pkt, false, false, 0);

    case VMXNET3_OM_CSUM:
        VMW_PKPRN("L4 CSO requested\n");
        return net_tx_pkt_build_vheader(s->tx_pkt, false, true, 0);

    case VMXNET3_OM_TSO:
        VMW_PKPRN("GSO offload requested.");
        if (!net_tx_pkt_build_vheader(s->tx_pkt, true, true,
            s->cso_or_gso_size)) {
            return false;
        }
        net_tx_pkt_update_ip_checksums(s->tx_pkt);
        break;

    default:
        g_assert_not_reached();
        return false;
    }

    return true;
}

static void
vmxnet3_tx_retrieve_metadata(VMXNET3State *s,
                             const struct Vmxnet3_TxDesc *txd)
{
    s->offload_mode = txd->om;
    s->cso_or_gso_size = txd->msscof;
    s->tci = txd->tci;
    s->needs_vlan = txd->ti;
}

typedef enum {
    VMXNET3_PKT_STATUS_OK,
    VMXNET3_PKT_STATUS_ERROR,
    VMXNET3_PKT_STATUS_DISCARD,/* only for tx */
    VMXNET3_PKT_STATUS_OUT_OF_BUF /* only for rx */
} Vmxnet3PktStatus;

static void
vmxnet3_on_tx_done_update_stats(VMXNET3State *s, int qidx,
    Vmxnet3PktStatus status)
{
    size_t tot_len = net_tx_pkt_get_total_len(s->tx_pkt);
    struct UPT1_TxStats *stats = &s->txq_descr[qidx].txq_stats;

    switch (status) {
    case VMXNET3_PKT_STATUS_OK:
        switch (net_tx_pkt_get_packet_type(s->tx_pkt)) {
        case ETH_PKT_BCAST:
            stats->bcastPktsTxOK++;
            stats->bcastBytesTxOK += tot_len;
            break;
        case ETH_PKT_MCAST:
            stats->mcastPktsTxOK++;
            stats->mcastBytesTxOK += tot_len;
            break;
        case ETH_PKT_UCAST:
            stats->ucastPktsTxOK++;
            stats->ucastBytesTxOK += tot_len;
            break;
        default:
            g_assert_not_reached();
        }

        if (s->offload_mode == VMXNET3_OM_TSO) {
            /*
             * According to VMWARE headers this statistic is a number
             * of packets after segmentation but since we don't have
             * this information in QEMU model, the best we can do is to
             * provide number of non-segmented packets
             */
            stats->TSOPktsTxOK++;
            stats->TSOBytesTxOK += tot_len;
        }
        break;

    case VMXNET3_PKT_STATUS_DISCARD:
        stats->pktsTxDiscard++;
        break;

    case VMXNET3_PKT_STATUS_ERROR:
        stats->pktsTxError++;
        break;

    default:
        g_assert_not_reached();
    }
}

static void
vmxnet3_on_rx_done_update_stats(VMXNET3State *s,
                                int qidx,
                                Vmxnet3PktStatus status)
{
    struct UPT1_RxStats *stats = &s->rxq_descr[qidx].rxq_stats;
    size_t tot_len = net_rx_pkt_get_total_len(s->rx_pkt);

    switch (status) {
    case VMXNET3_PKT_STATUS_OUT_OF_BUF:
        stats->pktsRxOutOfBuf++;
        break;

    case VMXNET3_PKT_STATUS_ERROR:
        stats->pktsRxError++;
        break;
    case VMXNET3_PKT_STATUS_OK:
        switch (net_rx_pkt_get_packet_type(s->rx_pkt)) {
        case ETH_PKT_BCAST:
            stats->bcastPktsRxOK++;
            stats->bcastBytesRxOK += tot_len;
            break;
        case ETH_PKT_MCAST:
            stats->mcastPktsRxOK++;
            stats->mcastBytesRxOK += tot_len;
            break;
        case ETH_PKT_UCAST:
            stats->ucastPktsRxOK++;
            stats->ucastBytesRxOK += tot_len;
            break;
        default:
            g_assert_not_reached();
        }

        if (tot_len > s->mtu) {
            stats->LROPktsRxOK++;
            stats->LROBytesRxOK += tot_len;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static inline void
vmxnet3_ring_read_curr_txdesc(PCIDevice *pcidev, Vmxnet3Ring *ring,
                              struct Vmxnet3_TxDesc *txd)
{
    vmxnet3_ring_read_curr_cell(pcidev, ring, txd);
    txd->addr = le64_to_cpu(txd->addr);
    txd->val1 = le32_to_cpu(txd->val1);
    txd->val2 = le32_to_cpu(txd->val2);
}

static inline bool
vmxnet3_pop_next_tx_descr(VMXNET3State *s,
                          int qidx,
                          struct Vmxnet3_TxDesc *txd,
                          uint32_t *descr_idx)
{
    Vmxnet3Ring *ring = &s->txq_descr[qidx].tx_ring;
    PCIDevice *d = PCI_DEVICE(s);

    vmxnet3_ring_read_curr_txdesc(d, ring, txd);
    if (txd->gen == vmxnet3_ring_curr_gen(ring)) {
        /* Only read after generation field verification */
        smp_rmb();
        /* Re-read to be sure we got the latest version */
        vmxnet3_ring_read_curr_txdesc(d, ring, txd);
        VMXNET3_RING_DUMP(VMW_RIPRN, "TX", qidx, ring);
        *descr_idx = vmxnet3_ring_curr_cell_idx(ring);
        vmxnet3_inc_tx_consumption_counter(s, qidx);
        return true;
    }

    return false;
}

static bool
vmxnet3_send_packet(VMXNET3State *s, uint32_t qidx)
{
    Vmxnet3PktStatus status = VMXNET3_PKT_STATUS_OK;

    if (!vmxnet3_setup_tx_offloads(s)) {
        status = VMXNET3_PKT_STATUS_ERROR;
        goto func_exit;
    }

    /* debug prints */
    vmxnet3_dump_virt_hdr(net_tx_pkt_get_vhdr(s->tx_pkt));
    net_tx_pkt_dump(s->tx_pkt);

    if (!net_tx_pkt_send(s->tx_pkt, qemu_get_queue(s->nic))) {
        status = VMXNET3_PKT_STATUS_DISCARD;
        goto func_exit;
    }

func_exit:
    vmxnet3_on_tx_done_update_stats(s, qidx, status);
    return (status == VMXNET3_PKT_STATUS_OK);
}

static void vmxnet3_process_tx_queue(VMXNET3State *s, int qidx)
{
    struct Vmxnet3_TxDesc txd;
    uint32_t txd_idx;
    uint32_t data_len;
    hwaddr data_pa;

    for (;;) {
        if (!vmxnet3_pop_next_tx_descr(s, qidx, &txd, &txd_idx)) {
            break;
        }

        vmxnet3_dump_tx_descr(&txd);

        if (!s->skip_current_tx_pkt) {
            data_len = (txd.len > 0) ? txd.len : VMXNET3_MAX_TX_BUF_SIZE;
            data_pa = txd.addr;

            if (!net_tx_pkt_add_raw_fragment_pci(s->tx_pkt, PCI_DEVICE(s),
                                                 data_pa, data_len)) {
                s->skip_current_tx_pkt = true;
            }
        }

        if (s->tx_sop) {
            vmxnet3_tx_retrieve_metadata(s, &txd);
            s->tx_sop = false;
        }

        if (txd.eop) {
            if (!s->skip_current_tx_pkt && net_tx_pkt_parse(s->tx_pkt)) {
                if (s->needs_vlan) {
                    net_tx_pkt_setup_vlan_header(s->tx_pkt, s->tci);
                }

                vmxnet3_send_packet(s, qidx);
            } else {
                vmxnet3_on_tx_done_update_stats(s, qidx,
                                                VMXNET3_PKT_STATUS_ERROR);
            }

            vmxnet3_complete_packet(s, qidx, txd_idx);
            s->tx_sop = true;
            s->skip_current_tx_pkt = false;
            net_tx_pkt_reset(s->tx_pkt,
                             net_tx_pkt_unmap_frag_pci, PCI_DEVICE(s));
        }
    }

    net_tx_pkt_reset(s->tx_pkt, net_tx_pkt_unmap_frag_pci, PCI_DEVICE(s));
}

static inline void
vmxnet3_read_next_rx_descr(VMXNET3State *s, int qidx, int ridx,
                           struct Vmxnet3_RxDesc *dbuf, uint32_t *didx)
{
    PCIDevice *d = PCI_DEVICE(s);

    Vmxnet3Ring *ring = &s->rxq_descr[qidx].rx_ring[ridx];
    *didx = vmxnet3_ring_curr_cell_idx(ring);
    vmxnet3_ring_read_curr_cell(d, ring, dbuf);
    dbuf->addr = le64_to_cpu(dbuf->addr);
    dbuf->val1 = le32_to_cpu(dbuf->val1);
    dbuf->ext1 = le32_to_cpu(dbuf->ext1);
}

static inline uint8_t
vmxnet3_get_rx_ring_gen(VMXNET3State *s, int qidx, int ridx)
{
    return s->rxq_descr[qidx].rx_ring[ridx].gen;
}

static inline hwaddr
vmxnet3_pop_rxc_descr(VMXNET3State *s, int qidx, uint32_t *descr_gen)
{
    uint8_t ring_gen;
    struct Vmxnet3_RxCompDesc rxcd;

    hwaddr daddr =
        vmxnet3_ring_curr_cell_pa(&s->rxq_descr[qidx].comp_ring);

    pci_dma_read(PCI_DEVICE(s),
                 daddr, &rxcd, sizeof(struct Vmxnet3_RxCompDesc));
    rxcd.val1 = le32_to_cpu(rxcd.val1);
    rxcd.val2 = le32_to_cpu(rxcd.val2);
    rxcd.val3 = le32_to_cpu(rxcd.val3);
    ring_gen = vmxnet3_ring_curr_gen(&s->rxq_descr[qidx].comp_ring);

    if (rxcd.gen != ring_gen) {
        *descr_gen = ring_gen;
        vmxnet3_inc_rx_completion_counter(s, qidx);
        return daddr;
    }

    return 0;
}

static inline void
vmxnet3_revert_rxc_descr(VMXNET3State *s, int qidx)
{
    vmxnet3_dec_rx_completion_counter(s, qidx);
}

#define RXQ_IDX      (0)
#define RX_HEAD_BODY_RING (0)
#define RX_BODY_ONLY_RING (1)

static bool
vmxnet3_get_next_head_rx_descr(VMXNET3State *s,
                               struct Vmxnet3_RxDesc *descr_buf,
                               uint32_t *descr_idx,
                               uint32_t *ridx)
{
    for (;;) {
        uint32_t ring_gen;
        vmxnet3_read_next_rx_descr(s, RXQ_IDX, RX_HEAD_BODY_RING,
                                   descr_buf, descr_idx);

        /* If no more free descriptors - return */
        ring_gen = vmxnet3_get_rx_ring_gen(s, RXQ_IDX, RX_HEAD_BODY_RING);
        if (descr_buf->gen != ring_gen) {
            return false;
        }

        /* Only read after generation field verification */
        smp_rmb();
        /* Re-read to be sure we got the latest version */
        vmxnet3_read_next_rx_descr(s, RXQ_IDX, RX_HEAD_BODY_RING,
                                   descr_buf, descr_idx);

        /* Mark current descriptor as used/skipped */
        vmxnet3_inc_rx_consumption_counter(s, RXQ_IDX, RX_HEAD_BODY_RING);

        /* If this is what we are looking for - return */
        if (descr_buf->btype == VMXNET3_RXD_BTYPE_HEAD) {
            *ridx = RX_HEAD_BODY_RING;
            return true;
        }
    }
}

static bool
vmxnet3_get_next_body_rx_descr(VMXNET3State *s,
                               struct Vmxnet3_RxDesc *d,
                               uint32_t *didx,
                               uint32_t *ridx)
{
    vmxnet3_read_next_rx_descr(s, RXQ_IDX, RX_HEAD_BODY_RING, d, didx);

    /* Try to find corresponding descriptor in head/body ring */
    if (d->gen == vmxnet3_get_rx_ring_gen(s, RXQ_IDX, RX_HEAD_BODY_RING)) {
        /* Only read after generation field verification */
        smp_rmb();
        /* Re-read to be sure we got the latest version */
        vmxnet3_read_next_rx_descr(s, RXQ_IDX, RX_HEAD_BODY_RING, d, didx);
        if (d->btype == VMXNET3_RXD_BTYPE_BODY) {
            vmxnet3_inc_rx_consumption_counter(s, RXQ_IDX, RX_HEAD_BODY_RING);
            *ridx = RX_HEAD_BODY_RING;
            return true;
        }
    }

    /*
     * If there is no free descriptors on head/body ring or next free
     * descriptor is a head descriptor switch to body only ring
     */
    vmxnet3_read_next_rx_descr(s, RXQ_IDX, RX_BODY_ONLY_RING, d, didx);

    /* If no more free descriptors - return */
    if (d->gen == vmxnet3_get_rx_ring_gen(s, RXQ_IDX, RX_BODY_ONLY_RING)) {
        /* Only read after generation field verification */
        smp_rmb();
        /* Re-read to be sure we got the latest version */
        vmxnet3_read_next_rx_descr(s, RXQ_IDX, RX_BODY_ONLY_RING, d, didx);
        assert(d->btype == VMXNET3_RXD_BTYPE_BODY);
        *ridx = RX_BODY_ONLY_RING;
        vmxnet3_inc_rx_consumption_counter(s, RXQ_IDX, RX_BODY_ONLY_RING);
        return true;
    }

    return false;
}

static inline bool
vmxnet3_get_next_rx_descr(VMXNET3State *s, bool is_head,
                          struct Vmxnet3_RxDesc *descr_buf,
                          uint32_t *descr_idx,
                          uint32_t *ridx)
{
    if (is_head || !s->rx_packets_compound) {
        return vmxnet3_get_next_head_rx_descr(s, descr_buf, descr_idx, ridx);
    } else {
        return vmxnet3_get_next_body_rx_descr(s, descr_buf, descr_idx, ridx);
    }
}

/* In case packet was csum offloaded (either NEEDS_CSUM or DATA_VALID),
 * the implementation always passes an RxCompDesc with a "Checksum
 * calculated and found correct" to the OS (cnc=0 and tuc=1, see
 * vmxnet3_rx_update_descr). This emulates the observed ESXi behavior.
 *
 * Therefore, if packet has the NEEDS_CSUM set, we must calculate
 * and place a fully computed checksum into the tcp/udp header.
 * Otherwise, the OS driver will receive a checksum-correct indication
 * (CHECKSUM_UNNECESSARY), but with the actual tcp/udp checksum field
 * having just the pseudo header csum value.
 *
 * While this is not a problem if packet is destined for local delivery,
 * in the case the host OS performs forwarding, it will forward an
 * incorrectly checksummed packet.
 */
static void vmxnet3_rx_need_csum_calculate(struct NetRxPkt *pkt,
                                           const void *pkt_data,
                                           size_t pkt_len)
{
    struct virtio_net_hdr *vhdr;
    bool hasip4, hasip6;
    EthL4HdrProto l4hdr_proto;
    uint8_t *data;
    int len;

    vhdr = net_rx_pkt_get_vhdr(pkt);
    if (!VMXNET_FLAG_IS_SET(vhdr->flags, VIRTIO_NET_HDR_F_NEEDS_CSUM)) {
        return;
    }

    net_rx_pkt_get_protocols(pkt, &hasip4, &hasip6, &l4hdr_proto);
    if (!(hasip4 || hasip6) ||
        (l4hdr_proto != ETH_L4_HDR_PROTO_TCP &&
         l4hdr_proto != ETH_L4_HDR_PROTO_UDP)) {
        return;
    }

    vmxnet3_dump_virt_hdr(vhdr);

    /* Validate packet len: csum_start + scum_offset + length of csum field */
    if (pkt_len < (vhdr->csum_start + vhdr->csum_offset + 2)) {
        VMW_PKPRN("packet len:%zu < csum_start(%d) + csum_offset(%d) + 2, "
                  "cannot calculate checksum",
                  pkt_len, vhdr->csum_start, vhdr->csum_offset);
        return;
    }

    data = (uint8_t *)pkt_data + vhdr->csum_start;
    len = pkt_len - vhdr->csum_start;
    /* Put the checksum obtained into the packet */
    stw_be_p(data + vhdr->csum_offset,
             net_checksum_finish_nozero(net_checksum_add(len, data)));

    vhdr->flags &= ~VIRTIO_NET_HDR_F_NEEDS_CSUM;
    vhdr->flags |= VIRTIO_NET_HDR_F_DATA_VALID;
}

static void vmxnet3_rx_update_descr(struct NetRxPkt *pkt,
    struct Vmxnet3_RxCompDesc *rxcd)
{
    int csum_ok, is_gso;
    bool hasip4, hasip6;
    EthL4HdrProto l4hdr_proto;
    struct virtio_net_hdr *vhdr;
    uint8_t offload_type;

    if (net_rx_pkt_is_vlan_stripped(pkt)) {
        rxcd->ts = 1;
        rxcd->tci = net_rx_pkt_get_vlan_tag(pkt);
    }

    vhdr = net_rx_pkt_get_vhdr(pkt);
    /*
     * Checksum is valid when lower level tell so or when lower level
     * requires checksum offload telling that packet produced/bridged
     * locally and did travel over network after last checksum calculation
     * or production
     */
    csum_ok = VMXNET_FLAG_IS_SET(vhdr->flags, VIRTIO_NET_HDR_F_DATA_VALID) ||
              VMXNET_FLAG_IS_SET(vhdr->flags, VIRTIO_NET_HDR_F_NEEDS_CSUM);

    offload_type = vhdr->gso_type & ~VIRTIO_NET_HDR_GSO_ECN;
    is_gso = (offload_type != VIRTIO_NET_HDR_GSO_NONE) ? 1 : 0;

    if (!csum_ok && !is_gso) {
        goto nocsum;
    }

    net_rx_pkt_get_protocols(pkt, &hasip4, &hasip6, &l4hdr_proto);
    if ((l4hdr_proto != ETH_L4_HDR_PROTO_TCP &&
         l4hdr_proto != ETH_L4_HDR_PROTO_UDP) ||
        (!hasip4 && !hasip6)) {
        goto nocsum;
    }

    rxcd->cnc = 0;
    rxcd->v4 = hasip4 ? 1 : 0;
    rxcd->v6 = hasip6 ? 1 : 0;
    rxcd->tcp = l4hdr_proto == ETH_L4_HDR_PROTO_TCP;
    rxcd->udp = l4hdr_proto == ETH_L4_HDR_PROTO_UDP;
    rxcd->fcs = rxcd->tuc = rxcd->ipc = 1;
    return;

nocsum:
    rxcd->cnc = 1;
    return;
}

static void
vmxnet3_pci_dma_writev(PCIDevice *pci_dev,
                       const struct iovec *iov,
                       size_t start_iov_off,
                       hwaddr target_addr,
                       size_t bytes_to_copy)
{
    size_t curr_off = 0;
    size_t copied = 0;

    while (bytes_to_copy) {
        if (start_iov_off < (curr_off + iov->iov_len)) {
            size_t chunk_len =
                MIN((curr_off + iov->iov_len) - start_iov_off, bytes_to_copy);

            pci_dma_write(pci_dev, target_addr + copied,
                          iov->iov_base + start_iov_off - curr_off,
                          chunk_len);

            copied += chunk_len;
            start_iov_off += chunk_len;
            curr_off = start_iov_off;
            bytes_to_copy -= chunk_len;
        } else {
            curr_off += iov->iov_len;
        }
        iov++;
    }
}

static void
vmxnet3_pci_dma_write_rxcd(PCIDevice *pcidev, dma_addr_t pa,
                           struct Vmxnet3_RxCompDesc *rxcd)
{
    rxcd->val1 = cpu_to_le32(rxcd->val1);
    rxcd->val2 = cpu_to_le32(rxcd->val2);
    rxcd->val3 = cpu_to_le32(rxcd->val3);
    pci_dma_write(pcidev, pa, rxcd, sizeof(*rxcd));
}

static bool
vmxnet3_indicate_packet(VMXNET3State *s)
{
    struct Vmxnet3_RxDesc rxd;
    PCIDevice *d = PCI_DEVICE(s);
    bool is_head = true;
    uint32_t rxd_idx;
    uint32_t rx_ridx = 0;

    struct Vmxnet3_RxCompDesc rxcd;
    uint32_t new_rxcd_gen = VMXNET3_INIT_GEN;
    hwaddr new_rxcd_pa = 0;
    hwaddr ready_rxcd_pa = 0;
    struct iovec *data = net_rx_pkt_get_iovec(s->rx_pkt);
    size_t bytes_copied = 0;
    size_t bytes_left = net_rx_pkt_get_total_len(s->rx_pkt);
    uint16_t num_frags = 0;
    size_t chunk_size;

    net_rx_pkt_dump(s->rx_pkt);

    while (bytes_left > 0) {

        /* cannot add more frags to packet */
        if (num_frags == s->max_rx_frags) {
            break;
        }

        new_rxcd_pa = vmxnet3_pop_rxc_descr(s, RXQ_IDX, &new_rxcd_gen);
        if (!new_rxcd_pa) {
            break;
        }

        if (!vmxnet3_get_next_rx_descr(s, is_head, &rxd, &rxd_idx, &rx_ridx)) {
            break;
        }

        chunk_size = MIN(bytes_left, rxd.len);
        vmxnet3_pci_dma_writev(d, data, bytes_copied, rxd.addr, chunk_size);
        bytes_copied += chunk_size;
        bytes_left -= chunk_size;

        vmxnet3_dump_rx_descr(&rxd);

        if (ready_rxcd_pa != 0) {
            vmxnet3_pci_dma_write_rxcd(d, ready_rxcd_pa, &rxcd);
        }

        memset(&rxcd, 0, sizeof(struct Vmxnet3_RxCompDesc));
        rxcd.rxdIdx = rxd_idx;
        rxcd.len = chunk_size;
        rxcd.sop = is_head;
        rxcd.gen = new_rxcd_gen;
        rxcd.rqID = RXQ_IDX + rx_ridx * s->rxq_num;

        if (bytes_left == 0) {
            vmxnet3_rx_update_descr(s->rx_pkt, &rxcd);
        }

        VMW_RIPRN("RX Completion descriptor: rxRing: %lu rxIdx %lu len %lu "
                  "sop %d csum_correct %lu",
                  (unsigned long) rx_ridx,
                  (unsigned long) rxcd.rxdIdx,
                  (unsigned long) rxcd.len,
                  (int) rxcd.sop,
                  (unsigned long) rxcd.tuc);

        is_head = false;
        ready_rxcd_pa = new_rxcd_pa;
        new_rxcd_pa = 0;
        num_frags++;
    }

    if (ready_rxcd_pa != 0) {
        rxcd.eop = 1;
        rxcd.err = (bytes_left != 0);

        vmxnet3_pci_dma_write_rxcd(d, ready_rxcd_pa, &rxcd);

        /* Flush RX descriptor changes */
        smp_wmb();
    }

    if (new_rxcd_pa != 0) {
        vmxnet3_revert_rxc_descr(s, RXQ_IDX);
    }

    vmxnet3_trigger_interrupt(s, s->rxq_descr[RXQ_IDX].intr_idx);

    if (bytes_left == 0) {
        vmxnet3_on_rx_done_update_stats(s, RXQ_IDX, VMXNET3_PKT_STATUS_OK);
        return true;
    } else if (num_frags == s->max_rx_frags) {
        vmxnet3_on_rx_done_update_stats(s, RXQ_IDX, VMXNET3_PKT_STATUS_ERROR);
        return false;
    } else {
        vmxnet3_on_rx_done_update_stats(s, RXQ_IDX,
                                        VMXNET3_PKT_STATUS_OUT_OF_BUF);
        return false;
    }
}

static void
vmxnet3_io_bar0_write(void *opaque, hwaddr addr,
                      uint64_t val, unsigned size)
{
    VMXNET3State *s = opaque;

    if (!s->device_active) {
        return;
    }

    if (VMW_IS_MULTIREG_ADDR(addr, VMXNET3_REG_TXPROD,
                        VMXNET3_DEVICE_MAX_TX_QUEUES, VMXNET3_REG_ALIGN)) {
        int tx_queue_idx =
            VMW_MULTIREG_IDX_BY_ADDR(addr, VMXNET3_REG_TXPROD,
                                     VMXNET3_REG_ALIGN);
        if (tx_queue_idx <= s->txq_num) {
            vmxnet3_process_tx_queue(s, tx_queue_idx);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "vmxnet3: Illegal TX queue %d/%d\n",
                          tx_queue_idx, s->txq_num);
        }
        return;
    }

    if (VMW_IS_MULTIREG_ADDR(addr, VMXNET3_REG_IMR,
                        VMXNET3_MAX_INTRS, VMXNET3_REG_ALIGN)) {
        int l = VMW_MULTIREG_IDX_BY_ADDR(addr, VMXNET3_REG_IMR,
                                         VMXNET3_REG_ALIGN);

        VMW_CBPRN("Interrupt mask for line %d written: 0x%" PRIx64, l, val);

        vmxnet3_on_interrupt_mask_changed(s, l, val);
        return;
    }

    if (VMW_IS_MULTIREG_ADDR(addr, VMXNET3_REG_RXPROD,
                        VMXNET3_DEVICE_MAX_RX_QUEUES, VMXNET3_REG_ALIGN) ||
       VMW_IS_MULTIREG_ADDR(addr, VMXNET3_REG_RXPROD2,
                        VMXNET3_DEVICE_MAX_RX_QUEUES, VMXNET3_REG_ALIGN)) {
        return;
    }

    VMW_WRPRN("BAR0 unknown write [%" PRIx64 "] = %" PRIx64 ", size %d",
              (uint64_t) addr, val, size);
}

static uint64_t
vmxnet3_io_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    VMXNET3State *s = opaque;

    if (VMW_IS_MULTIREG_ADDR(addr, VMXNET3_REG_IMR,
                        VMXNET3_MAX_INTRS, VMXNET3_REG_ALIGN)) {
        int l = VMW_MULTIREG_IDX_BY_ADDR(addr, VMXNET3_REG_IMR,
                                         VMXNET3_REG_ALIGN);
        return s->interrupt_states[l].is_masked;
    }

    VMW_CBPRN("BAR0 unknown read [%" PRIx64 "], size %d", addr, size);
    return 0;
}

static void vmxnet3_reset_interrupt_states(VMXNET3State *s)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(s->interrupt_states); i++) {
        s->interrupt_states[i].is_asserted = false;
        s->interrupt_states[i].is_pending = false;
        s->interrupt_states[i].is_masked = true;
    }
}

static void vmxnet3_reset_mac(VMXNET3State *s)
{
    memcpy(&s->conf.macaddr.a, &s->perm_mac.a, sizeof(s->perm_mac.a));
    VMW_CFPRN("MAC address set to: " MAC_FMT, MAC_ARG(s->conf.macaddr.a));
}

static void vmxnet3_deactivate_device(VMXNET3State *s)
{
    if (s->device_active) {
        VMW_CBPRN("Deactivating vmxnet3...");
        net_tx_pkt_uninit(s->tx_pkt);
        net_rx_pkt_uninit(s->rx_pkt);
        s->device_active = false;
    }
}

static void vmxnet3_reset(VMXNET3State *s)
{
    VMW_CBPRN("Resetting vmxnet3...");

    vmxnet3_deactivate_device(s);
    vmxnet3_reset_interrupt_states(s);
    s->drv_shmem = 0;
    s->tx_sop = true;
    s->skip_current_tx_pkt = false;
}

static void vmxnet3_update_rx_mode(VMXNET3State *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    s->rx_mode = VMXNET3_READ_DRV_SHARED32(d, s->drv_shmem,
                                           devRead.rxFilterConf.rxMode);
    VMW_CFPRN("RX mode: 0x%08X", s->rx_mode);
}

static void vmxnet3_update_vlan_filters(VMXNET3State *s)
{
    int i;
    PCIDevice *d = PCI_DEVICE(s);

    /* Copy configuration from shared memory */
    VMXNET3_READ_DRV_SHARED(d, s->drv_shmem,
                            devRead.rxFilterConf.vfTable,
                            s->vlan_table,
                            sizeof(s->vlan_table));

    /* Invert byte order when needed */
    for (i = 0; i < ARRAY_SIZE(s->vlan_table); i++) {
        s->vlan_table[i] = le32_to_cpu(s->vlan_table[i]);
    }

    /* Dump configuration for debugging purposes */
    VMW_CFPRN("Configured VLANs:");
    for (i = 0; i < sizeof(s->vlan_table) * 8; i++) {
        if (VMXNET3_VFTABLE_ENTRY_IS_SET(s->vlan_table, i)) {
            VMW_CFPRN("\tVLAN %d is present", i);
        }
    }
}

static void vmxnet3_update_mcast_filters(VMXNET3State *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    uint16_t list_bytes =
        VMXNET3_READ_DRV_SHARED16(d, s->drv_shmem,
                                  devRead.rxFilterConf.mfTableLen);

    s->mcast_list_len = list_bytes / sizeof(s->mcast_list[0]);

    s->mcast_list = g_realloc(s->mcast_list, list_bytes);
    if (!s->mcast_list) {
        if (s->mcast_list_len == 0) {
            VMW_CFPRN("Current multicast list is empty");
        } else {
            VMW_ERPRN("Failed to allocate multicast list of %d elements",
                      s->mcast_list_len);
        }
        s->mcast_list_len = 0;
    } else {
        int i;
        hwaddr mcast_list_pa =
            VMXNET3_READ_DRV_SHARED64(d, s->drv_shmem,
                                      devRead.rxFilterConf.mfTablePA);

        pci_dma_read(d, mcast_list_pa, s->mcast_list, list_bytes);

        VMW_CFPRN("Current multicast list len is %d:", s->mcast_list_len);
        for (i = 0; i < s->mcast_list_len; i++) {
            VMW_CFPRN("\t" MAC_FMT, MAC_ARG(s->mcast_list[i].a));
        }
    }
}

static void vmxnet3_setup_rx_filtering(VMXNET3State *s)
{
    vmxnet3_update_rx_mode(s);
    vmxnet3_update_vlan_filters(s);
    vmxnet3_update_mcast_filters(s);
}

static uint32_t vmxnet3_get_interrupt_config(VMXNET3State *s)
{
    uint32_t interrupt_mode = VMXNET3_IT_AUTO | (VMXNET3_IMM_AUTO << 2);
    VMW_CFPRN("Interrupt config is 0x%X", interrupt_mode);
    return interrupt_mode;
}

static void vmxnet3_fill_stats(VMXNET3State *s)
{
    int i;
    PCIDevice *d = PCI_DEVICE(s);

    if (!s->device_active)
        return;

    for (i = 0; i < s->txq_num; i++) {
        pci_dma_write(d,
                      s->txq_descr[i].tx_stats_pa,
                      &s->txq_descr[i].txq_stats,
                      sizeof(s->txq_descr[i].txq_stats));
    }

    for (i = 0; i < s->rxq_num; i++) {
        pci_dma_write(d,
                      s->rxq_descr[i].rx_stats_pa,
                      &s->rxq_descr[i].rxq_stats,
                      sizeof(s->rxq_descr[i].rxq_stats));
    }
}

static void vmxnet3_adjust_by_guest_type(VMXNET3State *s)
{
    struct Vmxnet3_GOSInfo gos;
    PCIDevice *d = PCI_DEVICE(s);

    VMXNET3_READ_DRV_SHARED(d, s->drv_shmem, devRead.misc.driverInfo.gos,
                            &gos, sizeof(gos));
    s->rx_packets_compound =
        (gos.gosType == VMXNET3_GOS_TYPE_WIN) ? false : true;

    VMW_CFPRN("Guest type specifics: RXCOMPOUND: %d", s->rx_packets_compound);
}

static void
vmxnet3_dump_conf_descr(const char *name,
                        struct Vmxnet3_VariableLenConfDesc *pm_descr)
{
    VMW_CFPRN("%s descriptor dump: Version %u, Length %u",
              name, pm_descr->confVer, pm_descr->confLen);

};

static void vmxnet3_update_pm_state(VMXNET3State *s)
{
    struct Vmxnet3_VariableLenConfDesc pm_descr;
    PCIDevice *d = PCI_DEVICE(s);

    pm_descr.confLen =
        VMXNET3_READ_DRV_SHARED32(d, s->drv_shmem, devRead.pmConfDesc.confLen);
    pm_descr.confVer =
        VMXNET3_READ_DRV_SHARED32(d, s->drv_shmem, devRead.pmConfDesc.confVer);
    pm_descr.confPA =
        VMXNET3_READ_DRV_SHARED64(d, s->drv_shmem, devRead.pmConfDesc.confPA);

    vmxnet3_dump_conf_descr("PM State", &pm_descr);
}

static void vmxnet3_update_features(VMXNET3State *s)
{
    uint32_t guest_features;
    int rxcso_supported;
    PCIDevice *d = PCI_DEVICE(s);

    guest_features = VMXNET3_READ_DRV_SHARED32(d, s->drv_shmem,
                                               devRead.misc.uptFeatures);

    rxcso_supported = VMXNET_FLAG_IS_SET(guest_features, UPT1_F_RXCSUM);
    s->rx_vlan_stripping = VMXNET_FLAG_IS_SET(guest_features, UPT1_F_RXVLAN);
    s->lro_supported = VMXNET_FLAG_IS_SET(guest_features, UPT1_F_LRO);

    VMW_CFPRN("Features configuration: LRO: %d, RXCSUM: %d, VLANSTRIP: %d",
              s->lro_supported, rxcso_supported,
              s->rx_vlan_stripping);
    if (s->peer_has_vhdr) {
        qemu_set_offload(qemu_get_queue(s->nic)->peer,
                         rxcso_supported,
                         s->lro_supported,
                         s->lro_supported,
                         0,
                         0,
                         0,
                         0);
    }
}

static bool vmxnet3_verify_intx(VMXNET3State *s, int intx)
{
    return s->msix_used || msi_enabled(PCI_DEVICE(s))
        || intx == pci_get_byte(s->parent_obj.config + PCI_INTERRUPT_PIN) - 1;
}

static void vmxnet3_validate_interrupt_idx(bool is_msix, int idx)
{
    int max_ints = is_msix ? VMXNET3_MAX_INTRS : VMXNET3_MAX_NMSIX_INTRS;
    if (idx >= max_ints) {
        hw_error("Bad interrupt index: %d\n", idx);
    }
}

static void vmxnet3_validate_interrupts(VMXNET3State *s)
{
    int i;

    VMW_CFPRN("Verifying event interrupt index (%d)", s->event_int_idx);
    vmxnet3_validate_interrupt_idx(s->msix_used, s->event_int_idx);

    for (i = 0; i < s->txq_num; i++) {
        int idx = s->txq_descr[i].intr_idx;
        VMW_CFPRN("Verifying TX queue %d interrupt index (%d)", i, idx);
        vmxnet3_validate_interrupt_idx(s->msix_used, idx);
    }

    for (i = 0; i < s->rxq_num; i++) {
        int idx = s->rxq_descr[i].intr_idx;
        VMW_CFPRN("Verifying RX queue %d interrupt index (%d)", i, idx);
        vmxnet3_validate_interrupt_idx(s->msix_used, idx);
    }
}

static bool vmxnet3_validate_queues(VMXNET3State *s)
{
    /*
    * txq_num and rxq_num are total number of queues
    * configured by guest. These numbers must not
    * exceed corresponding maximal values.
    */

    if (s->txq_num > VMXNET3_DEVICE_MAX_TX_QUEUES) {
        qemu_log_mask(LOG_GUEST_ERROR, "vmxnet3: Bad TX queues number: %d\n",
                      s->txq_num);
        return false;
    }

    if (s->rxq_num > VMXNET3_DEVICE_MAX_RX_QUEUES) {
        qemu_log_mask(LOG_GUEST_ERROR, "vmxnet3: Bad RX queues number: %d\n",
                      s->rxq_num);
        return false;
    }

    return true;
}

static void vmxnet3_activate_device(VMXNET3State *s)
{
    int i;
    static const uint32_t VMXNET3_DEF_TX_THRESHOLD = 1;
    PCIDevice *d = PCI_DEVICE(s);
    hwaddr qdescr_table_pa;
    uint64_t pa;
    uint32_t size;

    /* Verify configuration consistency */
    if (!vmxnet3_verify_driver_magic(d, s->drv_shmem)) {
        VMW_ERPRN("Device configuration received from driver is invalid");
        return;
    }

    /* Verify if device is active */
    if (s->device_active) {
        VMW_CFPRN("Vmxnet3 device is active");
        return;
    }

    s->txq_num =
        VMXNET3_READ_DRV_SHARED8(d, s->drv_shmem, devRead.misc.numTxQueues);
    s->rxq_num =
        VMXNET3_READ_DRV_SHARED8(d, s->drv_shmem, devRead.misc.numRxQueues);

    VMW_CFPRN("Number of TX/RX queues %u/%u", s->txq_num, s->rxq_num);
    if (!vmxnet3_validate_queues(s)) {
        return;
    }

    vmxnet3_adjust_by_guest_type(s);
    vmxnet3_update_features(s);
    vmxnet3_update_pm_state(s);
    vmxnet3_setup_rx_filtering(s);
    /* Cache fields from shared memory */
    s->mtu = VMXNET3_READ_DRV_SHARED32(d, s->drv_shmem, devRead.misc.mtu);
    if (s->mtu < VMXNET3_MIN_MTU || s->mtu > VMXNET3_MAX_MTU) {
        qemu_log_mask(LOG_GUEST_ERROR, "vmxnet3: Bad MTU size: %u\n", s->mtu);
        return;
    }
    VMW_CFPRN("MTU is %u", s->mtu);

    s->max_rx_frags =
        VMXNET3_READ_DRV_SHARED16(d, s->drv_shmem, devRead.misc.maxNumRxSG);

    if (s->max_rx_frags == 0) {
        s->max_rx_frags = 1;
    }

    VMW_CFPRN("Max RX fragments is %u", s->max_rx_frags);

    s->event_int_idx =
        VMXNET3_READ_DRV_SHARED8(d, s->drv_shmem, devRead.intrConf.eventIntrIdx);
    assert(vmxnet3_verify_intx(s, s->event_int_idx));
    VMW_CFPRN("Events interrupt line is %u", s->event_int_idx);

    s->auto_int_masking =
        VMXNET3_READ_DRV_SHARED8(d, s->drv_shmem, devRead.intrConf.autoMask);
    VMW_CFPRN("Automatic interrupt masking is %d", (int)s->auto_int_masking);

    qdescr_table_pa =
        VMXNET3_READ_DRV_SHARED64(d, s->drv_shmem, devRead.misc.queueDescPA);
    VMW_CFPRN("TX queues descriptors table is at 0x%" PRIx64, qdescr_table_pa);

    /*
     * Worst-case scenario is a packet that holds all TX rings space so
     * we calculate total size of all TX rings for max TX fragments number
     */
    s->max_tx_frags = 0;

    /* TX queues */
    for (i = 0; i < s->txq_num; i++) {
        hwaddr qdescr_pa =
            qdescr_table_pa + i * sizeof(struct Vmxnet3_TxQueueDesc);

        /* Read interrupt number for this TX queue */
        s->txq_descr[i].intr_idx =
            VMXNET3_READ_TX_QUEUE_DESCR8(d, qdescr_pa, conf.intrIdx);
        assert(vmxnet3_verify_intx(s, s->txq_descr[i].intr_idx));

        VMW_CFPRN("TX Queue %d interrupt: %d", i, s->txq_descr[i].intr_idx);

        /* Read rings memory locations for TX queues */
        pa = VMXNET3_READ_TX_QUEUE_DESCR64(d, qdescr_pa, conf.txRingBasePA);
        size = VMXNET3_READ_TX_QUEUE_DESCR32(d, qdescr_pa, conf.txRingSize);
        if (size > VMXNET3_TX_RING_MAX_SIZE) {
            size = VMXNET3_TX_RING_MAX_SIZE;
        }

        vmxnet3_ring_init(d, &s->txq_descr[i].tx_ring, pa, size,
                          sizeof(struct Vmxnet3_TxDesc), false);
        VMXNET3_RING_DUMP(VMW_CFPRN, "TX", i, &s->txq_descr[i].tx_ring);

        s->max_tx_frags += size;

        /* TXC ring */
        pa = VMXNET3_READ_TX_QUEUE_DESCR64(d, qdescr_pa, conf.compRingBasePA);
        size = VMXNET3_READ_TX_QUEUE_DESCR32(d, qdescr_pa, conf.compRingSize);
        if (size > VMXNET3_TC_RING_MAX_SIZE) {
            size = VMXNET3_TC_RING_MAX_SIZE;
        }
        vmxnet3_ring_init(d, &s->txq_descr[i].comp_ring, pa, size,
                          sizeof(struct Vmxnet3_TxCompDesc), true);
        VMXNET3_RING_DUMP(VMW_CFPRN, "TXC", i, &s->txq_descr[i].comp_ring);

        s->txq_descr[i].tx_stats_pa =
            qdescr_pa + offsetof(struct Vmxnet3_TxQueueDesc, stats);

        memset(&s->txq_descr[i].txq_stats, 0,
               sizeof(s->txq_descr[i].txq_stats));

        /* Fill device-managed parameters for queues */
        VMXNET3_WRITE_TX_QUEUE_DESCR32(d, qdescr_pa,
                                       ctrl.txThreshold,
                                       VMXNET3_DEF_TX_THRESHOLD);
    }

    /* Preallocate TX packet wrapper */
    VMW_CFPRN("Max TX fragments is %u", s->max_tx_frags);
    net_tx_pkt_init(&s->tx_pkt, s->max_tx_frags);
    net_rx_pkt_init(&s->rx_pkt);

    /* Read rings memory locations for RX queues */
    for (i = 0; i < s->rxq_num; i++) {
        int j;
        hwaddr qd_pa =
            qdescr_table_pa + s->txq_num * sizeof(struct Vmxnet3_TxQueueDesc) +
            i * sizeof(struct Vmxnet3_RxQueueDesc);

        /* Read interrupt number for this RX queue */
        s->rxq_descr[i].intr_idx =
            VMXNET3_READ_TX_QUEUE_DESCR8(d, qd_pa, conf.intrIdx);
        assert(vmxnet3_verify_intx(s, s->rxq_descr[i].intr_idx));

        VMW_CFPRN("RX Queue %d interrupt: %d", i, s->rxq_descr[i].intr_idx);

        /* Read rings memory locations */
        for (j = 0; j < VMXNET3_RX_RINGS_PER_QUEUE; j++) {
            /* RX rings */
            pa = VMXNET3_READ_RX_QUEUE_DESCR64(d, qd_pa, conf.rxRingBasePA[j]);
            size = VMXNET3_READ_RX_QUEUE_DESCR32(d, qd_pa, conf.rxRingSize[j]);
            if (size > VMXNET3_RX_RING_MAX_SIZE) {
                size = VMXNET3_RX_RING_MAX_SIZE;
            }
            vmxnet3_ring_init(d, &s->rxq_descr[i].rx_ring[j], pa, size,
                              sizeof(struct Vmxnet3_RxDesc), false);
            VMW_CFPRN("RX queue %d:%d: Base: %" PRIx64 ", Size: %d",
                      i, j, pa, size);
        }

        /* RXC ring */
        pa = VMXNET3_READ_RX_QUEUE_DESCR64(d, qd_pa, conf.compRingBasePA);
        size = VMXNET3_READ_RX_QUEUE_DESCR32(d, qd_pa, conf.compRingSize);
        if (size > VMXNET3_RC_RING_MAX_SIZE) {
            size = VMXNET3_RC_RING_MAX_SIZE;
        }
        vmxnet3_ring_init(d, &s->rxq_descr[i].comp_ring, pa, size,
                          sizeof(struct Vmxnet3_RxCompDesc), true);
        VMW_CFPRN("RXC queue %d: Base: %" PRIx64 ", Size: %d", i, pa, size);

        s->rxq_descr[i].rx_stats_pa =
            qd_pa + offsetof(struct Vmxnet3_RxQueueDesc, stats);
        memset(&s->rxq_descr[i].rxq_stats, 0,
               sizeof(s->rxq_descr[i].rxq_stats));
    }

    vmxnet3_validate_interrupts(s);

    /* Make sure everything is in place before device activation */
    smp_wmb();

    vmxnet3_reset_mac(s);

    s->device_active = true;
}

static void vmxnet3_handle_command(VMXNET3State *s, uint64_t cmd)
{
    s->last_command = cmd;

    switch (cmd) {
    case VMXNET3_CMD_GET_PERM_MAC_HI:
        VMW_CBPRN("Set: Get upper part of permanent MAC");
        break;

    case VMXNET3_CMD_GET_PERM_MAC_LO:
        VMW_CBPRN("Set: Get lower part of permanent MAC");
        break;

    case VMXNET3_CMD_GET_STATS:
        VMW_CBPRN("Set: Get device statistics");
        vmxnet3_fill_stats(s);
        break;

    case VMXNET3_CMD_ACTIVATE_DEV:
        VMW_CBPRN("Set: Activating vmxnet3 device");
        vmxnet3_activate_device(s);
        break;

    case VMXNET3_CMD_UPDATE_RX_MODE:
        VMW_CBPRN("Set: Update rx mode");
        vmxnet3_update_rx_mode(s);
        break;

    case VMXNET3_CMD_UPDATE_VLAN_FILTERS:
        VMW_CBPRN("Set: Update VLAN filters");
        vmxnet3_update_vlan_filters(s);
        break;

    case VMXNET3_CMD_UPDATE_MAC_FILTERS:
        VMW_CBPRN("Set: Update MAC filters");
        vmxnet3_update_mcast_filters(s);
        break;

    case VMXNET3_CMD_UPDATE_FEATURE:
        VMW_CBPRN("Set: Update features");
        vmxnet3_update_features(s);
        break;

    case VMXNET3_CMD_UPDATE_PMCFG:
        VMW_CBPRN("Set: Update power management config");
        vmxnet3_update_pm_state(s);
        break;

    case VMXNET3_CMD_GET_LINK:
        VMW_CBPRN("Set: Get link");
        break;

    case VMXNET3_CMD_RESET_DEV:
        VMW_CBPRN("Set: Reset device");
        vmxnet3_reset(s);
        break;

    case VMXNET3_CMD_QUIESCE_DEV:
        VMW_CBPRN("Set: VMXNET3_CMD_QUIESCE_DEV - deactivate the device");
        vmxnet3_deactivate_device(s);
        break;

    case VMXNET3_CMD_GET_CONF_INTR:
        VMW_CBPRN("Set: VMXNET3_CMD_GET_CONF_INTR - interrupt configuration");
        break;

    case VMXNET3_CMD_GET_ADAPTIVE_RING_INFO:
        VMW_CBPRN("Set: VMXNET3_CMD_GET_ADAPTIVE_RING_INFO - "
                  "adaptive ring info flags");
        break;

    case VMXNET3_CMD_GET_DID_LO:
        VMW_CBPRN("Set: Get lower part of device ID");
        break;

    case VMXNET3_CMD_GET_DID_HI:
        VMW_CBPRN("Set: Get upper part of device ID");
        break;

    case VMXNET3_CMD_GET_DEV_EXTRA_INFO:
        VMW_CBPRN("Set: Get device extra info");
        break;

    default:
        VMW_CBPRN("Received unknown command: %" PRIx64, cmd);
        break;
    }
}

static uint64_t vmxnet3_get_command_status(VMXNET3State *s)
{
    uint64_t ret;

    switch (s->last_command) {
    case VMXNET3_CMD_ACTIVATE_DEV:
        ret = (s->device_active) ? 0 : 1;
        VMW_CFPRN("Device active: %" PRIx64, ret);
        break;

    case VMXNET3_CMD_RESET_DEV:
    case VMXNET3_CMD_QUIESCE_DEV:
    case VMXNET3_CMD_GET_QUEUE_STATUS:
    case VMXNET3_CMD_GET_DEV_EXTRA_INFO:
        ret = 0;
        break;

    case VMXNET3_CMD_GET_LINK:
        ret = s->link_status_and_speed;
        VMW_CFPRN("Link and speed: %" PRIx64, ret);
        break;

    case VMXNET3_CMD_GET_PERM_MAC_LO:
        ret = vmxnet3_get_mac_low(&s->perm_mac);
        break;

    case VMXNET3_CMD_GET_PERM_MAC_HI:
        ret = vmxnet3_get_mac_high(&s->perm_mac);
        break;

    case VMXNET3_CMD_GET_CONF_INTR:
        ret = vmxnet3_get_interrupt_config(s);
        break;

    case VMXNET3_CMD_GET_ADAPTIVE_RING_INFO:
        ret = VMXNET3_DISABLE_ADAPTIVE_RING;
        break;

    case VMXNET3_CMD_GET_DID_LO:
        ret = PCI_DEVICE_ID_VMWARE_VMXNET3;
        break;

    case VMXNET3_CMD_GET_DID_HI:
        ret = VMXNET3_DEVICE_REVISION;
        break;

    default:
        VMW_WRPRN("Received request for unknown command: %x", s->last_command);
        ret = 0;
        break;
    }

    return ret;
}

static void vmxnet3_set_events(VMXNET3State *s, uint32_t val)
{
    uint32_t events;
    PCIDevice *d = PCI_DEVICE(s);

    VMW_CBPRN("Setting events: 0x%x", val);
    events = VMXNET3_READ_DRV_SHARED32(d, s->drv_shmem, ecr) | val;
    VMXNET3_WRITE_DRV_SHARED32(d, s->drv_shmem, ecr, events);
}

static void vmxnet3_ack_events(VMXNET3State *s, uint32_t val)
{
    PCIDevice *d = PCI_DEVICE(s);
    uint32_t events;

    VMW_CBPRN("Clearing events: 0x%x", val);
    events = VMXNET3_READ_DRV_SHARED32(d, s->drv_shmem, ecr) & ~val;
    VMXNET3_WRITE_DRV_SHARED32(d, s->drv_shmem, ecr, events);
}

static void
vmxnet3_io_bar1_write(void *opaque,
                      hwaddr addr,
                      uint64_t val,
                      unsigned size)
{
    VMXNET3State *s = opaque;

    switch (addr) {
    /* Vmxnet3 Revision Report Selection */
    case VMXNET3_REG_VRRS:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_VRRS] = %" PRIx64 ", size %d",
                  val, size);
        break;

    /* UPT Version Report Selection */
    case VMXNET3_REG_UVRS:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_UVRS] = %" PRIx64 ", size %d",
                  val, size);
        break;

    /* Driver Shared Address Low */
    case VMXNET3_REG_DSAL:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_DSAL] = %" PRIx64 ", size %d",
                  val, size);
        /*
         * Guest driver will first write the low part of the shared
         * memory address. We save it to temp variable and set the
         * shared address only after we get the high part
         */
        if (val == 0) {
            vmxnet3_deactivate_device(s);
        }
        s->temp_shared_guest_driver_memory = val;
        s->drv_shmem = 0;
        break;

    /* Driver Shared Address High */
    case VMXNET3_REG_DSAH:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_DSAH] = %" PRIx64 ", size %d",
                  val, size);
        /*
         * Set the shared memory between guest driver and device.
         * We already should have low address part.
         */
        s->drv_shmem = s->temp_shared_guest_driver_memory | (val << 32);
        break;

    /* Command */
    case VMXNET3_REG_CMD:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_CMD] = %" PRIx64 ", size %d",
                  val, size);
        vmxnet3_handle_command(s, val);
        break;

    /* MAC Address Low */
    case VMXNET3_REG_MACL:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_MACL] = %" PRIx64 ", size %d",
                  val, size);
        s->temp_mac = val;
        break;

    /* MAC Address High */
    case VMXNET3_REG_MACH:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_MACH] = %" PRIx64 ", size %d",
                  val, size);
        vmxnet3_set_variable_mac(s, val, s->temp_mac);
        break;

    /* Interrupt Cause Register */
    case VMXNET3_REG_ICR:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_ICR] = %" PRIx64 ", size %d",
                  val, size);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register VMXNET3_REG_ICR\n",
                      TYPE_VMXNET3);
        break;

    /* Event Cause Register */
    case VMXNET3_REG_ECR:
        VMW_CBPRN("Write BAR1 [VMXNET3_REG_ECR] = %" PRIx64 ", size %d",
                  val, size);
        vmxnet3_ack_events(s, val);
        break;

    default:
        VMW_CBPRN("Unknown Write to BAR1 [%" PRIx64 "] = %" PRIx64 ", size %d",
                  addr, val, size);
        break;
    }
}

static uint64_t
vmxnet3_io_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
        VMXNET3State *s = opaque;
        uint64_t ret = 0;

        switch (addr) {
        /* Vmxnet3 Revision Report Selection */
        case VMXNET3_REG_VRRS:
            VMW_CBPRN("Read BAR1 [VMXNET3_REG_VRRS], size %d", size);
            ret = VMXNET3_DEVICE_REVISION;
            break;

        /* UPT Version Report Selection */
        case VMXNET3_REG_UVRS:
            VMW_CBPRN("Read BAR1 [VMXNET3_REG_UVRS], size %d", size);
            ret = VMXNET3_UPT_REVISION;
            break;

        /* Command */
        case VMXNET3_REG_CMD:
            VMW_CBPRN("Read BAR1 [VMXNET3_REG_CMD], size %d", size);
            ret = vmxnet3_get_command_status(s);
            break;

        /* MAC Address Low */
        case VMXNET3_REG_MACL:
            VMW_CBPRN("Read BAR1 [VMXNET3_REG_MACL], size %d", size);
            ret = vmxnet3_get_mac_low(&s->conf.macaddr);
            break;

        /* MAC Address High */
        case VMXNET3_REG_MACH:
            VMW_CBPRN("Read BAR1 [VMXNET3_REG_MACH], size %d", size);
            ret = vmxnet3_get_mac_high(&s->conf.macaddr);
            break;

        /*
         * Interrupt Cause Register
         * Used for legacy interrupts only so interrupt index always 0
         */
        case VMXNET3_REG_ICR:
            VMW_CBPRN("Read BAR1 [VMXNET3_REG_ICR], size %d", size);
            if (vmxnet3_interrupt_asserted(s, 0)) {
                vmxnet3_clear_interrupt(s, 0);
                ret = true;
            } else {
                ret = false;
            }
            break;

        default:
            VMW_CBPRN("Unknown read BAR1[%" PRIx64 "], %d bytes", addr, size);
            break;
        }

        return ret;
}

static int
vmxnet3_can_receive(NetClientState *nc)
{
    VMXNET3State *s = qemu_get_nic_opaque(nc);
    return s->device_active &&
           VMXNET_FLAG_IS_SET(s->link_status_and_speed, VMXNET3_LINK_STATUS_UP);
}

static inline bool
vmxnet3_is_registered_vlan(VMXNET3State *s, const void *data)
{
    uint16_t vlan_tag = eth_get_pkt_tci(data) & VLAN_VID_MASK;
    if (IS_SPECIAL_VLAN_ID(vlan_tag)) {
        return true;
    }

    return VMXNET3_VFTABLE_ENTRY_IS_SET(s->vlan_table, vlan_tag);
}

static bool
vmxnet3_is_allowed_mcast_group(VMXNET3State *s, const uint8_t *group_mac)
{
    int i;
    for (i = 0; i < s->mcast_list_len; i++) {
        if (!memcmp(group_mac, s->mcast_list[i].a, sizeof(s->mcast_list[i]))) {
            return true;
        }
    }
    return false;
}

static bool
vmxnet3_rx_filter_may_indicate(VMXNET3State *s, const void *data,
    size_t size)
{
    struct eth_header *ehdr = PKT_GET_ETH_HDR(data);

    if (VMXNET_FLAG_IS_SET(s->rx_mode, VMXNET3_RXM_PROMISC)) {
        return true;
    }

    if (!vmxnet3_is_registered_vlan(s, data)) {
        return false;
    }

    switch (net_rx_pkt_get_packet_type(s->rx_pkt)) {
    case ETH_PKT_UCAST:
        if (!VMXNET_FLAG_IS_SET(s->rx_mode, VMXNET3_RXM_UCAST)) {
            return false;
        }
        if (memcmp(s->conf.macaddr.a, ehdr->h_dest, ETH_ALEN)) {
            return false;
        }
        break;

    case ETH_PKT_BCAST:
        if (!VMXNET_FLAG_IS_SET(s->rx_mode, VMXNET3_RXM_BCAST)) {
            return false;
        }
        break;

    case ETH_PKT_MCAST:
        if (VMXNET_FLAG_IS_SET(s->rx_mode, VMXNET3_RXM_ALL_MULTI)) {
            return true;
        }
        if (!VMXNET_FLAG_IS_SET(s->rx_mode, VMXNET3_RXM_MCAST)) {
            return false;
        }
        if (!vmxnet3_is_allowed_mcast_group(s, ehdr->h_dest)) {
            return false;
        }
        break;

    default:
        g_assert_not_reached();
    }

    return true;
}

static ssize_t
vmxnet3_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    VMXNET3State *s = qemu_get_nic_opaque(nc);
    size_t bytes_indicated;

    if (!vmxnet3_can_receive(nc)) {
        VMW_PKPRN("Cannot receive now");
        return -1;
    }

    if (s->peer_has_vhdr) {
        net_rx_pkt_set_vhdr(s->rx_pkt, (struct virtio_net_hdr *)buf);
        buf += sizeof(struct virtio_net_hdr);
        size -= sizeof(struct virtio_net_hdr);
    }

    net_rx_pkt_set_packet_type(s->rx_pkt,
        get_eth_packet_type(PKT_GET_ETH_HDR(buf)));

    if (vmxnet3_rx_filter_may_indicate(s, buf, size)) {
        struct iovec iov = {
            .iov_base = (void *)buf,
            .iov_len = size
        };

        net_rx_pkt_set_protocols(s->rx_pkt, &iov, 1, 0);
        vmxnet3_rx_need_csum_calculate(s->rx_pkt, buf, size);
        net_rx_pkt_attach_data(s->rx_pkt, buf, size, s->rx_vlan_stripping);
        bytes_indicated = vmxnet3_indicate_packet(s) ? size : -1;
        if (bytes_indicated < size) {
            VMW_PKPRN("RX: %zu of %zu bytes indicated", bytes_indicated, size);
        }
    } else {
        VMW_PKPRN("Packet dropped by RX filter");
        bytes_indicated = size;
    }

    assert(size > 0);
    assert(bytes_indicated != 0);
    return bytes_indicated;
}

static void vmxnet3_set_link_status(NetClientState *nc)
{
    VMXNET3State *s = qemu_get_nic_opaque(nc);

    if (nc->link_down) {
        s->link_status_and_speed &= ~VMXNET3_LINK_STATUS_UP;
    } else {
        s->link_status_and_speed |= VMXNET3_LINK_STATUS_UP;
    }

    vmxnet3_set_events(s, VMXNET3_ECR_LINK);
    vmxnet3_trigger_interrupt(s, s->event_int_idx);
}

static NetClientInfo net_vmxnet3_info = {
        .type = NET_CLIENT_DRIVER_NIC,
        .size = sizeof(NICState),
        .receive = vmxnet3_receive,
        .link_status_changed = vmxnet3_set_link_status,
};

static bool vmxnet3_peer_has_vnet_hdr(VMXNET3State *s)
{
    NetClientState *nc = qemu_get_queue(s->nic);

    if (qemu_has_vnet_hdr(nc->peer)) {
        return true;
    }

    return false;
}

static void vmxnet3_net_uninit(VMXNET3State *s)
{
    g_free(s->mcast_list);
    vmxnet3_deactivate_device(s);
    qemu_del_nic(s->nic);
}

static void vmxnet3_net_init(VMXNET3State *s)
{
    DeviceState *d = DEVICE(s);

    VMW_CBPRN("vmxnet3_net_init called...");

    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    /* Windows guest will query the address that was set on init */
    memcpy(&s->perm_mac.a, &s->conf.macaddr.a, sizeof(s->perm_mac.a));

    s->mcast_list = NULL;
    s->mcast_list_len = 0;

    s->link_status_and_speed = VMXNET3_LINK_SPEED | VMXNET3_LINK_STATUS_UP;

    VMW_CFPRN("Permanent MAC: " MAC_FMT, MAC_ARG(s->perm_mac.a));

    s->nic = qemu_new_nic(&net_vmxnet3_info, &s->conf,
                          object_get_typename(OBJECT(s)),
                          d->id, &d->mem_reentrancy_guard, s);

    s->peer_has_vhdr = vmxnet3_peer_has_vnet_hdr(s);
    s->tx_sop = true;
    s->skip_current_tx_pkt = false;
    s->tx_pkt = NULL;
    s->rx_pkt = NULL;
    s->rx_vlan_stripping = false;
    s->lro_supported = false;

    if (s->peer_has_vhdr) {
        qemu_set_vnet_hdr_len(qemu_get_queue(s->nic)->peer,
            sizeof(struct virtio_net_hdr));

        qemu_using_vnet_hdr(qemu_get_queue(s->nic)->peer, 1);
    }

    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void
vmxnet3_unuse_msix_vectors(VMXNET3State *s, int num_vectors)
{
    PCIDevice *d = PCI_DEVICE(s);
    int i;
    for (i = 0; i < num_vectors; i++) {
        msix_vector_unuse(d, i);
    }
}

static void
vmxnet3_use_msix_vectors(VMXNET3State *s, int num_vectors)
{
    PCIDevice *d = PCI_DEVICE(s);
    int i;
    for (i = 0; i < num_vectors; i++) {
        msix_vector_use(d, i);
    }
}

static bool
vmxnet3_init_msix(VMXNET3State *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int res = msix_init(d, VMXNET3_MAX_INTRS,
                        &s->msix_bar,
                        VMXNET3_MSIX_BAR_IDX, VMXNET3_OFF_MSIX_TABLE,
                        &s->msix_bar,
                        VMXNET3_MSIX_BAR_IDX, VMXNET3_OFF_MSIX_PBA(s),
                        VMXNET3_MSIX_OFFSET(s), NULL);

    if (0 > res) {
        VMW_WRPRN("Failed to initialize MSI-X, error %d", res);
        s->msix_used = false;
    } else {
        vmxnet3_use_msix_vectors(s, VMXNET3_MAX_INTRS);
        s->msix_used = true;
    }
    return s->msix_used;
}

static void
vmxnet3_cleanup_msix(VMXNET3State *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    if (s->msix_used) {
        vmxnet3_unuse_msix_vectors(s, VMXNET3_MAX_INTRS);
        msix_uninit(d, &s->msix_bar, &s->msix_bar);
    }
}

static void
vmxnet3_cleanup_msi(VMXNET3State *s)
{
    PCIDevice *d = PCI_DEVICE(s);

    msi_uninit(d);
}

static const MemoryRegionOps b0_ops = {
    .read = vmxnet3_io_bar0_read,
    .write = vmxnet3_io_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
    },
};

static const MemoryRegionOps b1_ops = {
    .read = vmxnet3_io_bar1_read,
    .write = vmxnet3_io_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
            .min_access_size = 4,
            .max_access_size = 4,
    },
};

static uint64_t vmxnet3_device_serial_num(VMXNET3State *s)
{
    uint64_t dsn_payload;
    uint8_t *dsnp = (uint8_t *)&dsn_payload;

    dsnp[0] = 0xfe;
    dsnp[1] = s->conf.macaddr.a[3];
    dsnp[2] = s->conf.macaddr.a[4];
    dsnp[3] = s->conf.macaddr.a[5];
    dsnp[4] = s->conf.macaddr.a[0];
    dsnp[5] = s->conf.macaddr.a[1];
    dsnp[6] = s->conf.macaddr.a[2];
    dsnp[7] = 0xff;
    return dsn_payload;
}


#define VMXNET3_USE_64BIT         (true)
#define VMXNET3_PER_VECTOR_MASK   (false)

static void vmxnet3_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    VMXNET3State *s = VMXNET3(pci_dev);
    int ret;

    VMW_CBPRN("Starting init...");

    memory_region_init_io(&s->bar0, OBJECT(s), &b0_ops, s,
                          "vmxnet3-b0", VMXNET3_PT_REG_SIZE);
    pci_register_bar(pci_dev, VMXNET3_BAR0_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    memory_region_init_io(&s->bar1, OBJECT(s), &b1_ops, s,
                          "vmxnet3-b1", VMXNET3_VD_REG_SIZE);
    pci_register_bar(pci_dev, VMXNET3_BAR1_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1);

    memory_region_init(&s->msix_bar, OBJECT(s), "vmxnet3-msix-bar",
                       VMXNET3_MSIX_BAR_SIZE);
    pci_register_bar(pci_dev, VMXNET3_MSIX_BAR_IDX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);

    vmxnet3_reset_interrupt_states(s);

    /* Interrupt pin A */
    pci_dev->config[PCI_INTERRUPT_PIN] = 0x01;

    ret = msi_init(pci_dev, VMXNET3_MSI_OFFSET(s), VMXNET3_MAX_NMSIX_INTRS,
                   VMXNET3_USE_64BIT, VMXNET3_PER_VECTOR_MASK, NULL);
    /* Any error other than -ENOTSUP(board's MSI support is broken)
     * is a programming error. Fall back to INTx silently on -ENOTSUP */
    assert(!ret || ret == -ENOTSUP);

    if (!vmxnet3_init_msix(s)) {
        VMW_WRPRN("Failed to initialize MSI-X, configuration is inconsistent.");
    }

    vmxnet3_net_init(s);

    if (pci_is_express(pci_dev)) {
        if (pci_bus_is_express(pci_get_bus(pci_dev))) {
            pcie_endpoint_cap_init(pci_dev, VMXNET3_EXP_EP_OFFSET);
        }

        pcie_dev_ser_num_init(pci_dev, VMXNET3_DSN_OFFSET,
                              vmxnet3_device_serial_num(s));
    }
}

static void vmxnet3_instance_init(Object *obj)
{
    VMXNET3State *s = VMXNET3(obj);
    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static void vmxnet3_pci_uninit(PCIDevice *pci_dev)
{
    VMXNET3State *s = VMXNET3(pci_dev);

    VMW_CBPRN("Starting uninit...");

    vmxnet3_net_uninit(s);

    vmxnet3_cleanup_msix(s);

    vmxnet3_cleanup_msi(s);
}

static void vmxnet3_qdev_reset(DeviceState *dev)
{
    PCIDevice *d = PCI_DEVICE(dev);
    VMXNET3State *s = VMXNET3(d);

    VMW_CBPRN("Starting QDEV reset...");
    vmxnet3_reset(s);
}

static bool vmxnet3_mc_list_needed(void *opaque)
{
    return true;
}

static int vmxnet3_mcast_list_pre_load(void *opaque)
{
    VMXNET3State *s = opaque;

    s->mcast_list = g_malloc(s->mcast_list_buff_size);

    return 0;
}


static int vmxnet3_pre_save(void *opaque)
{
    VMXNET3State *s = opaque;

    s->mcast_list_buff_size = s->mcast_list_len * sizeof(MACAddr);

    return 0;
}

static const VMStateDescription vmxstate_vmxnet3_mcast_list = {
    .name = "vmxnet3/mcast_list",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = vmxnet3_mcast_list_pre_load,
    .needed = vmxnet3_mc_list_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(mcast_list, VMXNET3State, 0, NULL,
            mcast_list_buff_size),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmxnet3_ring = {
    .name = "vmxnet3-ring",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(pa, Vmxnet3Ring),
        VMSTATE_UINT32(size, Vmxnet3Ring),
        VMSTATE_UINT32(cell_size, Vmxnet3Ring),
        VMSTATE_UINT32(next, Vmxnet3Ring),
        VMSTATE_UINT8(gen, Vmxnet3Ring),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmxnet3_tx_stats = {
    .name = "vmxnet3-tx-stats",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(TSOPktsTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(TSOBytesTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(ucastPktsTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(ucastBytesTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(mcastPktsTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(mcastBytesTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(bcastPktsTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(bcastBytesTxOK, struct UPT1_TxStats),
        VMSTATE_UINT64(pktsTxError, struct UPT1_TxStats),
        VMSTATE_UINT64(pktsTxDiscard, struct UPT1_TxStats),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmxnet3_txq_descr = {
    .name = "vmxnet3-txq-descr",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(tx_ring, Vmxnet3TxqDescr, 0, vmstate_vmxnet3_ring,
                       Vmxnet3Ring),
        VMSTATE_STRUCT(comp_ring, Vmxnet3TxqDescr, 0, vmstate_vmxnet3_ring,
                       Vmxnet3Ring),
        VMSTATE_UINT8(intr_idx, Vmxnet3TxqDescr),
        VMSTATE_UINT64(tx_stats_pa, Vmxnet3TxqDescr),
        VMSTATE_STRUCT(txq_stats, Vmxnet3TxqDescr, 0, vmstate_vmxnet3_tx_stats,
                       struct UPT1_TxStats),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmxnet3_rx_stats = {
    .name = "vmxnet3-rx-stats",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(LROPktsRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(LROBytesRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(ucastPktsRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(ucastBytesRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(mcastPktsRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(mcastBytesRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(bcastPktsRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(bcastBytesRxOK, struct UPT1_RxStats),
        VMSTATE_UINT64(pktsRxOutOfBuf, struct UPT1_RxStats),
        VMSTATE_UINT64(pktsRxError, struct UPT1_RxStats),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmxnet3_rxq_descr = {
    .name = "vmxnet3-rxq-descr",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(rx_ring, Vmxnet3RxqDescr,
                             VMXNET3_RX_RINGS_PER_QUEUE, 0,
                             vmstate_vmxnet3_ring, Vmxnet3Ring),
        VMSTATE_STRUCT(comp_ring, Vmxnet3RxqDescr, 0, vmstate_vmxnet3_ring,
                       Vmxnet3Ring),
        VMSTATE_UINT8(intr_idx, Vmxnet3RxqDescr),
        VMSTATE_UINT64(rx_stats_pa, Vmxnet3RxqDescr),
        VMSTATE_STRUCT(rxq_stats, Vmxnet3RxqDescr, 0, vmstate_vmxnet3_rx_stats,
                       struct UPT1_RxStats),
        VMSTATE_END_OF_LIST()
    }
};

static int vmxnet3_post_load(void *opaque, int version_id)
{
    VMXNET3State *s = opaque;

    net_tx_pkt_init(&s->tx_pkt, s->max_tx_frags);
    net_rx_pkt_init(&s->rx_pkt);

    if (s->msix_used) {
        vmxnet3_use_msix_vectors(s, VMXNET3_MAX_INTRS);
    }

    if (!vmxnet3_validate_queues(s)) {
        return -1;
    }
    vmxnet3_validate_interrupts(s);

    return 0;
}

static const VMStateDescription vmstate_vmxnet3_int_state = {
    .name = "vmxnet3-int-state",
    .version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(is_masked, Vmxnet3IntState),
        VMSTATE_BOOL(is_pending, Vmxnet3IntState),
        VMSTATE_BOOL(is_asserted, Vmxnet3IntState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmxnet3 = {
    .name = "vmxnet3",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = vmxnet3_pre_save,
    .post_load = vmxnet3_post_load,
    .fields = (const VMStateField[]) {
            VMSTATE_PCI_DEVICE(parent_obj, VMXNET3State),
            VMSTATE_MSIX(parent_obj, VMXNET3State),
            VMSTATE_BOOL(rx_packets_compound, VMXNET3State),
            VMSTATE_BOOL(rx_vlan_stripping, VMXNET3State),
            VMSTATE_BOOL(lro_supported, VMXNET3State),
            VMSTATE_UINT32(rx_mode, VMXNET3State),
            VMSTATE_UINT32(mcast_list_len, VMXNET3State),
            VMSTATE_UINT32(mcast_list_buff_size, VMXNET3State),
            VMSTATE_UINT32_ARRAY(vlan_table, VMXNET3State, VMXNET3_VFT_SIZE),
            VMSTATE_UINT32(mtu, VMXNET3State),
            VMSTATE_UINT16(max_rx_frags, VMXNET3State),
            VMSTATE_UINT32(max_tx_frags, VMXNET3State),
            VMSTATE_UINT8(event_int_idx, VMXNET3State),
            VMSTATE_BOOL(auto_int_masking, VMXNET3State),
            VMSTATE_UINT8(txq_num, VMXNET3State),
            VMSTATE_UINT8(rxq_num, VMXNET3State),
            VMSTATE_UINT32(device_active, VMXNET3State),
            VMSTATE_UINT32(last_command, VMXNET3State),
            VMSTATE_UINT32(link_status_and_speed, VMXNET3State),
            VMSTATE_UINT32(temp_mac, VMXNET3State),
            VMSTATE_UINT64(drv_shmem, VMXNET3State),
            VMSTATE_UINT64(temp_shared_guest_driver_memory, VMXNET3State),

            VMSTATE_STRUCT_ARRAY(txq_descr, VMXNET3State,
                VMXNET3_DEVICE_MAX_TX_QUEUES, 0, vmstate_vmxnet3_txq_descr,
                Vmxnet3TxqDescr),
            VMSTATE_STRUCT_ARRAY(rxq_descr, VMXNET3State,
                VMXNET3_DEVICE_MAX_RX_QUEUES, 0, vmstate_vmxnet3_rxq_descr,
                Vmxnet3RxqDescr),
            VMSTATE_STRUCT_ARRAY(interrupt_states, VMXNET3State,
                VMXNET3_MAX_INTRS, 0, vmstate_vmxnet3_int_state,
                Vmxnet3IntState),

            VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmxstate_vmxnet3_mcast_list,
        NULL
    }
};

static Property vmxnet3_properties[] = {
    DEFINE_NIC_PROPERTIES(VMXNET3State, conf),
    DEFINE_PROP_BIT("x-old-msi-offsets", VMXNET3State, compat_flags,
                    VMXNET3_COMPAT_FLAG_OLD_MSI_OFFSETS_BIT, false),
    DEFINE_PROP_BIT("x-disable-pcie", VMXNET3State, compat_flags,
                    VMXNET3_COMPAT_FLAG_DISABLE_PCIE_BIT, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmxnet3_realize(DeviceState *qdev, Error **errp)
{
    VMXNET3Class *vc = VMXNET3_DEVICE_GET_CLASS(qdev);
    PCIDevice *pci_dev = PCI_DEVICE(qdev);
    VMXNET3State *s = VMXNET3(qdev);

    if (!(s->compat_flags & VMXNET3_COMPAT_FLAG_DISABLE_PCIE)) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    vc->parent_dc_realize(qdev, errp);
}

static void vmxnet3_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *c = PCI_DEVICE_CLASS(class);
    VMXNET3Class *vc = VMXNET3_DEVICE_CLASS(class);

    c->realize = vmxnet3_pci_realize;
    c->exit = vmxnet3_pci_uninit;
    c->vendor_id = PCI_VENDOR_ID_VMWARE;
    c->device_id = PCI_DEVICE_ID_VMWARE_VMXNET3;
    c->revision = PCI_DEVICE_ID_VMWARE_VMXNET3_REVISION;
    c->romfile = "efi-vmxnet3.rom";
    c->class_id = PCI_CLASS_NETWORK_ETHERNET;
    c->subsystem_vendor_id = PCI_VENDOR_ID_VMWARE;
    c->subsystem_id = PCI_DEVICE_ID_VMWARE_VMXNET3;
    device_class_set_parent_realize(dc, vmxnet3_realize,
                                    &vc->parent_dc_realize);
    dc->desc = "VMWare Paravirtualized Ethernet v3";
    dc->reset = vmxnet3_qdev_reset;
    dc->vmsd = &vmstate_vmxnet3;
    device_class_set_props(dc, vmxnet3_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo vmxnet3_info = {
    .name          = TYPE_VMXNET3,
    .parent        = TYPE_PCI_DEVICE,
    .class_size    = sizeof(VMXNET3Class),
    .instance_size = sizeof(VMXNET3State),
    .class_init    = vmxnet3_class_init,
    .instance_init = vmxnet3_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static void vmxnet3_register_types(void)
{
    VMW_CBPRN("vmxnet3_register_types called...");
    type_register_static(&vmxnet3_info);
}

type_init(vmxnet3_register_types)
