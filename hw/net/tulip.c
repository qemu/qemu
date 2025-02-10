/*
 * QEMU TULIP Emulation
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/nvram/eeprom93xx.h"
#include "migration/vmstate.h"
#include "system/system.h"
#include "tulip.h"
#include "trace.h"
#include "net/eth.h"

struct TULIPState {
    PCIDevice dev;
    MemoryRegion io;
    MemoryRegion memory;
    NICConf c;
    qemu_irq irq;
    NICState *nic;
    eeprom_t *eeprom;
    uint32_t csr[16];

    /* state for MII */
    uint32_t old_csr9;
    uint32_t mii_word;
    uint32_t mii_bitcnt;

    hwaddr current_rx_desc;
    hwaddr current_tx_desc;

    uint8_t rx_frame[2048];
    uint8_t tx_frame[2048];
    uint16_t tx_frame_len;
    uint16_t rx_frame_len;
    uint16_t rx_frame_size;

    uint32_t rx_status;
    uint8_t filter[16][6];
};

static const VMStateDescription vmstate_pci_tulip = {
    .name = "tulip",
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, TULIPState),
        VMSTATE_UINT32_ARRAY(csr, TULIPState, 16),
        VMSTATE_UINT32(old_csr9, TULIPState),
        VMSTATE_UINT32(mii_word, TULIPState),
        VMSTATE_UINT32(mii_bitcnt, TULIPState),
        VMSTATE_UINT64(current_rx_desc, TULIPState),
        VMSTATE_UINT64(current_tx_desc, TULIPState),
        VMSTATE_BUFFER(rx_frame, TULIPState),
        VMSTATE_BUFFER(tx_frame, TULIPState),
        VMSTATE_UINT16(rx_frame_len, TULIPState),
        VMSTATE_UINT16(tx_frame_len, TULIPState),
        VMSTATE_UINT16(rx_frame_size, TULIPState),
        VMSTATE_UINT32(rx_status, TULIPState),
        VMSTATE_UINT8_2DARRAY(filter, TULIPState, 16, 6),
        VMSTATE_END_OF_LIST()
    }
};

static void tulip_desc_read(TULIPState *s, hwaddr p,
        struct tulip_descriptor *desc)
{
    const MemTxAttrs attrs = { .memory = true };

    if (s->csr[0] & CSR0_DBO) {
        ldl_be_pci_dma(&s->dev, p, &desc->status, attrs);
        ldl_be_pci_dma(&s->dev, p + 4, &desc->control, attrs);
        ldl_be_pci_dma(&s->dev, p + 8, &desc->buf_addr1, attrs);
        ldl_be_pci_dma(&s->dev, p + 12, &desc->buf_addr2, attrs);
    } else {
        ldl_le_pci_dma(&s->dev, p, &desc->status, attrs);
        ldl_le_pci_dma(&s->dev, p + 4, &desc->control, attrs);
        ldl_le_pci_dma(&s->dev, p + 8, &desc->buf_addr1, attrs);
        ldl_le_pci_dma(&s->dev, p + 12, &desc->buf_addr2, attrs);
    }
}

static void tulip_desc_write(TULIPState *s, hwaddr p,
        struct tulip_descriptor *desc)
{
    const MemTxAttrs attrs = { .memory = true };

    if (s->csr[0] & CSR0_DBO) {
        stl_be_pci_dma(&s->dev, p, desc->status, attrs);
        stl_be_pci_dma(&s->dev, p + 4, desc->control, attrs);
        stl_be_pci_dma(&s->dev, p + 8, desc->buf_addr1, attrs);
        stl_be_pci_dma(&s->dev, p + 12, desc->buf_addr2, attrs);
    } else {
        stl_le_pci_dma(&s->dev, p, desc->status, attrs);
        stl_le_pci_dma(&s->dev, p + 4, desc->control, attrs);
        stl_le_pci_dma(&s->dev, p + 8, desc->buf_addr1, attrs);
        stl_le_pci_dma(&s->dev, p + 12, desc->buf_addr2, attrs);
    }
}

