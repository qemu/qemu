/*
 * SMSC LAN9118 Ethernet interface emulation
 *
 * Copyright (c) 2009 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2
 */

#include "sysbus.h"
#include "net.h"
#include "devices.h"
#include "sysemu.h"
/* For crc32 */
#include <zlib.h>

//#define DEBUG_LAN9118

#ifdef DEBUG_LAN9118
#define DPRINTF(fmt, ...) \
do { printf("lan9118: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { hw_error("lan9118: error: " fmt , ## __VA_ARGS__);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "lan9118: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

#define CSR_ID_REV      0x50
#define CSR_IRQ_CFG     0x54
#define CSR_INT_STS     0x58
#define CSR_INT_EN      0x5c
#define CSR_BYTE_TEST   0x64
#define CSR_FIFO_INT    0x68
#define CSR_RX_CFG      0x6c
#define CSR_TX_CFG      0x70
#define CSR_HW_CFG      0x74
#define CSR_RX_DP_CTRL  0x78
#define CSR_RX_FIFO_INF 0x7c
#define CSR_TX_FIFO_INF 0x80
#define CSR_PMT_CTRL    0x84
#define CSR_GPIO_CFG    0x88
#define CSR_GPT_CFG     0x8c
#define CSR_GPT_CNT     0x90
#define CSR_WORD_SWAP   0x98
#define CSR_FREE_RUN    0x9c
#define CSR_RX_DROP     0xa0
#define CSR_MAC_CSR_CMD 0xa4
#define CSR_MAC_CSR_DATA 0xa8
#define CSR_AFC_CFG     0xac
#define CSR_E2P_CMD     0xb0
#define CSR_E2P_DATA    0xb4

/* IRQ_CFG */
#define IRQ_INT         0x00001000
#define IRQ_EN          0x00000100
#define IRQ_POL         0x00000010
#define IRQ_TYPE        0x00000001

/* INT_STS/INT_EN */
#define SW_INT          0x80000000
#define TXSTOP_INT      0x02000000
#define RXSTOP_INT      0x01000000
#define RXDFH_INT       0x00800000
#define TX_IOC_INT      0x00200000
#define RXD_INT         0x00100000
#define GPT_INT         0x00080000
#define PHY_INT         0x00040000
#define PME_INT         0x00020000
#define TXSO_INT        0x00010000
#define RWT_INT         0x00008000
#define RXE_INT         0x00004000
#define TXE_INT         0x00002000
#define TDFU_INT        0x00000800
#define TDFO_INT        0x00000400
#define TDFA_INT        0x00000200
#define TSFF_INT        0x00000100
#define TSFL_INT        0x00000080
#define RXDF_INT        0x00000040
#define RDFL_INT        0x00000020
#define RSFF_INT        0x00000010
#define RSFL_INT        0x00000008
#define GPIO2_INT       0x00000004
#define GPIO1_INT       0x00000002
#define GPIO0_INT       0x00000001
#define RESERVED_INT    0x7c001000

#define MAC_CR          1
#define MAC_ADDRH       2
#define MAC_ADDRL       3
#define MAC_HASHH       4
#define MAC_HASHL       5
#define MAC_MII_ACC     6
#define MAC_MII_DATA    7
#define MAC_FLOW        8
#define MAC_VLAN1       9 /* TODO */
#define MAC_VLAN2       10 /* TODO */
#define MAC_WUFF        11 /* TODO */
#define MAC_WUCSR       12 /* TODO */

#define MAC_CR_RXALL    0x80000000
#define MAC_CR_RCVOWN   0x00800000
#define MAC_CR_LOOPBK   0x00200000
#define MAC_CR_FDPX     0x00100000
#define MAC_CR_MCPAS    0x00080000
#define MAC_CR_PRMS     0x00040000
#define MAC_CR_INVFILT  0x00020000
#define MAC_CR_PASSBAD  0x00010000
#define MAC_CR_HO       0x00008000
#define MAC_CR_HPFILT   0x00002000
#define MAC_CR_LCOLL    0x00001000
#define MAC_CR_BCAST    0x00000800
#define MAC_CR_DISRTY   0x00000400
#define MAC_CR_PADSTR   0x00000100
#define MAC_CR_BOLMT    0x000000c0
#define MAC_CR_DFCHK    0x00000020
#define MAC_CR_TXEN     0x00000008
#define MAC_CR_RXEN     0x00000004
#define MAC_CR_RESERVED 0x7f404213

#define PHY_INT_ENERGYON            0x80
#define PHY_INT_AUTONEG_COMPLETE    0x40
#define PHY_INT_FAULT               0x20
#define PHY_INT_DOWN                0x10
#define PHY_INT_AUTONEG_LP          0x08
#define PHY_INT_PARFAULT            0x04
#define PHY_INT_AUTONEG_PAGE        0x02

#define GPT_TIMER_EN    0x20000000

enum tx_state {
    TX_IDLE,
    TX_B,
    TX_DATA
};

typedef struct {
    enum tx_state state;
    uint32_t cmd_a;
    uint32_t cmd_b;
    int buffer_size;
    int offset;
    int pad;
    int fifo_used;
    int len;
    uint8_t data[2048];
} LAN9118Packet;

typedef struct {
    SysBusDevice busdev;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    int mmio_index;
    ptimer_state *timer;

    uint32_t irq_cfg;
    uint32_t int_sts;
    uint32_t int_en;
    uint32_t fifo_int;
    uint32_t rx_cfg;
    uint32_t tx_cfg;
    uint32_t hw_cfg;
    uint32_t pmt_ctrl;
    uint32_t gpio_cfg;
    uint32_t gpt_cfg;
    uint32_t word_swap;
    uint32_t free_timer_start;
    uint32_t mac_cmd;
    uint32_t mac_data;
    uint32_t afc_cfg;
    uint32_t e2p_cmd;
    uint32_t e2p_data;

    uint32_t mac_cr;
    uint32_t mac_hashh;
    uint32_t mac_hashl;
    uint32_t mac_mii_acc;
    uint32_t mac_mii_data;
    uint32_t mac_flow;

    uint32_t phy_status;
    uint32_t phy_control;
    uint32_t phy_advertise;
    uint32_t phy_int;
    uint32_t phy_int_mask;

    int eeprom_writable;
    uint8_t eeprom[128];

    int tx_fifo_size;
    LAN9118Packet *txp;
    LAN9118Packet tx_packet;

    int tx_status_fifo_used;
    int tx_status_fifo_head;
    uint32_t tx_status_fifo[512];

    int rx_status_fifo_size;
    int rx_status_fifo_used;
    int rx_status_fifo_head;
    uint32_t rx_status_fifo[896];
    int rx_fifo_size;
    int rx_fifo_used;
    int rx_fifo_head;
    uint32_t rx_fifo[3360];
    int rx_packet_size_head;
    int rx_packet_size_tail;
    int rx_packet_size[1024];

    int rxp_offset;
    int rxp_size;
    int rxp_pad;
} lan9118_state;

static void lan9118_update(lan9118_state *s)
{
    int level;

    /* TODO: Implement FIFO level IRQs.  */
    level = (s->int_sts & s->int_en) != 0;
    if (level) {
        s->irq_cfg |= IRQ_INT;
    } else {
        s->irq_cfg &= ~IRQ_INT;
    }
    if ((s->irq_cfg & IRQ_EN) == 0) {
        level = 0;
    }
    if ((s->irq_cfg & (IRQ_TYPE | IRQ_POL)) != (IRQ_TYPE | IRQ_POL)) {
        /* Interrupt is active low unless we're configured as
         * active-high polarity, push-pull type.
         */
        level = !level;
    }
    qemu_set_irq(s->irq, level);
}

static void lan9118_mac_changed(lan9118_state *s)
{
    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);
}

static void lan9118_reload_eeprom(lan9118_state *s)
{
    int i;
    if (s->eeprom[0] != 0xa5) {
        s->e2p_cmd &= ~0x10;
        DPRINTF("MACADDR load failed\n");
        return;
    }
    for (i = 0; i < 6; i++) {
        s->conf.macaddr.a[i] = s->eeprom[i + 1];
    }
    s->e2p_cmd |= 0x10;
    DPRINTF("MACADDR loaded from eeprom\n");
    lan9118_mac_changed(s);
}

static void phy_update_irq(lan9118_state *s)
{
    if (s->phy_int & s->phy_int_mask) {
        s->int_sts |= PHY_INT;
    } else {
        s->int_sts &= ~PHY_INT;
    }
    lan9118_update(s);
}

static void phy_update_link(lan9118_state *s)
{
    /* Autonegotiation status mirrors link status.  */
    if (s->nic->nc.link_down) {
        s->phy_status &= ~0x0024;
        s->phy_int |= PHY_INT_DOWN;
    } else {
        s->phy_status |= 0x0024;
        s->phy_int |= PHY_INT_ENERGYON;
        s->phy_int |= PHY_INT_AUTONEG_COMPLETE;
    }
    phy_update_irq(s);
}

static void lan9118_set_link(VLANClientState *nc)
{
    phy_update_link(DO_UPCAST(NICState, nc, nc)->opaque);
}

static void phy_reset(lan9118_state *s)
{
    s->phy_status = 0x7809;
    s->phy_control = 0x3000;
    s->phy_advertise = 0x01e1;
    s->phy_int_mask = 0;
    s->phy_int = 0;
    phy_update_link(s);
}

static void lan9118_reset(DeviceState *d)
{
    lan9118_state *s = FROM_SYSBUS(lan9118_state, sysbus_from_qdev(d));
    s->irq_cfg &= (IRQ_TYPE | IRQ_POL);
    s->int_sts = 0;
    s->int_en = 0;
    s->fifo_int = 0x48000000;
    s->rx_cfg = 0;
    s->tx_cfg = 0;
    s->hw_cfg = 0x00050000;
    s->pmt_ctrl &= 0x45;
    s->gpio_cfg = 0;
    s->txp->fifo_used = 0;
    s->txp->state = TX_IDLE;
    s->txp->cmd_a = 0xffffffffu;
    s->txp->cmd_b = 0xffffffffu;
    s->txp->len = 0;
    s->txp->fifo_used = 0;
    s->tx_fifo_size = 4608;
    s->tx_status_fifo_used = 0;
    s->rx_status_fifo_size = 704;
    s->rx_fifo_size = 2640;
    s->rx_fifo_used = 0;
    s->rx_status_fifo_size = 176;
    s->rx_status_fifo_used = 0;
    s->rxp_offset = 0;
    s->rxp_size = 0;
    s->rxp_pad = 0;
    s->rx_packet_size_tail = s->rx_packet_size_head;
    s->rx_packet_size[s->rx_packet_size_head] = 0;
    s->mac_cmd = 0;
    s->mac_data = 0;
    s->afc_cfg = 0;
    s->e2p_cmd = 0;
    s->e2p_data = 0;
    s->free_timer_start = qemu_get_clock_ns(vm_clock) / 40;

    ptimer_stop(s->timer);
    ptimer_set_count(s->timer, 0xffff);
    s->gpt_cfg = 0xffff;

    s->mac_cr = MAC_CR_PRMS;
    s->mac_hashh = 0;
    s->mac_hashl = 0;
    s->mac_mii_acc = 0;
    s->mac_mii_data = 0;
    s->mac_flow = 0;

    phy_reset(s);

    s->eeprom_writable = 0;
    lan9118_reload_eeprom(s);
}

static int lan9118_can_receive(VLANClientState *nc)
{
    return 1;
}

static void rx_fifo_push(lan9118_state *s, uint32_t val)
{
    int fifo_pos;
    fifo_pos = s->rx_fifo_head + s->rx_fifo_used;
    if (fifo_pos >= s->rx_fifo_size)
      fifo_pos -= s->rx_fifo_size;
    s->rx_fifo[fifo_pos] = val;
    s->rx_fifo_used++;
}

/* Return nonzero if the packet is accepted by the filter.  */
static int lan9118_filter(lan9118_state *s, const uint8_t *addr)
{
    int multicast;
    uint32_t hash;

    if (s->mac_cr & MAC_CR_PRMS) {
        return 1;
    }
    if (addr[0] == 0xff && addr[1] == 0xff && addr[2] == 0xff &&
        addr[3] == 0xff && addr[4] == 0xff && addr[5] == 0xff) {
        return (s->mac_cr & MAC_CR_BCAST) == 0;
    }

    multicast = addr[0] & 1;
    if (multicast &&s->mac_cr & MAC_CR_MCPAS) {
        return 1;
    }
    if (multicast ? (s->mac_cr & MAC_CR_HPFILT) == 0
                  : (s->mac_cr & MAC_CR_HO) == 0) {
        /* Exact matching.  */
        hash = memcmp(addr, s->conf.macaddr.a, 6);
        if (s->mac_cr & MAC_CR_INVFILT) {
            return hash != 0;
        } else {
            return hash == 0;
        }
    } else {
        /* Hash matching  */
        hash = (crc32(~0, addr, 6) >> 26);
        if (hash & 0x20) {
            return (s->mac_hashh >> (hash & 0x1f)) & 1;
        } else {
            return (s->mac_hashl >> (hash & 0x1f)) & 1;
        }
    }
}

static ssize_t lan9118_receive(VLANClientState *nc, const uint8_t *buf,
                               size_t size)
{
    lan9118_state *s = DO_UPCAST(NICState, nc, nc)->opaque;
    int fifo_len;
    int offset;
    int src_pos;
    int n;
    int filter;
    uint32_t val;
    uint32_t crc;
    uint32_t status;

    if ((s->mac_cr & MAC_CR_RXEN) == 0) {
        return -1;
    }

    if (size >= 2048 || size < 14) {
        return -1;
    }

    /* TODO: Implement FIFO overflow notification.  */
    if (s->rx_status_fifo_used == s->rx_status_fifo_size) {
        return -1;
    }

    filter = lan9118_filter(s, buf);
    if (!filter && (s->mac_cr & MAC_CR_RXALL) == 0) {
        return size;
    }

    offset = (s->rx_cfg >> 8) & 0x1f;
    n = offset & 3;
    fifo_len = (size + n + 3) >> 2;
    /* Add a word for the CRC.  */
    fifo_len++;
    if (s->rx_fifo_size - s->rx_fifo_used < fifo_len) {
        return -1;
    }

    DPRINTF("Got packet len:%d fifo:%d filter:%s\n",
            (int)size, fifo_len, filter ? "pass" : "fail");
    val = 0;
    crc = bswap32(crc32(~0, buf, size));
    for (src_pos = 0; src_pos < size; src_pos++) {
        val = (val >> 8) | ((uint32_t)buf[src_pos] << 24);
        n++;
        if (n == 4) {
            n = 0;
            rx_fifo_push(s, val);
            val = 0;
        }
    }
    if (n) {
        val >>= ((4 - n) * 8);
        val |= crc << (n * 8);
        rx_fifo_push(s, val);
        val = crc >> ((4 - n) * 8);
        rx_fifo_push(s, val);
    } else {
        rx_fifo_push(s, crc);
    }
    n = s->rx_status_fifo_head + s->rx_status_fifo_used;
    if (n >= s->rx_status_fifo_size) {
        n -= s->rx_status_fifo_size;
    }
    s->rx_packet_size[s->rx_packet_size_tail] = fifo_len;
    s->rx_packet_size_tail = (s->rx_packet_size_tail + 1023) & 1023;
    s->rx_status_fifo_used++;

    status = (size + 4) << 16;
    if (buf[0] == 0xff && buf[1] == 0xff && buf[2] == 0xff &&
        buf[3] == 0xff && buf[4] == 0xff && buf[5] == 0xff) {
        status |= 0x00002000;
    } else if (buf[0] & 1) {
        status |= 0x00000400;
    }
    if (!filter) {
        status |= 0x40000000;
    }
    s->rx_status_fifo[n] = status;

    if (s->rx_status_fifo_used > (s->fifo_int & 0xff)) {
        s->int_sts |= RSFL_INT;
    }
    lan9118_update(s);

    return size;
}

static uint32_t rx_fifo_pop(lan9118_state *s)
{
    int n;
    uint32_t val;

    if (s->rxp_size == 0 && s->rxp_pad == 0) {
        s->rxp_size = s->rx_packet_size[s->rx_packet_size_head];
        s->rx_packet_size[s->rx_packet_size_head] = 0;
        if (s->rxp_size != 0) {
            s->rx_packet_size_head = (s->rx_packet_size_head + 1023) & 1023;
            s->rxp_offset = (s->rx_cfg >> 10) & 7;
            n = s->rxp_offset + s->rxp_size;
            switch (s->rx_cfg >> 30) {
            case 1:
                n = (-n) & 3;
                break;
            case 2:
                n = (-n) & 7;
                break;
            default:
                n = 0;
                break;
            }
            s->rxp_pad = n;
            DPRINTF("Pop packet size:%d offset:%d pad: %d\n",
                    s->rxp_size, s->rxp_offset, s->rxp_pad);
        }
    }
    if (s->rxp_offset > 0) {
        s->rxp_offset--;
        val = 0;
    } else if (s->rxp_size > 0) {
        s->rxp_size--;
        val = s->rx_fifo[s->rx_fifo_head++];
        if (s->rx_fifo_head >= s->rx_fifo_size) {
            s->rx_fifo_head -= s->rx_fifo_size;
        }
        s->rx_fifo_used--;
    } else if (s->rxp_pad > 0) {
        s->rxp_pad--;
        val =  0;
    } else {
        DPRINTF("RX underflow\n");
        s->int_sts |= RXE_INT;
        val =  0;
    }
    lan9118_update(s);
    return val;
}

static void do_tx_packet(lan9118_state *s)
{
    int n;
    uint32_t status;

    /* FIXME: Honor TX disable, and allow queueing of packets.  */
    if (s->phy_control & 0x4000)  {
        /* This assumes the receive routine doesn't touch the VLANClient.  */
        lan9118_receive(&s->nic->nc, s->txp->data, s->txp->len);
    } else {
        qemu_send_packet(&s->nic->nc, s->txp->data, s->txp->len);
    }
    s->txp->fifo_used = 0;

    if (s->tx_status_fifo_used == 512) {
        /* Status FIFO full */
        return;
    }
    /* Add entry to status FIFO.  */
    status = s->txp->cmd_b & 0xffff0000u;
    DPRINTF("Sent packet tag:%04x len %d\n", status >> 16, s->txp->len);
    n = (s->tx_status_fifo_head + s->tx_status_fifo_used) & 511;
    s->tx_status_fifo[n] = status;
    s->tx_status_fifo_used++;
    if (s->tx_status_fifo_used == 512) {
        s->int_sts |= TSFF_INT;
        /* TODO: Stop transmission.  */
    }
}

static uint32_t rx_status_fifo_pop(lan9118_state *s)
{
    uint32_t val;

    val = s->rx_status_fifo[s->rx_status_fifo_head];
    if (s->rx_status_fifo_used != 0) {
        s->rx_status_fifo_used--;
        s->rx_status_fifo_head++;
        if (s->rx_status_fifo_head >= s->rx_status_fifo_size) {
            s->rx_status_fifo_head -= s->rx_status_fifo_size;
        }
        /* ??? What value should be returned when the FIFO is empty?  */
        DPRINTF("RX status pop 0x%08x\n", val);
    }
    return val;
}

static uint32_t tx_status_fifo_pop(lan9118_state *s)
{
    uint32_t val;

    val = s->tx_status_fifo[s->tx_status_fifo_head];
    if (s->tx_status_fifo_used != 0) {
        s->tx_status_fifo_used--;
        s->tx_status_fifo_head = (s->tx_status_fifo_head + 1) & 511;
        /* ??? What value should be returned when the FIFO is empty?  */
    }
    return val;
}

static void tx_fifo_push(lan9118_state *s, uint32_t val)
{
    int n;

    if (s->txp->fifo_used == s->tx_fifo_size) {
        s->int_sts |= TDFO_INT;
        return;
    }
    switch (s->txp->state) {
    case TX_IDLE:
        s->txp->cmd_a = val & 0x831f37ff;
        s->txp->fifo_used++;
        s->txp->state = TX_B;
        break;
    case TX_B:
        if (s->txp->cmd_a & 0x2000) {
            /* First segment */
            s->txp->cmd_b = val;
            s->txp->fifo_used++;
            s->txp->buffer_size = s->txp->cmd_a & 0x7ff;
            s->txp->offset = (s->txp->cmd_a >> 16) & 0x1f;
            /* End alignment does not include command words.  */
            n = (s->txp->buffer_size + s->txp->offset + 3) >> 2;
            switch ((n >> 24) & 3) {
            case 1:
                n = (-n) & 3;
                break;
            case 2:
                n = (-n) & 7;
                break;
            default:
                n = 0;
            }
            s->txp->pad = n;
            s->txp->len = 0;
        }
        DPRINTF("Block len:%d offset:%d pad:%d cmd %08x\n",
                s->txp->buffer_size, s->txp->offset, s->txp->pad,
                s->txp->cmd_a);
        s->txp->state = TX_DATA;
        break;
    case TX_DATA:
        if (s->txp->offset >= 4) {
            s->txp->offset -= 4;
            break;
        }
        if (s->txp->buffer_size <= 0 && s->txp->pad != 0) {
            s->txp->pad--;
        } else {
            n = 4;
            while (s->txp->offset) {
                val >>= 8;
                n--;
                s->txp->offset--;
            }
            /* Documentation is somewhat unclear on the ordering of bytes
               in FIFO words.  Empirical results show it to be little-endian.
               */
            /* TODO: FIFO overflow checking.  */
            while (n--) {
                s->txp->data[s->txp->len] = val & 0xff;
                s->txp->len++;
                val >>= 8;
                s->txp->buffer_size--;
            }
            s->txp->fifo_used++;
        }
        if (s->txp->buffer_size <= 0 && s->txp->pad == 0) {
            if (s->txp->cmd_a & 0x1000) {
                do_tx_packet(s);
            }
            if (s->txp->cmd_a & 0x80000000) {
                s->int_sts |= TX_IOC_INT;
            }
            s->txp->state = TX_IDLE;
        }
        break;
    }
}

static uint32_t do_phy_read(lan9118_state *s, int reg)
{
    uint32_t val;

    switch (reg) {
    case 0: /* Basic Control */
        return s->phy_control;
    case 1: /* Basic Status */
        return s->phy_status;
    case 2: /* ID1 */
        return 0x0007;
    case 3: /* ID2 */
        return 0xc0d1;
    case 4: /* Auto-neg advertisment */
        return s->phy_advertise;
    case 5: /* Auto-neg Link Partner Ability */
        return 0x0f71;
    case 6: /* Auto-neg Expansion */
        return 1;
        /* TODO 17, 18, 27, 29, 30, 31 */
    case 29: /* Interrupt source.  */
        val = s->phy_int;
        s->phy_int = 0;
        phy_update_irq(s);
        return val;
    case 30: /* Interrupt mask */
        return s->phy_int_mask;
    default:
        BADF("PHY read reg %d\n", reg);
        return 0;
    }
}

static void do_phy_write(lan9118_state *s, int reg, uint32_t val)
{
    switch (reg) {
    case 0: /* Basic Control */
        if (val & 0x8000) {
            phy_reset(s);
            break;
        }
        s->phy_control = val & 0x7980;
        /* Complete autonegotiation immediately.  */
        if (val & 0x1000) {
            s->phy_status |= 0x0020;
        }
        break;
    case 4: /* Auto-neg advertisment */
        s->phy_advertise = (val & 0x2d7f) | 0x80;
        break;
        /* TODO 17, 18, 27, 31 */
    case 30: /* Interrupt mask */
        s->phy_int_mask = val & 0xff;
        phy_update_irq(s);
        break;
    default:
        BADF("PHY write reg %d = 0x%04x\n", reg, val);
    }
}

static void do_mac_write(lan9118_state *s, int reg, uint32_t val)
{
    switch (reg) {
    case MAC_CR:
        if ((s->mac_cr & MAC_CR_RXEN) != 0 && (val & MAC_CR_RXEN) == 0) {
            s->int_sts |= RXSTOP_INT;
        }
        s->mac_cr = val & ~MAC_CR_RESERVED;
        DPRINTF("MAC_CR: %08x\n", val);
        break;
    case MAC_ADDRH:
        s->conf.macaddr.a[4] = val & 0xff;
        s->conf.macaddr.a[5] = (val >> 8) & 0xff;
        lan9118_mac_changed(s);
        break;
    case MAC_ADDRL:
        s->conf.macaddr.a[0] = val & 0xff;
        s->conf.macaddr.a[1] = (val >> 8) & 0xff;
        s->conf.macaddr.a[2] = (val >> 16) & 0xff;
        s->conf.macaddr.a[3] = (val >> 24) & 0xff;
        lan9118_mac_changed(s);
        break;
    case MAC_HASHH:
        s->mac_hashh = val;
        break;
    case MAC_HASHL:
        s->mac_hashl = val;
        break;
    case MAC_MII_ACC:
        s->mac_mii_acc = val & 0xffc2;
        if (val & 2) {
            DPRINTF("PHY write %d = 0x%04x\n",
                    (val >> 6) & 0x1f, s->mac_mii_data);
            do_phy_write(s, (val >> 6) & 0x1f, s->mac_mii_data);
        } else {
            s->mac_mii_data = do_phy_read(s, (val >> 6) & 0x1f);
            DPRINTF("PHY read %d = 0x%04x\n",
                    (val >> 6) & 0x1f, s->mac_mii_data);
        }
        break;
    case MAC_MII_DATA:
        s->mac_mii_data = val & 0xffff;
        break;
    case MAC_FLOW:
        s->mac_flow = val & 0xffff0000;
        break;
    case MAC_VLAN1:
        /* Writing to this register changes a condition for
         * FrameTooLong bit in rx_status.  Since we do not set
         * FrameTooLong anyway, just ignore write to this.
         */
        break;
    default:
        hw_error("lan9118: Unimplemented MAC register write: %d = 0x%x\n",
                 s->mac_cmd & 0xf, val);
    }
}

static uint32_t do_mac_read(lan9118_state *s, int reg)
{
    switch (reg) {
    case MAC_CR:
        return s->mac_cr;
    case MAC_ADDRH:
        return s->conf.macaddr.a[4] | (s->conf.macaddr.a[5] << 8);
    case MAC_ADDRL:
        return s->conf.macaddr.a[0] | (s->conf.macaddr.a[1] << 8)
               | (s->conf.macaddr.a[2] << 16) | (s->conf.macaddr.a[3] << 24);
    case MAC_HASHH:
        return s->mac_hashh;
        break;
    case MAC_HASHL:
        return s->mac_hashl;
        break;
    case MAC_MII_ACC:
        return s->mac_mii_acc;
    case MAC_MII_DATA:
        return s->mac_mii_data;
    case MAC_FLOW:
        return s->mac_flow;
    default:
        hw_error("lan9118: Unimplemented MAC register read: %d\n",
                 s->mac_cmd & 0xf);
    }
}

static void lan9118_eeprom_cmd(lan9118_state *s, int cmd, int addr)
{
    s->e2p_cmd = (s->e2p_cmd & 0x10) | (cmd << 28) | addr;
    switch (cmd) {
    case 0:
        s->e2p_data = s->eeprom[addr];
        DPRINTF("EEPROM Read %d = 0x%02x\n", addr, s->e2p_data);
        break;
    case 1:
        s->eeprom_writable = 0;
        DPRINTF("EEPROM Write Disable\n");
        break;
    case 2: /* EWEN */
        s->eeprom_writable = 1;
        DPRINTF("EEPROM Write Enable\n");
        break;
    case 3: /* WRITE */
        if (s->eeprom_writable) {
            s->eeprom[addr] &= s->e2p_data;
            DPRINTF("EEPROM Write %d = 0x%02x\n", addr, s->e2p_data);
        } else {
            DPRINTF("EEPROM Write %d (ignored)\n", addr);
        }
        break;
    case 4: /* WRAL */
        if (s->eeprom_writable) {
            for (addr = 0; addr < 128; addr++) {
                s->eeprom[addr] &= s->e2p_data;
            }
            DPRINTF("EEPROM Write All 0x%02x\n", s->e2p_data);
        } else {
            DPRINTF("EEPROM Write All (ignored)\n");
        }
    case 5: /* ERASE */
        if (s->eeprom_writable) {
            s->eeprom[addr] = 0xff;
            DPRINTF("EEPROM Erase %d\n", addr);
        } else {
            DPRINTF("EEPROM Erase %d (ignored)\n", addr);
        }
        break;
    case 6: /* ERAL */
        if (s->eeprom_writable) {
            memset(s->eeprom, 0xff, 128);
            DPRINTF("EEPROM Erase All\n");
        } else {
            DPRINTF("EEPROM Erase All (ignored)\n");
        }
        break;
    case 7: /* RELOAD */
        lan9118_reload_eeprom(s);
        break;
    }
}

static void lan9118_tick(void *opaque)
{
    lan9118_state *s = (lan9118_state *)opaque;
    if (s->int_en & GPT_INT) {
        s->int_sts |= GPT_INT;
    }
    lan9118_update(s);
}

static void lan9118_writel(void *opaque, target_phys_addr_t offset,
                           uint32_t val)
{
    lan9118_state *s = (lan9118_state *)opaque;
    offset &= 0xff;
    
    //DPRINTF("Write reg 0x%02x = 0x%08x\n", (int)offset, val);
    if (offset >= 0x20 && offset < 0x40) {
        /* TX FIFO */
        tx_fifo_push(s, val);
        return;
    }
    switch (offset) {
    case CSR_IRQ_CFG:
        /* TODO: Implement interrupt deassertion intervals.  */
        val &= (IRQ_EN | IRQ_POL | IRQ_TYPE);
        s->irq_cfg = (s->irq_cfg & IRQ_INT) | val;
        break;
    case CSR_INT_STS:
        s->int_sts &= ~val;
        break;
    case CSR_INT_EN:
        s->int_en = val & ~RESERVED_INT;
        s->int_sts |= val & SW_INT;
        break;
    case CSR_FIFO_INT:
        DPRINTF("FIFO INT levels %08x\n", val);
        s->fifo_int = val;
        break;
    case CSR_RX_CFG:
        if (val & 0x8000) {
            /* RX_DUMP */
            s->rx_fifo_used = 0;
            s->rx_status_fifo_used = 0;
            s->rx_packet_size_tail = s->rx_packet_size_head;
            s->rx_packet_size[s->rx_packet_size_head] = 0;
        }
        s->rx_cfg = val & 0xcfff1ff0;
        break;
    case CSR_TX_CFG:
        if (val & 0x8000) {
            s->tx_status_fifo_used = 0;
        }
        if (val & 0x4000) {
            s->txp->state = TX_IDLE;
            s->txp->fifo_used = 0;
            s->txp->cmd_a = 0xffffffff;
        }
        s->tx_cfg = val & 6;
        break;
    case CSR_HW_CFG:
        if (val & 1) {
            /* SRST */
            lan9118_reset(&s->busdev.qdev);
        } else {
            s->hw_cfg = val & 0x003f300;
        }
        break;
    case CSR_RX_DP_CTRL:
        if (val & 0x80000000) {
            /* Skip forward to next packet.  */
            s->rxp_pad = 0;
            s->rxp_offset = 0;
            if (s->rxp_size == 0) {
                /* Pop a word to start the next packet.  */
                rx_fifo_pop(s);
                s->rxp_pad = 0;
                s->rxp_offset = 0;
            }
            s->rx_fifo_head += s->rxp_size;
            if (s->rx_fifo_head >= s->rx_fifo_size) {
                s->rx_fifo_head -= s->rx_fifo_size;
            }
        }
        break;
    case CSR_PMT_CTRL:
        if (val & 0x400) {
            phy_reset(s);
        }
        s->pmt_ctrl &= ~0x34e;
        s->pmt_ctrl |= (val & 0x34e);
        break;
    case CSR_GPIO_CFG:
        /* Probably just enabling LEDs.  */
        s->gpio_cfg = val & 0x7777071f;
        break;
    case CSR_GPT_CFG:
        if ((s->gpt_cfg ^ val) & GPT_TIMER_EN) {
            if (val & GPT_TIMER_EN) {
                ptimer_set_count(s->timer, val & 0xffff);
                ptimer_run(s->timer, 0);
            } else {
                ptimer_stop(s->timer);
                ptimer_set_count(s->timer, 0xffff);
            }
        }
        s->gpt_cfg = val & (GPT_TIMER_EN | 0xffff);
        break;
    case CSR_WORD_SWAP:
        /* Ignored because we're in 32-bit mode.  */
        s->word_swap = val;
        break;
    case CSR_MAC_CSR_CMD:
        s->mac_cmd = val & 0x4000000f;
        if (val & 0x80000000) {
            if (val & 0x40000000) {
                s->mac_data = do_mac_read(s, val & 0xf);
                DPRINTF("MAC read %d = 0x%08x\n", val & 0xf, s->mac_data);
            } else {
                DPRINTF("MAC write %d = 0x%08x\n", val & 0xf, s->mac_data);
                do_mac_write(s, val & 0xf, s->mac_data);
            }
        }
        break;
    case CSR_MAC_CSR_DATA:
        s->mac_data = val;
        break;
    case CSR_AFC_CFG:
        s->afc_cfg = val & 0x00ffffff;
        break;
    case CSR_E2P_CMD:
        lan9118_eeprom_cmd(s, (val >> 28) & 7, val & 0x7f);
        break;
    case CSR_E2P_DATA:
        s->e2p_data = val & 0xff;
        break;

    default:
        hw_error("lan9118_write: Bad reg 0x%x = %x\n", (int)offset, val);
        break;
    }
    lan9118_update(s);
}

static uint32_t lan9118_readl(void *opaque, target_phys_addr_t offset)
{
    lan9118_state *s = (lan9118_state *)opaque;

    //DPRINTF("Read reg 0x%02x\n", (int)offset);
    if (offset < 0x20) {
        /* RX FIFO */
        return rx_fifo_pop(s);
    }
    switch (offset) {
    case 0x40:
        return rx_status_fifo_pop(s);
    case 0x44:
        return s->rx_status_fifo[s->tx_status_fifo_head];
    case 0x48:
        return tx_status_fifo_pop(s);
    case 0x4c:
        return s->tx_status_fifo[s->tx_status_fifo_head];
    case CSR_ID_REV:
        return 0x01180001;
    case CSR_IRQ_CFG:
        return s->irq_cfg;
    case CSR_INT_STS:
        return s->int_sts;
    case CSR_INT_EN:
        return s->int_en;
    case CSR_BYTE_TEST:
        return 0x87654321;
    case CSR_FIFO_INT:
        return s->fifo_int;
    case CSR_RX_CFG:
        return s->rx_cfg;
    case CSR_TX_CFG:
        return s->tx_cfg;
    case CSR_HW_CFG:
        return s->hw_cfg | 0x4;
    case CSR_RX_DP_CTRL:
        return 0;
    case CSR_RX_FIFO_INF:
        return (s->rx_status_fifo_used << 16) | (s->rx_fifo_used << 2);
    case CSR_TX_FIFO_INF:
        return (s->tx_status_fifo_used << 16)
               | (s->tx_fifo_size - s->txp->fifo_used);
    case CSR_PMT_CTRL:
        return s->pmt_ctrl;
    case CSR_GPIO_CFG:
        return s->gpio_cfg;
    case CSR_GPT_CFG:
        return s->gpt_cfg;
    case CSR_GPT_CNT:
        return ptimer_get_count(s->timer);
    case CSR_WORD_SWAP:
        return s->word_swap;
    case CSR_FREE_RUN:
        return (qemu_get_clock_ns(vm_clock) / 40) - s->free_timer_start;
    case CSR_RX_DROP:
        /* TODO: Implement dropped frames counter.  */
        return 0;
    case CSR_MAC_CSR_CMD:
        return s->mac_cmd;
    case CSR_MAC_CSR_DATA:
        return s->mac_data;
    case CSR_AFC_CFG:
        return s->afc_cfg;
    case CSR_E2P_CMD:
        return s->e2p_cmd;
    case CSR_E2P_DATA:
        return s->e2p_data;
    }
    hw_error("lan9118_read: Bad reg 0x%x\n", (int)offset);
    return 0;
}

static CPUReadMemoryFunc * const lan9118_readfn[] = {
    lan9118_readl,
    lan9118_readl,
    lan9118_readl
};

static CPUWriteMemoryFunc * const lan9118_writefn[] = {
    lan9118_writel,
    lan9118_writel,
    lan9118_writel
};

static void lan9118_cleanup(VLANClientState *nc)
{
    lan9118_state *s = DO_UPCAST(NICState, nc, nc)->opaque;

    s->nic = NULL;
}

static NetClientInfo net_lan9118_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = lan9118_can_receive,
    .receive = lan9118_receive,
    .cleanup = lan9118_cleanup,
    .link_status_changed = lan9118_set_link,
};