static void tulip_update_int(TULIPState *s)
{
    uint32_t ie = s->csr[5] & s->csr[7];
    bool assert = false;

    s->csr[5] &= ~(CSR5_AIS | CSR5_NIS);

    if (ie & (CSR5_TI | CSR5_TU | CSR5_RI | CSR5_GTE | CSR5_ERI)) {
        s->csr[5] |= CSR5_NIS;
    }

    if (ie & (CSR5_LC | CSR5_GPI | CSR5_FBE | CSR5_LNF | CSR5_ETI | CSR5_RWT |
              CSR5_RPS | CSR5_RU | CSR5_UNF | CSR5_LNP_ANC | CSR5_TJT |
              CSR5_TPS)) {
        s->csr[5] |= CSR5_AIS;
    }

    assert = s->csr[5] & s->csr[7] & (CSR5_AIS | CSR5_NIS);
    trace_tulip_irq(s->csr[5], s->csr[7], assert ? "assert" : "deassert");
    qemu_set_irq(s->irq, assert);
}

static bool tulip_rx_stopped(TULIPState *s)
{
    return ((s->csr[5] >> CSR5_RS_SHIFT) & CSR5_RS_MASK) == CSR5_RS_STOPPED;
}

static void tulip_dump_tx_descriptor(TULIPState *s,
        struct tulip_descriptor *desc)
{
    trace_tulip_descriptor("TX ", s->current_tx_desc,
                desc->status, desc->control >> 22,
                desc->control & 0x7ff, (desc->control >> 11) & 0x7ff,
                desc->buf_addr1, desc->buf_addr2);
}

static void tulip_dump_rx_descriptor(TULIPState *s,
        struct tulip_descriptor *desc)
{
    trace_tulip_descriptor("RX ", s->current_rx_desc,
                desc->status, desc->control >> 22,
                desc->control & 0x7ff, (desc->control >> 11) & 0x7ff,
                desc->buf_addr1, desc->buf_addr2);
}

static void tulip_next_rx_descriptor(TULIPState *s,
    struct tulip_descriptor *desc)
{
    if (desc->control & RDES1_RER) {
        s->current_rx_desc = s->csr[3];
    } else if (desc->control & RDES1_RCH) {
        s->current_rx_desc = desc->buf_addr2;
    } else {
        s->current_rx_desc += sizeof(struct tulip_descriptor) +
                (((s->csr[0] >> CSR0_DSL_SHIFT) & CSR0_DSL_MASK) << 2);
    }
    s->current_rx_desc &= ~3ULL;
}

static void tulip_copy_rx_bytes(TULIPState *s, struct tulip_descriptor *desc)
{
    int len1 = (desc->control >> RDES1_BUF1_SIZE_SHIFT) & RDES1_BUF1_SIZE_MASK;
    int len2 = (desc->control >> RDES1_BUF2_SIZE_SHIFT) & RDES1_BUF2_SIZE_MASK;
    int len;

    if (s->rx_frame_len && len1) {
        if (s->rx_frame_len > len1) {
            len = len1;
        } else {
            len = s->rx_frame_len;
        }

        pci_dma_write(&s->dev, desc->buf_addr1, s->rx_frame +
            (s->rx_frame_size - s->rx_frame_len), len);
        s->rx_frame_len -= len;
    }

    if (s->rx_frame_len && len2) {
        if (s->rx_frame_len > len2) {
            len = len2;
        } else {
            len = s->rx_frame_len;
        }

        pci_dma_write(&s->dev, desc->buf_addr2, s->rx_frame +
            (s->rx_frame_size - s->rx_frame_len), len);
        s->rx_frame_len -= len;
    }
}

static bool tulip_filter_address(TULIPState *s, const uint8_t *addr)
{
    static const char broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    bool ret = false;
    int i;

    for (i = 0; i < 16 && ret == false; i++) {
        if (!memcmp(&s->filter[i], addr, ETH_ALEN)) {
            ret = true;
        }
    }

    if (!memcmp(addr, broadcast, ETH_ALEN)) {
        return true;
    }

    if (s->csr[6] & (CSR6_PR | CSR6_RA)) {
        /* Promiscuous mode enabled */
        s->rx_status |= RDES0_FF;
        return true;
    }

    if ((s->csr[6] & CSR6_PM) && (addr[0] & 1)) {
        /* Pass all Multicast enabled */
        s->rx_status |= RDES0_MF;
        return true;
    }

    if (s->csr[6] & CSR6_IF) {
        ret ^= true;
    }
    return ret;
}

static ssize_t tulip_receive(TULIPState *s, const uint8_t *buf, size_t size)
{
    struct tulip_descriptor desc;

    trace_tulip_receive(buf, size);

    if (size < 14 || size > sizeof(s->rx_frame) - 4
        || s->rx_frame_len || tulip_rx_stopped(s)) {
        return 0;
    }

    if (!tulip_filter_address(s, buf)) {
        return size;
    }

    do {
        tulip_desc_read(s, s->current_rx_desc, &desc);
        tulip_dump_rx_descriptor(s, &desc);

        if (!(desc.status & RDES0_OWN)) {
            s->csr[5] |= CSR5_RU;
            tulip_update_int(s);
            return s->rx_frame_size - s->rx_frame_len;
        }
        desc.status = 0;

        if (!s->rx_frame_len) {
            s->rx_frame_size = size + 4;
            s->rx_status = RDES0_LS |
                 ((s->rx_frame_size & RDES0_FL_MASK) << RDES0_FL_SHIFT);
            desc.status |= RDES0_FS;
            memcpy(s->rx_frame, buf, size);
            s->rx_frame_len = s->rx_frame_size;
        }

        tulip_copy_rx_bytes(s, &desc);

        if (!s->rx_frame_len) {
            desc.status |= s->rx_status;
            s->csr[5] |= CSR5_RI;
            tulip_update_int(s);
        }
        tulip_dump_rx_descriptor(s, &desc);
        tulip_desc_write(s, s->current_rx_desc, &desc);
        tulip_next_rx_descriptor(s, &desc);
    } while (s->rx_frame_len);
    return size;
}

static ssize_t tulip_receive_nc(NetClientState *nc,
                             const uint8_t *buf, size_t size)
{
    return tulip_receive(qemu_get_nic_opaque(nc), buf, size);
}

static NetClientInfo net_tulip_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = tulip_receive_nc,
};

static const char *tulip_reg_name(const hwaddr addr)
{
    switch (addr) {
    case CSR(0):
        return "CSR0";

    case CSR(1):
        return "CSR1";

    case CSR(2):
        return "CSR2";

    case CSR(3):
        return "CSR3";

    case CSR(4):
        return "CSR4";

    case CSR(5):
        return "CSR5";

    case CSR(6):
        return "CSR6";

    case CSR(7):
        return "CSR7";

    case CSR(8):
        return "CSR8";

    case CSR(9):
        return "CSR9";

    case CSR(10):
        return "CSR10";

    case CSR(11):
        return "CSR11";

    case CSR(12):
        return "CSR12";

    case CSR(13):
        return "CSR13";

    case CSR(14):
        return "CSR14";

    case CSR(15):
        return "CSR15";

    default:
        break;
    }
    return "";
}

static const char *tulip_rx_state_name(int state)
{
    switch (state) {
    case CSR5_RS_STOPPED:
        return "STOPPED";

    case CSR5_RS_RUNNING_FETCH:
        return "RUNNING/FETCH";

    case CSR5_RS_RUNNING_CHECK_EOR:
        return "RUNNING/CHECK EOR";

    case CSR5_RS_RUNNING_WAIT_RECEIVE:
        return "WAIT RECEIVE";

    case CSR5_RS_SUSPENDED:
        return "SUSPENDED";

    case CSR5_RS_RUNNING_CLOSE:
        return "RUNNING/CLOSE";

    case CSR5_RS_RUNNING_FLUSH:
        return "RUNNING/FLUSH";

    case CSR5_RS_RUNNING_QUEUE:
        return "RUNNING/QUEUE";

    default:
        break;
    }
    return "";
}