static int lan9118_init1(SysBusDevice *dev)
{
    lan9118_state *s = FROM_SYSBUS(lan9118_state, dev);
    QEMUBH *bh;
    int i;

    s->mmio_index = cpu_register_io_memory(lan9118_readfn,
                                           lan9118_writefn, s,
                                           DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x100, s->mmio_index);
    sysbus_init_irq(dev, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_lan9118_info, &s->conf,
                          dev->qdev.info->name, dev->qdev.id, s);
    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);
    s->eeprom[0] = 0xa5;
    for (i = 0; i < 6; i++) {
        s->eeprom[i + 1] = s->conf.macaddr.a[i];
    }
    s->pmt_ctrl = 1;
    s->txp = &s->tx_packet;

    bh = qemu_bh_new(lan9118_tick, s);
    s->timer = ptimer_init(bh);
    ptimer_set_freq(s->timer, 10000);
    ptimer_set_limit(s->timer, 0xffff, 1);

    /* ??? Save/restore.  */
    return 0;
}

static SysBusDeviceInfo lan9118_info = {
    .init = lan9118_init1,
    .qdev.name  = "lan9118",
    .qdev.size  = sizeof(lan9118_state),
    .qdev.reset = lan9118_reset,
    .qdev.props = (Property[]) {
        DEFINE_NIC_PROPERTIES(lan9118_state, conf),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void lan9118_register_devices(void)
{
    sysbus_register_withprop(&lan9118_info);
}

/* Legacy helper function.  Should go away when machine config files are
   implemented.  */
void lan9118_init(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    qemu_check_nic_model(nd, "lan9118");
    dev = qdev_create(NULL, "lan9118");
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

device_init(lan9118_register_devices)