static const char *tulip_tx_state_name(int state)
{
    switch (state) {
    case CSR5_TS_STOPPED:
        return "STOPPED";

    case CSR5_TS_RUNNING_FETCH:
        return "RUNNING/FETCH";

    case CSR5_TS_RUNNING_WAIT_EOT:
        return "RUNNING/WAIT EOT";

    case CSR5_TS_RUNNING_READ_BUF:
        return "RUNNING/READ BUF";

    case CSR5_TS_RUNNING_SETUP:
        return "RUNNING/SETUP";

    case CSR5_TS_SUSPENDED:
        return "SUSPENDED";

    case CSR5_TS_RUNNING_CLOSE:
        return "RUNNING/CLOSE";

    default:
        break;
    }
    return "";
}

static void tulip_update_rs(TULIPState *s, int state)
{
    s->csr[5] &= ~(CSR5_RS_MASK << CSR5_RS_SHIFT);
    s->csr[5] |= (state & CSR5_RS_MASK) << CSR5_RS_SHIFT;
    trace_tulip_rx_state(tulip_rx_state_name(state));
}

static uint16_t tulip_mdi_default[] = {
    /* MDI Registers 0 - 6, 7 */
    0x3100, 0xf02c, 0x7810, 0x0000, 0x0501, 0x4181, 0x0000, 0x0000,
    /* MDI Registers 8 - 15 */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 16 - 31 */
    0x0003, 0x0000, 0x0001, 0x0000, 0x3b40, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

/* Readonly mask for MDI (PHY) registers */
static const uint16_t tulip_mdi_mask[] = {
    0x0000, 0xffff, 0xffff, 0xffff, 0xc01f, 0xffff, 0xffff, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0fff, 0x0000, 0xffff, 0xffff, 0x0000, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static uint16_t tulip_mii_read(TULIPState *s, int phy, int reg)
{
    uint16_t ret = 0;
    if (phy == 1) {
        ret = tulip_mdi_default[reg];
    }
    trace_tulip_mii_read(phy, reg, ret);
    return ret;
}

static void tulip_mii_write(TULIPState *s, int phy, int reg, uint16_t data)
{
    trace_tulip_mii_write(phy, reg, data);

    if (phy != 1) {
        return;
    }

    tulip_mdi_default[reg] &= ~tulip_mdi_mask[reg];
    tulip_mdi_default[reg] |= (data & tulip_mdi_mask[reg]);
}

static void tulip_mii(TULIPState *s)
{
    uint32_t changed = s->old_csr9 ^ s->csr[9];
    uint16_t data;
    int op, phy, reg;

    if (!(changed & CSR9_MDC)) {
        return;
    }

    if (!(s->csr[9] & CSR9_MDC)) {
        return;
    }

    s->mii_bitcnt++;
    s->mii_word <<= 1;

    if (s->csr[9] & CSR9_MDO && (s->mii_bitcnt < 16 ||
        !(s->csr[9] & CSR9_MII))) {
        /* write op or address bits */
        s->mii_word |= 1;
    }

    if (s->mii_bitcnt >= 16 && (s->csr[9] & CSR9_MII)) {
        if (s->mii_word & 0x8000) {
            s->csr[9] |= CSR9_MDI;
        } else {
            s->csr[9] &= ~CSR9_MDI;
        }
    }

    if (s->mii_word == 0xffffffff) {
        s->mii_bitcnt = 0;
    } else if (s->mii_bitcnt == 16) {
        op = (s->mii_word >> 12) & 0x0f;
        phy = (s->mii_word >> 7) & 0x1f;
        reg = (s->mii_word >> 2) & 0x1f;

        if (op == 6) {
            s->mii_word = tulip_mii_read(s, phy, reg);
        }
    } else if (s->mii_bitcnt == 32) {
            op = (s->mii_word >> 28) & 0x0f;
            phy = (s->mii_word >> 23) & 0x1f;
            reg = (s->mii_word >> 18) & 0x1f;
            data = s->mii_word & 0xffff;

        if (op == 5) {
            tulip_mii_write(s, phy, reg, data);
        }
    }
}

static uint32_t tulip_csr9_read(TULIPState *s)
{
    if (s->csr[9] & CSR9_SR) {
        if (eeprom93xx_read(s->eeprom)) {
            s->csr[9] |= CSR9_SR_DO;
        } else {
            s->csr[9] &= ~CSR9_SR_DO;
        }
    }

    tulip_mii(s);
    return s->csr[9];
}

static void tulip_update_ts(TULIPState *s, int state)
{
        s->csr[5] &= ~(CSR5_TS_MASK << CSR5_TS_SHIFT);
        s->csr[5] |= (state & CSR5_TS_MASK) << CSR5_TS_SHIFT;
        trace_tulip_tx_state(tulip_tx_state_name(state));
}

static uint64_t tulip_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    TULIPState *s = opaque;
    uint64_t data = 0;

    switch (addr) {
    case CSR(9):
        data = tulip_csr9_read(s);
        break;

    case CSR(12):
        /* Fake autocompletion complete until we have PHY emulation */
        data = 5 << CSR12_ANS_SHIFT;
        break;

    default:
        if (addr & 7) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: read access at unknown address"
                " 0x%"PRIx64"\n", __func__, addr);
        } else {
            data = s->csr[addr >> 3];
        }
        break;
    }
    trace_tulip_reg_read(addr, tulip_reg_name(addr), size, data);
    return data;
}

static void tulip_tx(TULIPState *s, struct tulip_descriptor *desc)
{
    if (s->tx_frame_len) {
        if ((s->csr[6] >> CSR6_OM_SHIFT) & CSR6_OM_MASK) {
            /* Internal or external Loopback */
            tulip_receive(s, s->tx_frame, s->tx_frame_len);
        } else if (s->tx_frame_len <= sizeof(s->tx_frame)) {
            qemu_send_packet(qemu_get_queue(s->nic),
                s->tx_frame, s->tx_frame_len);
        }
    }

    if (desc->control & TDES1_IC) {
        s->csr[5] |= CSR5_TI;
        tulip_update_int(s);
    }
}

static int tulip_copy_tx_buffers(TULIPState *s, struct tulip_descriptor *desc)
{
    int len1 = (desc->control >> TDES1_BUF1_SIZE_SHIFT) & TDES1_BUF1_SIZE_MASK;
    int len2 = (desc->control >> TDES1_BUF2_SIZE_SHIFT) & TDES1_BUF2_SIZE_MASK;

    if (s->tx_frame_len + len1 > sizeof(s->tx_frame)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: descriptor overflow (ofs: %u, len:%d, size:%zu)\n",
                      __func__, s->tx_frame_len, len1, sizeof(s->tx_frame));
        return -1;
    }
    if (len1) {
        pci_dma_read(&s->dev, desc->buf_addr1,
            s->tx_frame + s->tx_frame_len, len1);
        s->tx_frame_len += len1;
    }

    if (s->tx_frame_len + len2 > sizeof(s->tx_frame)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: descriptor overflow (ofs: %u, len:%d, size:%zu)\n",
                      __func__, s->tx_frame_len, len2, sizeof(s->tx_frame));
        return -1;
    }
    if (len2) {
        pci_dma_read(&s->dev, desc->buf_addr2,
            s->tx_frame + s->tx_frame_len, len2);
        s->tx_frame_len += len2;
    }
    desc->status = (len1 + len2) ? 0 : 0x7fffffff;

    return 0;
}

static void tulip_setup_filter_addr(TULIPState *s, uint8_t *buf, int n)
{
    int offset = n * 12;

    s->filter[n][0] = buf[offset];
    s->filter[n][1] = buf[offset + 1];

    s->filter[n][2] = buf[offset + 4];
    s->filter[n][3] = buf[offset + 5];

    s->filter[n][4] = buf[offset + 8];
    s->filter[n][5] = buf[offset + 9];

    trace_tulip_setup_filter(n, s->filter[n][5], s->filter[n][4],
            s->filter[n][3], s->filter[n][2], s->filter[n][1], s->filter[n][0]);
}

static void tulip_setup_frame(TULIPState *s,
        struct tulip_descriptor *desc)
{
    uint8_t buf[4096];
    int len = (desc->control >> TDES1_BUF1_SIZE_SHIFT) & TDES1_BUF1_SIZE_MASK;
    int i;

    trace_tulip_setup_frame();

    if (len == 192) {
        pci_dma_read(&s->dev, desc->buf_addr1, buf, len);
        for (i = 0; i < 16; i++) {
            tulip_setup_filter_addr(s, buf, i);
        }
    }

    desc->status = 0x7fffffff;

    if (desc->control & TDES1_IC) {
        s->csr[5] |= CSR5_TI;
        tulip_update_int(s);
    }
}

static void tulip_next_tx_descriptor(TULIPState *s,
    struct tulip_descriptor *desc)
{
    if (desc->control & TDES1_TER) {
        s->current_tx_desc = s->csr[4];
    } else if (desc->control & TDES1_TCH) {
        s->current_tx_desc = desc->buf_addr2;
    } else {
        s->current_tx_desc += sizeof(struct tulip_descriptor) +
                (((s->csr[0] >> CSR0_DSL_SHIFT) & CSR0_DSL_MASK) << 2);
    }
    s->current_tx_desc &= ~3ULL;
}

static uint32_t tulip_ts(TULIPState *s)
{
    return (s->csr[5] >> CSR5_TS_SHIFT) & CSR5_TS_MASK;
}

static void tulip_xmit_list_update(TULIPState *s)
{
#define TULIP_DESC_MAX 128
    uint8_t i = 0;
    struct tulip_descriptor desc;

    if (tulip_ts(s) != CSR5_TS_SUSPENDED) {
        return;
    }

    for (i = 0; i < TULIP_DESC_MAX; i++) {
        tulip_desc_read(s, s->current_tx_desc, &desc);
        tulip_dump_tx_descriptor(s, &desc);

        if (!(desc.status & TDES0_OWN)) {
            tulip_update_ts(s, CSR5_TS_SUSPENDED);
            s->csr[5] |= CSR5_TU;
            tulip_update_int(s);
            return;
        }

        if (desc.control & TDES1_SET) {
            tulip_setup_frame(s, &desc);
        } else {
            if (desc.control & TDES1_FS) {
                s->tx_frame_len = 0;
            }

            if (!tulip_copy_tx_buffers(s, &desc)) {
                if (desc.control & TDES1_LS) {
                    tulip_tx(s, &desc);
                }
            }
        }
        tulip_desc_write(s, s->current_tx_desc, &desc);
        tulip_next_tx_descriptor(s, &desc);
    }
}

static void tulip_csr9_write(TULIPState *s, uint32_t old_val,
        uint32_t new_val)
{
    if (new_val & CSR9_SR) {
        eeprom93xx_write(s->eeprom,
            !!(new_val & CSR9_SR_CS),
            !!(new_val & CSR9_SR_SK),
            !!(new_val & CSR9_SR_DI));
    }
}

static void tulip_reset(TULIPState *s)
{
    trace_tulip_reset();

    s->csr[0] = 0xfe000000;
    s->csr[1] = 0xffffffff;
    s->csr[2] = 0xffffffff;
    s->csr[5] = 0xf0000000;
    s->csr[6] = 0x32000040;
    s->csr[7] = 0xf3fe0000;
    s->csr[8] = 0xe0000000;
    s->csr[9] = 0xfff483ff;
    s->csr[11] = 0xfffe0000;
    s->csr[12] = 0x000000c6;
    s->csr[13] = 0xffff0000;
    s->csr[14] = 0xffffffff;
    s->csr[15] = 0x8ff00000;
}

static void tulip_qdev_reset(DeviceState *dev)
{
    PCIDevice *d = PCI_DEVICE(dev);
    TULIPState *s = TULIP(d);

    tulip_reset(s);
}

static void tulip_write(void *opaque, hwaddr addr,
                           uint64_t data, unsigned size)
{
    TULIPState *s = opaque;
    trace_tulip_reg_write(addr, tulip_reg_name(addr), size, data);

    switch (addr) {
    case CSR(0):
        s->csr[0] = data;
        if (data & CSR0_SWR) {
            tulip_reset(s);
            tulip_update_int(s);
        }
        break;

    case CSR(1):
        tulip_xmit_list_update(s);
        break;

    case CSR(2):
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
        break;

    case CSR(3):
        s->csr[3] = data & ~3ULL;
        s->current_rx_desc = s->csr[3];
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
        break;

    case CSR(4):
        s->csr[4] = data & ~3ULL;
        s->current_tx_desc = s->csr[4];
        tulip_xmit_list_update(s);
        break;

    case CSR(5):
        /* Status register, write clears bit */
        s->csr[5] &= ~(data & (CSR5_TI | CSR5_TPS | CSR5_TU | CSR5_TJT |
                               CSR5_LNP_ANC | CSR5_UNF | CSR5_RI | CSR5_RU |
                               CSR5_RPS | CSR5_RWT | CSR5_ETI | CSR5_GTE |
                               CSR5_LNF | CSR5_FBE | CSR5_ERI | CSR5_AIS |
                               CSR5_NIS | CSR5_GPI | CSR5_LC));
        tulip_update_int(s);
        break;

    case CSR(6):
        s->csr[6] = data;
        if (s->csr[6] & CSR6_SR) {
            tulip_update_rs(s, CSR5_RS_RUNNING_WAIT_RECEIVE);
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        } else {
            tulip_update_rs(s, CSR5_RS_STOPPED);
        }

        if (s->csr[6] & CSR6_ST) {
            tulip_update_ts(s, CSR5_TS_SUSPENDED);
            tulip_xmit_list_update(s);
        } else {
            tulip_update_ts(s, CSR5_TS_STOPPED);
        }
        break;

    case CSR(7):
        s->csr[7] = data;
        tulip_update_int(s);
        break;

    case CSR(8):
        s->csr[9] = data;
        break;

    case CSR(9):
        tulip_csr9_write(s, s->csr[9], data);
        /* don't clear MII read data */
        s->csr[9] &= CSR9_MDI;
        s->csr[9] |= (data & ~CSR9_MDI);
        tulip_mii(s);
        s->old_csr9 = s->csr[9];
        break;

    case CSR(10):
        s->csr[10] = data;
        break;

    case CSR(11):
        s->csr[11] = data;
        break;

    case CSR(12):
        /* SIA Status register, some bits are cleared by writing 1 */
        s->csr[12] &= ~(data & (CSR12_MRA | CSR12_TRA | CSR12_ARA));
        break;

    case CSR(13):
        s->csr[13] = data;
        break;

    case CSR(14):
        s->csr[14] = data;
        break;

    case CSR(15):
        s->csr[15] = data;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to CSR at unknown address "
                "0x%"PRIx64"\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps tulip_ops = {
    .read = tulip_read,
    .write = tulip_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void tulip_idblock_crc(TULIPState *s, uint16_t *srom)
{
    int word;
    int bit;
    unsigned char bitval, crc;
    const int len = 9;
    crc = -1;

    for (word = 0; word < len; word++) {
        for (bit = 15; bit >= 0; bit--) {
            if ((word == (len - 1)) && (bit == 7)) {
                /*
                 * Insert the correct CRC result into input data stream
                 * in place.
                 */
                srom[len - 1] = (srom[len - 1] & 0xff00) | (unsigned short)crc;
                break;
            }
            bitval = ((srom[word] >> bit) & 1) ^ ((crc >> 7) & 1);
            crc = crc << 1;
            if (bitval == 1) {
                crc ^= 6;
                crc |= 0x01;
            }
        }
    }
}

static uint16_t tulip_srom_crc(TULIPState *s, uint8_t *eeprom, size_t len)
{
    unsigned long crc = 0xffffffff;
    unsigned long flippedcrc = 0;
    unsigned char currentbyte;
    unsigned int msb, bit, i;

    for (i = 0; i < len; i++) {
        currentbyte = eeprom[i];
        for (bit = 0; bit < 8; bit++) {
            msb = (crc >> 31) & 1;
            crc <<= 1;
            if (msb ^ (currentbyte & 1)) {
                crc ^= 0x04c11db6;
                crc |= 0x00000001;
            }
            currentbyte >>= 1;
        }
    }

    for (i = 0; i < 32; i++) {
        flippedcrc <<= 1;
        bit = crc & 1;
        crc >>= 1;
        flippedcrc += bit;
    }
    return (flippedcrc ^ 0xffffffff) & 0xffff;
}

static const uint8_t eeprom_default[128] = {
    0x3c, 0x10, 0x4f, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x56, 0x08, 0x04, 0x01, 0x00, 0x80, 0x48, 0xb3,
    0x0e, 0xa7, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x8d, 0x03, 0x00, 0x00, 0x00, 0x00, 0x78,
    0xe0, 0x01, 0x00, 0x50, 0x00, 0x18, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x6b,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
    0x48, 0xb3, 0x0e, 0xa7, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void tulip_fill_eeprom(TULIPState *s)
{
    uint16_t *eeprom = eeprom93xx_data(s->eeprom);
    memcpy(eeprom, eeprom_default, 128);

    /* patch in our mac address */
    eeprom[10] = cpu_to_le16(s->c.macaddr.a[0] | (s->c.macaddr.a[1] << 8));
    eeprom[11] = cpu_to_le16(s->c.macaddr.a[2] | (s->c.macaddr.a[3] << 8));
    eeprom[12] = cpu_to_le16(s->c.macaddr.a[4] | (s->c.macaddr.a[5] << 8));
    tulip_idblock_crc(s, eeprom);
    eeprom[63] = cpu_to_le16(tulip_srom_crc(s, (uint8_t *)eeprom, 126));
}

static void pci_tulip_realize(PCIDevice *pci_dev, Error **errp)
{
    TULIPState *s = DO_UPCAST(TULIPState, dev, pci_dev);
    uint8_t *pci_conf;

    pci_conf = s->dev.config;
    pci_conf[PCI_INTERRUPT_PIN] = 1; /* interrupt pin A */

    qemu_macaddr_default_if_unset(&s->c.macaddr);

    s->eeprom = eeprom93xx_new(&pci_dev->qdev, 64);
    tulip_fill_eeprom(s);

    memory_region_init_io(&s->io, OBJECT(&s->dev), &tulip_ops, s,
            "tulip-io", 128);

    memory_region_init_io(&s->memory, OBJECT(&s->dev), &tulip_ops, s,
            "tulip-mem", 128);

    pci_register_bar(&s->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
    pci_register_bar(&s->dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->memory);

    s->irq = pci_allocate_irq(&s->dev);

    s->nic = qemu_new_nic(&net_tulip_info, &s->c,
                          object_get_typename(OBJECT(pci_dev)),
                          pci_dev->qdev.id,
                          &pci_dev->qdev.mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->c.macaddr.a);
}

static void pci_tulip_exit(PCIDevice *pci_dev)
{
    TULIPState *s = DO_UPCAST(TULIPState, dev, pci_dev);

    qemu_del_nic(s->nic);
    qemu_free_irq(s->irq);
    eeprom93xx_free(&pci_dev->qdev, s->eeprom);
}

static void tulip_instance_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    TULIPState *d = DO_UPCAST(TULIPState, dev, pci_dev);

    device_add_bootindex_property(obj, &d->c.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  &pci_dev->qdev);
}

static const Property tulip_properties[] = {
    DEFINE_NIC_PROPERTIES(TULIPState, c),
};

static void tulip_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_tulip_realize;
    k->exit = pci_tulip_exit;
    k->vendor_id = PCI_VENDOR_ID_DEC;
    k->device_id = PCI_DEVICE_ID_DEC_21143;
    k->subsystem_vendor_id = PCI_VENDOR_ID_HP;
    k->subsystem_id = 0x104f;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->vmsd = &vmstate_pci_tulip;
    device_class_set_props(dc, tulip_properties);
    device_class_set_legacy_reset(dc, tulip_qdev_reset);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo tulip_info = {
    .name          = TYPE_TULIP,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(TULIPState),
    .class_init    = tulip_class_init,
    .instance_init = tulip_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void tulip_register_types(void)
{
    type_register_static(&tulip_info);
}

type_init(tulip_register_types)
