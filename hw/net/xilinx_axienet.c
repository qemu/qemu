/*
 * QEMU model of Xilinx AXI-Ethernet.
 *
 * Copyright (c) 2011 Edgar E. Iglesias.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw/sysbus.h"
#include "qemu/log.h"
#include "net/net.h"
#include "net/checksum.h"
#include "qapi/qmp/qerror.h"

#include "hw/stream.h"

#define DPHY(x)

#define TYPE_XILINX_AXI_ENET "xlnx.axi-ethernet"
#define TYPE_XILINX_AXI_ENET_DATA_STREAM "xilinx-axienet-data-stream"
#define TYPE_XILINX_AXI_ENET_CONTROL_STREAM "xilinx-axienet-control-stream"

#define XILINX_AXI_ENET(obj) \
     OBJECT_CHECK(XilinxAXIEnet, (obj), TYPE_XILINX_AXI_ENET)

#define XILINX_AXI_ENET_DATA_STREAM(obj) \
     OBJECT_CHECK(XilinxAXIEnetStreamSlave, (obj),\
     TYPE_XILINX_AXI_ENET_DATA_STREAM)

#define XILINX_AXI_ENET_CONTROL_STREAM(obj) \
     OBJECT_CHECK(XilinxAXIEnetStreamSlave, (obj),\
     TYPE_XILINX_AXI_ENET_CONTROL_STREAM)

/* Advertisement control register. */
#define ADVERTISE_10HALF        0x0020  /* Try for 10mbps half-duplex  */
#define ADVERTISE_10FULL        0x0040  /* Try for 10mbps full-duplex  */
#define ADVERTISE_100HALF       0x0080  /* Try for 100mbps half-duplex */
#define ADVERTISE_100FULL       0x0100  /* Try for 100mbps full-duplex */

#define CONTROL_PAYLOAD_WORDS 5
#define CONTROL_PAYLOAD_SIZE (CONTROL_PAYLOAD_WORDS * (sizeof(uint32_t)))

struct PHY {
    uint32_t regs[32];

    int link;

    unsigned int (*read)(struct PHY *phy, unsigned int req);
    void (*write)(struct PHY *phy, unsigned int req,
                  unsigned int data);
};

static unsigned int tdk_read(struct PHY *phy, unsigned int req)
{
    int regnum;
    unsigned r = 0;

    regnum = req & 0x1f;

    switch (regnum) {
        case 1:
            if (!phy->link) {
                break;
            }
            /* MR1.  */
            /* Speeds and modes.  */
            r |= (1 << 13) | (1 << 14);
            r |= (1 << 11) | (1 << 12);
            r |= (1 << 5); /* Autoneg complete.  */
            r |= (1 << 3); /* Autoneg able.  */
            r |= (1 << 2); /* link.  */
            r |= (1 << 1); /* link.  */
            break;
        case 5:
            /* Link partner ability.
               We are kind; always agree with whatever best mode
               the guest advertises.  */
            r = 1 << 14; /* Success.  */
            /* Copy advertised modes.  */
            r |= phy->regs[4] & (15 << 5);
            /* Autoneg support.  */
            r |= 1;
            break;
        case 17:
            /* Marvell PHY on many xilinx boards.  */
            r = 0x8000; /* 1000Mb  */
            break;
        case 18:
            {
                /* Diagnostics reg.  */
                int duplex = 0;
                int speed_100 = 0;

                if (!phy->link) {
                    break;
                }

                /* Are we advertising 100 half or 100 duplex ? */
                speed_100 = !!(phy->regs[4] & ADVERTISE_100HALF);
                speed_100 |= !!(phy->regs[4] & ADVERTISE_100FULL);

                /* Are we advertising 10 duplex or 100 duplex ? */
                duplex = !!(phy->regs[4] & ADVERTISE_100FULL);
                duplex |= !!(phy->regs[4] & ADVERTISE_10FULL);
                r = (speed_100 << 10) | (duplex << 11);
            }
            break;

        default:
            r = phy->regs[regnum];
            break;
    }
    DPHY(qemu_log("\n%s %x = reg[%d]\n", __func__, r, regnum));
    return r;
}

static void
tdk_write(struct PHY *phy, unsigned int req, unsigned int data)
{
    int regnum;

    regnum = req & 0x1f;
    DPHY(qemu_log("%s reg[%d] = %x\n", __func__, regnum, data));
    switch (regnum) {
        default:
            phy->regs[regnum] = data;
            break;
    }

    /* Unconditionally clear regs[BMCR][BMCR_RESET] */
    phy->regs[0] &= ~0x8000;
}

static void
tdk_init(struct PHY *phy)
{
    phy->regs[0] = 0x3100;
    /* PHY Id.  */
    phy->regs[2] = 0x0300;
    phy->regs[3] = 0xe400;
    /* Autonegotiation advertisement reg.  */
    phy->regs[4] = 0x01E1;
    phy->link = 1;

    phy->read = tdk_read;
    phy->write = tdk_write;
}

struct MDIOBus {
    /* bus.  */
    int mdc;
    int mdio;

    /* decoder.  */
    enum {
        PREAMBLE,
        SOF,
        OPC,
        ADDR,
        REQ,
        TURNAROUND,
        DATA
    } state;
    unsigned int drive;

    unsigned int cnt;
    unsigned int addr;
    unsigned int opc;
    unsigned int req;
    unsigned int data;

    struct PHY *devs[32];
};

static void
mdio_attach(struct MDIOBus *bus, struct PHY *phy, unsigned int addr)
{
    bus->devs[addr & 0x1f] = phy;
}

#ifdef USE_THIS_DEAD_CODE
static void
mdio_detach(struct MDIOBus *bus, struct PHY *phy, unsigned int addr)
{
    bus->devs[addr & 0x1f] = NULL;
}
#endif

static uint16_t mdio_read_req(struct MDIOBus *bus, unsigned int addr,
                  unsigned int reg)
{
    struct PHY *phy;
    uint16_t data;

    phy = bus->devs[addr];
    if (phy && phy->read) {
        data = phy->read(phy, reg);
    } else {
        data = 0xffff;
    }
    DPHY(qemu_log("%s addr=%d reg=%d data=%x\n", __func__, addr, reg, data));
    return data;
}

static void mdio_write_req(struct MDIOBus *bus, unsigned int addr,
               unsigned int reg, uint16_t data)
{
    struct PHY *phy;

    DPHY(qemu_log("%s addr=%d reg=%d data=%x\n", __func__, addr, reg, data));
    phy = bus->devs[addr];
    if (phy && phy->write) {
        phy->write(phy, reg, data);
    }
}

#define DENET(x)

#define R_RAF      (0x000 / 4)
enum {
    RAF_MCAST_REJ = (1 << 1),
    RAF_BCAST_REJ = (1 << 2),
    RAF_EMCF_EN = (1 << 12),
    RAF_NEWFUNC_EN = (1 << 11)
};

#define R_IS       (0x00C / 4)
enum {
    IS_HARD_ACCESS_COMPLETE = 1,
    IS_AUTONEG = (1 << 1),
    IS_RX_COMPLETE = (1 << 2),
    IS_RX_REJECT = (1 << 3),
    IS_TX_COMPLETE = (1 << 5),
    IS_RX_DCM_LOCK = (1 << 6),
    IS_MGM_RDY = (1 << 7),
    IS_PHY_RST_DONE = (1 << 8),
};

#define R_IP       (0x010 / 4)
#define R_IE       (0x014 / 4)
#define R_UAWL     (0x020 / 4)
#define R_UAWU     (0x024 / 4)
#define R_PPST     (0x030 / 4)
enum {
    PPST_LINKSTATUS = (1 << 0),
    PPST_PHY_LINKSTATUS = (1 << 7),
};

#define R_STATS_RX_BYTESL (0x200 / 4)
#define R_STATS_RX_BYTESH (0x204 / 4)
#define R_STATS_TX_BYTESL (0x208 / 4)
#define R_STATS_TX_BYTESH (0x20C / 4)
#define R_STATS_RXL       (0x290 / 4)
#define R_STATS_RXH       (0x294 / 4)
#define R_STATS_RX_BCASTL (0x2a0 / 4)
#define R_STATS_RX_BCASTH (0x2a4 / 4)
#define R_STATS_RX_MCASTL (0x2a8 / 4)
#define R_STATS_RX_MCASTH (0x2ac / 4)

#define R_RCW0     (0x400 / 4)
#define R_RCW1     (0x404 / 4)
enum {
    RCW1_VLAN = (1 << 27),
    RCW1_RX   = (1 << 28),
    RCW1_FCS  = (1 << 29),
    RCW1_JUM  = (1 << 30),
    RCW1_RST  = (1 << 31),
};

#define R_TC       (0x408 / 4)
enum {
    TC_VLAN = (1 << 27),
    TC_TX   = (1 << 28),
    TC_FCS  = (1 << 29),
    TC_JUM  = (1 << 30),
    TC_RST  = (1 << 31),
};

#define R_EMMC     (0x410 / 4)
enum {
    EMMC_LINKSPEED_10MB = (0 << 30),
    EMMC_LINKSPEED_100MB = (1 << 30),
    EMMC_LINKSPEED_1000MB = (2 << 30),
};

#define R_PHYC     (0x414 / 4)

#define R_MC       (0x500 / 4)
#define MC_EN      (1 << 6)

#define R_MCR      (0x504 / 4)
#define R_MWD      (0x508 / 4)
#define R_MRD      (0x50c / 4)
#define R_MIS      (0x600 / 4)
#define R_MIP      (0x620 / 4)
#define R_MIE      (0x640 / 4)
#define R_MIC      (0x640 / 4)

#define R_UAW0     (0x700 / 4)
#define R_UAW1     (0x704 / 4)
#define R_FMI      (0x708 / 4)
#define R_AF0      (0x710 / 4)
#define R_AF1      (0x714 / 4)
#define R_MAX      (0x34 / 4)

/* Indirect registers.  */
struct TEMAC  {
    struct MDIOBus mdio_bus;
    struct PHY phy;

    void *parent;
};

typedef struct XilinxAXIEnetStreamSlave XilinxAXIEnetStreamSlave;
typedef struct XilinxAXIEnet XilinxAXIEnet;

struct XilinxAXIEnetStreamSlave {
    Object parent;

    struct XilinxAXIEnet *enet;
} ;

struct XilinxAXIEnet {
    SysBusDevice busdev;
    MemoryRegion iomem;
    qemu_irq irq;
    StreamSlave *tx_data_dev;
    StreamSlave *tx_control_dev;
    XilinxAXIEnetStreamSlave rx_data_dev;
    XilinxAXIEnetStreamSlave rx_control_dev;
    NICState *nic;
    NICConf conf;


    uint32_t c_rxmem;
    uint32_t c_txmem;
    uint32_t c_phyaddr;

    struct TEMAC TEMAC;

    /* MII regs.  */
    union {
        uint32_t regs[4];
        struct {
            uint32_t mc;
            uint32_t mcr;
            uint32_t mwd;
            uint32_t mrd;
        };
    } mii;

    struct {
        uint64_t rx_bytes;
        uint64_t tx_bytes;

        uint64_t rx;
        uint64_t rx_bcast;
        uint64_t rx_mcast;
    } stats;

    /* Receive configuration words.  */
    uint32_t rcw[2];
    /* Transmit config.  */
    uint32_t tc;
    uint32_t emmc;
    uint32_t phyc;

    /* Unicast Address Word.  */
    uint32_t uaw[2];
    /* Unicast address filter used with extended mcast.  */
    uint32_t ext_uaw[2];
    uint32_t fmi;

    uint32_t regs[R_MAX];

    /* Multicast filter addrs.  */
    uint32_t maddr[4][2];
    /* 32K x 1 lookup filter.  */
    uint32_t ext_mtable[1024];

    uint32_t hdr[CONTROL_PAYLOAD_WORDS];

    uint8_t *rxmem;
    uint32_t rxsize;
    uint32_t rxpos;

    uint8_t rxapp[CONTROL_PAYLOAD_SIZE];
    uint32_t rxappsize;
};

static void axienet_rx_reset(XilinxAXIEnet *s)
{
    s->rcw[1] = RCW1_JUM | RCW1_FCS | RCW1_RX | RCW1_VLAN;
}

static void axienet_tx_reset(XilinxAXIEnet *s)
{
    s->tc = TC_JUM | TC_TX | TC_VLAN;
}

static inline int axienet_rx_resetting(XilinxAXIEnet *s)
{
    return s->rcw[1] & RCW1_RST;
}

static inline int axienet_rx_enabled(XilinxAXIEnet *s)
{
    return s->rcw[1] & RCW1_RX;
}

static inline int axienet_extmcf_enabled(XilinxAXIEnet *s)
{
    return !!(s->regs[R_RAF] & RAF_EMCF_EN);
}

static inline int axienet_newfunc_enabled(XilinxAXIEnet *s)
{
    return !!(s->regs[R_RAF] & RAF_NEWFUNC_EN);
}

static void xilinx_axienet_reset(DeviceState *d)
{
    XilinxAXIEnet *s = XILINX_AXI_ENET(d);

    axienet_rx_reset(s);
    axienet_tx_reset(s);

    s->regs[R_PPST] = PPST_LINKSTATUS | PPST_PHY_LINKSTATUS;
    s->regs[R_IS] = IS_AUTONEG | IS_RX_DCM_LOCK | IS_MGM_RDY | IS_PHY_RST_DONE;

    s->emmc = EMMC_LINKSPEED_100MB;
}

static void enet_update_irq(XilinxAXIEnet *s)
{
    s->regs[R_IP] = s->regs[R_IS] & s->regs[R_IE];
    qemu_set_irq(s->irq, !!s->regs[R_IP]);
}

static uint64_t enet_read(void *opaque, hwaddr addr, unsigned size)
{
    XilinxAXIEnet *s = opaque;
    uint32_t r = 0;
    addr >>= 2;

    switch (addr) {
        case R_RCW0:
        case R_RCW1:
            r = s->rcw[addr & 1];
            break;

        case R_TC:
            r = s->tc;
            break;

        case R_EMMC:
            r = s->emmc;
            break;

        case R_PHYC:
            r = s->phyc;
            break;

        case R_MCR:
            r = s->mii.regs[addr & 3] | (1 << 7); /* Always ready.  */
            break;

        case R_STATS_RX_BYTESL:
        case R_STATS_RX_BYTESH:
            r = s->stats.rx_bytes >> (32 * (addr & 1));
            break;

        case R_STATS_TX_BYTESL:
        case R_STATS_TX_BYTESH:
            r = s->stats.tx_bytes >> (32 * (addr & 1));
            break;

        case R_STATS_RXL:
        case R_STATS_RXH:
            r = s->stats.rx >> (32 * (addr & 1));
            break;
        case R_STATS_RX_BCASTL:
        case R_STATS_RX_BCASTH:
            r = s->stats.rx_bcast >> (32 * (addr & 1));
            break;
        case R_STATS_RX_MCASTL:
        case R_STATS_RX_MCASTH:
            r = s->stats.rx_mcast >> (32 * (addr & 1));
            break;

        case R_MC:
        case R_MWD:
        case R_MRD:
            r = s->mii.regs[addr & 3];
            break;

        case R_UAW0:
        case R_UAW1:
            r = s->uaw[addr & 1];
            break;

        case R_UAWU:
        case R_UAWL:
            r = s->ext_uaw[addr & 1];
            break;

        case R_FMI:
            r = s->fmi;
            break;

        case R_AF0:
        case R_AF1:
            r = s->maddr[s->fmi & 3][addr & 1];
            break;

        case 0x8000 ... 0x83ff:
            r = s->ext_mtable[addr - 0x8000];
            break;

        default:
            if (addr < ARRAY_SIZE(s->regs)) {
                r = s->regs[addr];
            }
            DENET(qemu_log("%s addr=" TARGET_FMT_plx " v=%x\n",
                            __func__, addr * 4, r));
            break;
    }
    return r;
}

static void enet_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned size)
{
    XilinxAXIEnet *s = opaque;
    struct TEMAC *t = &s->TEMAC;

    addr >>= 2;
    switch (addr) {
        case R_RCW0:
        case R_RCW1:
            s->rcw[addr & 1] = value;
            if ((addr & 1) && value & RCW1_RST) {
                axienet_rx_reset(s);
            } else {
                qemu_flush_queued_packets(qemu_get_queue(s->nic));
            }
            break;

        case R_TC:
            s->tc = value;
            if (value & TC_RST) {
                axienet_tx_reset(s);
            }
            break;

        case R_EMMC:
            s->emmc = value;
            break;

        case R_PHYC:
            s->phyc = value;
            break;

        case R_MC:
             value &= ((1 << 7) - 1);

             /* Enable the MII.  */
             if (value & MC_EN) {
                 unsigned int miiclkdiv = value & ((1 << 6) - 1);
                 if (!miiclkdiv) {
                     qemu_log("AXIENET: MDIO enabled but MDIOCLK is zero!\n");
                 }
             }
             s->mii.mc = value;
             break;

        case R_MCR: {
             unsigned int phyaddr = (value >> 24) & 0x1f;
             unsigned int regaddr = (value >> 16) & 0x1f;
             unsigned int op = (value >> 14) & 3;
             unsigned int initiate = (value >> 11) & 1;

             if (initiate) {
                 if (op == 1) {
                     mdio_write_req(&t->mdio_bus, phyaddr, regaddr, s->mii.mwd);
                 } else if (op == 2) {
                     s->mii.mrd = mdio_read_req(&t->mdio_bus, phyaddr, regaddr);
                 } else {
                     qemu_log("AXIENET: invalid MDIOBus OP=%d\n", op);
                 }
             }
             s->mii.mcr = value;
             break;
        }

        case R_MWD:
        case R_MRD:
             s->mii.regs[addr & 3] = value;
             break;


        case R_UAW0:
        case R_UAW1:
            s->uaw[addr & 1] = value;
            break;

        case R_UAWL:
        case R_UAWU:
            s->ext_uaw[addr & 1] = value;
            break;

        case R_FMI:
            s->fmi = value;
            break;

        case R_AF0:
        case R_AF1:
            s->maddr[s->fmi & 3][addr & 1] = value;
            break;

        case R_IS:
            s->regs[addr] &= ~value;
            break;

        case 0x8000 ... 0x83ff:
            s->ext_mtable[addr - 0x8000] = value;
            break;

        default:
            DENET(qemu_log("%s addr=" TARGET_FMT_plx " v=%x\n",
                           __func__, addr * 4, (unsigned)value));
            if (addr < ARRAY_SIZE(s->regs)) {
                s->regs[addr] = value;
            }
            break;
    }
    enet_update_irq(s);
}

static const MemoryRegionOps enet_ops = {
    .read = enet_read,
    .write = enet_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int eth_can_rx(NetClientState *nc)
{
    XilinxAXIEnet *s = qemu_get_nic_opaque(nc);

    /* RX enabled?  */
    return !s->rxsize && !axienet_rx_resetting(s) && axienet_rx_enabled(s);
}

static int enet_match_addr(const uint8_t *buf, uint32_t f0, uint32_t f1)
{
    int match = 1;

    if (memcmp(buf, &f0, 4)) {
        match = 0;
    }

    if (buf[4] != (f1 & 0xff) || buf[5] != ((f1 >> 8) & 0xff)) {
        match = 0;
    }

    return match;
}

static void axienet_eth_rx_notify(void *opaque)
{
    XilinxAXIEnet *s = XILINX_AXI_ENET(opaque);

    while (s->rxappsize && stream_can_push(s->tx_control_dev,
                                           axienet_eth_rx_notify, s)) {
        size_t ret = stream_push(s->tx_control_dev,
                                 (void *)s->rxapp + CONTROL_PAYLOAD_SIZE
                                 - s->rxappsize, s->rxappsize);
        s->rxappsize -= ret;
    }

    while (s->rxsize && stream_can_push(s->tx_data_dev,
                                        axienet_eth_rx_notify, s)) {
        size_t ret = stream_push(s->tx_data_dev, (void *)s->rxmem + s->rxpos,
                                 s->rxsize);
        s->rxsize -= ret;
        s->rxpos += ret;
        if (!s->rxsize) {
            s->regs[R_IS] |= IS_RX_COMPLETE;
        }
    }
    enet_update_irq(s);
}

static ssize_t eth_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    XilinxAXIEnet *s = qemu_get_nic_opaque(nc);
    static const unsigned char sa_bcast[6] = {0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff};
    static const unsigned char sa_ipmcast[3] = {0x01, 0x00, 0x52};
    uint32_t app[CONTROL_PAYLOAD_WORDS] = {0};
    int promisc = s->fmi & (1 << 31);
    int unicast, broadcast, multicast, ip_multicast = 0;
    uint32_t csum32;
    uint16_t csum16;
    int i;

    DENET(qemu_log("%s: %zd bytes\n", __func__, size));

    unicast = ~buf[0] & 0x1;
    broadcast = memcmp(buf, sa_bcast, 6) == 0;
    multicast = !unicast && !broadcast;
    if (multicast && (memcmp(sa_ipmcast, buf, sizeof sa_ipmcast) == 0)) {
        ip_multicast = 1;
    }

    /* Jumbo or vlan sizes ?  */
    if (!(s->rcw[1] & RCW1_JUM)) {
        if (size > 1518 && size <= 1522 && !(s->rcw[1] & RCW1_VLAN)) {
            return size;
        }
    }

    /* Basic Address filters.  If you want to use the extended filters
       you'll generally have to place the ethernet mac into promiscuous mode
       to avoid the basic filtering from dropping most frames.  */
    if (!promisc) {
        if (unicast) {
            if (!enet_match_addr(buf, s->uaw[0], s->uaw[1])) {
                return size;
            }
        } else {
            if (broadcast) {
                /* Broadcast.  */
                if (s->regs[R_RAF] & RAF_BCAST_REJ) {
                    return size;
                }
            } else {
                int drop = 1;

                /* Multicast.  */
                if (s->regs[R_RAF] & RAF_MCAST_REJ) {
                    return size;
                }

                for (i = 0; i < 4; i++) {
                    if (enet_match_addr(buf, s->maddr[i][0], s->maddr[i][1])) {
                        drop = 0;
                        break;
                    }
                }

                if (drop) {
                    return size;
                }
            }
        }
    }

    /* Extended mcast filtering enabled?  */
    if (axienet_newfunc_enabled(s) && axienet_extmcf_enabled(s)) {
        if (unicast) {
            if (!enet_match_addr(buf, s->ext_uaw[0], s->ext_uaw[1])) {
                return size;
            }
        } else {
            if (broadcast) {
                /* Broadcast. ???  */
                if (s->regs[R_RAF] & RAF_BCAST_REJ) {
                    return size;
                }
            } else {
                int idx, bit;

                /* Multicast.  */
                if (!memcmp(buf, sa_ipmcast, 3)) {
                    return size;
                }

                idx  = (buf[4] & 0x7f) << 8;
                idx |= buf[5];

                bit = 1 << (idx & 0x1f);
                idx >>= 5;

                if (!(s->ext_mtable[idx] & bit)) {
                    return size;
                }
            }
        }
    }

    if (size < 12) {
        s->regs[R_IS] |= IS_RX_REJECT;
        enet_update_irq(s);
        return -1;
    }

    if (size > (s->c_rxmem - 4)) {
        size = s->c_rxmem - 4;
    }

    memcpy(s->rxmem, buf, size);
    memset(s->rxmem + size, 0, 4); /* Clear the FCS.  */

    if (s->rcw[1] & RCW1_FCS) {
        size += 4; /* fcs is inband.  */
    }

    app[0] = 5 << 28;
    csum32 = net_checksum_add(size - 14, (uint8_t *)s->rxmem + 14);
    /* Fold it once.  */
    csum32 = (csum32 & 0xffff) + (csum32 >> 16);
    /* And twice to get rid of possible carries.  */
    csum16 = (csum32 & 0xffff) + (csum32 >> 16);
    app[3] = csum16;
    app[4] = size & 0xffff;

    s->stats.rx_bytes += size;
    s->stats.rx++;
    if (multicast) {
        s->stats.rx_mcast++;
        app[2] |= 1 | (ip_multicast << 1);
    } else if (broadcast) {
        s->stats.rx_bcast++;
        app[2] |= 1 << 3;
    }

    /* Good frame.  */
    app[2] |= 1 << 6;

    s->rxsize = size;
    s->rxpos = 0;
    for (i = 0; i < ARRAY_SIZE(app); ++i) {
        app[i] = cpu_to_le32(app[i]);
    }
    s->rxappsize = CONTROL_PAYLOAD_SIZE;
    memcpy(s->rxapp, app, s->rxappsize);
    axienet_eth_rx_notify(s);

    enet_update_irq(s);
    return size;
}

static void eth_cleanup(NetClientState *nc)
{
    /* FIXME.  */
    XilinxAXIEnet *s = qemu_get_nic_opaque(nc);
    g_free(s->rxmem);
    g_free(s);
}

static size_t
xilinx_axienet_control_stream_push(StreamSlave *obj, uint8_t *buf, size_t len)
{
    int i;
    XilinxAXIEnetStreamSlave *cs = XILINX_AXI_ENET_CONTROL_STREAM(obj);
    XilinxAXIEnet *s = cs->enet;

    if (len != CONTROL_PAYLOAD_SIZE) {
        hw_error("AXI Enet requires %d byte control stream payload\n",
                 (int)CONTROL_PAYLOAD_SIZE);
    }

    memcpy(s->hdr, buf, len);

    for (i = 0; i < ARRAY_SIZE(s->hdr); ++i) {
        s->hdr[i] = le32_to_cpu(s->hdr[i]);
    }
    return len;
}

static size_t
xilinx_axienet_data_stream_push(StreamSlave *obj, uint8_t *buf, size_t size)
{
    XilinxAXIEnetStreamSlave *ds = XILINX_AXI_ENET_DATA_STREAM(obj);
    XilinxAXIEnet *s = ds->enet;

    /* TX enable ?  */
    if (!(s->tc & TC_TX)) {
        return size;
    }

    /* Jumbo or vlan sizes ?  */
    if (!(s->tc & TC_JUM)) {
        if (size > 1518 && size <= 1522 && !(s->tc & TC_VLAN)) {
            return size;
        }
    }

    if (s->hdr[0] & 1) {
        unsigned int start_off = s->hdr[1] >> 16;
        unsigned int write_off = s->hdr[1] & 0xffff;
        uint32_t tmp_csum;
        uint16_t csum;

        tmp_csum = net_checksum_add(size - start_off,
                                    (uint8_t *)buf + start_off);
        /* Accumulate the seed.  */
        tmp_csum += s->hdr[2] & 0xffff;

        /* Fold the 32bit partial checksum.  */
        csum = net_checksum_finish(tmp_csum);

        /* Writeback.  */
        buf[write_off] = csum >> 8;
        buf[write_off + 1] = csum & 0xff;
    }

    qemu_send_packet(qemu_get_queue(s->nic), buf, size);

    s->stats.tx_bytes += size;
    s->regs[R_IS] |= IS_TX_COMPLETE;
    enet_update_irq(s);

    return size;
}

static NetClientInfo net_xilinx_enet_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = eth_can_rx,
    .receive = eth_rx,
    .cleanup = eth_cleanup,
};

static void xilinx_enet_realize(DeviceState *dev, Error **errp)
{
    XilinxAXIEnet *s = XILINX_AXI_ENET(dev);
    XilinxAXIEnetStreamSlave *ds = XILINX_AXI_ENET_DATA_STREAM(&s->rx_data_dev);
    XilinxAXIEnetStreamSlave *cs = XILINX_AXI_ENET_CONTROL_STREAM(
                                                            &s->rx_control_dev);
    Error *local_err = NULL;

    object_property_add_link(OBJECT(ds), "enet", "xlnx.axi-ethernet",
                             (Object **) &ds->enet,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &local_err);
    object_property_add_link(OBJECT(cs), "enet", "xlnx.axi-ethernet",
                             (Object **) &cs->enet,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &local_err);
    if (local_err) {
        goto xilinx_enet_realize_fail;
    }
    object_property_set_link(OBJECT(ds), OBJECT(s), "enet", &local_err);
    object_property_set_link(OBJECT(cs), OBJECT(s), "enet", &local_err);
    if (local_err) {
        goto xilinx_enet_realize_fail;
    }

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_xilinx_enet_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    tdk_init(&s->TEMAC.phy);
    mdio_attach(&s->TEMAC.mdio_bus, &s->TEMAC.phy, s->c_phyaddr);

    s->TEMAC.parent = s;

    s->rxmem = g_malloc(s->c_rxmem);
    return;

xilinx_enet_realize_fail:
    if (!*errp) {
        *errp = local_err;
    }
}

static void xilinx_enet_init(Object *obj)
{
    XilinxAXIEnet *s = XILINX_AXI_ENET(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    object_property_add_link(obj, "axistream-connected", TYPE_STREAM_SLAVE,
                             (Object **) &s->tx_data_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
    object_property_add_link(obj, "axistream-control-connected",
                             TYPE_STREAM_SLAVE,
                             (Object **) &s->tx_control_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);

    object_initialize(&s->rx_data_dev, sizeof(s->rx_data_dev),
                      TYPE_XILINX_AXI_ENET_DATA_STREAM);
    object_initialize(&s->rx_control_dev, sizeof(s->rx_control_dev),
                      TYPE_XILINX_AXI_ENET_CONTROL_STREAM);
    object_property_add_child(OBJECT(s), "axistream-connected-target",
                              (Object *)&s->rx_data_dev, &error_abort);
    object_property_add_child(OBJECT(s), "axistream-control-connected-target",
                              (Object *)&s->rx_control_dev, &error_abort);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &enet_ops, s, "enet", 0x40000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property xilinx_enet_properties[] = {
    DEFINE_PROP_UINT32("phyaddr", XilinxAXIEnet, c_phyaddr, 7),
    DEFINE_PROP_UINT32("rxmem", XilinxAXIEnet, c_rxmem, 0x1000),
    DEFINE_PROP_UINT32("txmem", XilinxAXIEnet, c_txmem, 0x1000),
    DEFINE_NIC_PROPERTIES(XilinxAXIEnet, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_enet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xilinx_enet_realize;
    dc->props = xilinx_enet_properties;
    dc->reset = xilinx_axienet_reset;
}

static void xilinx_enet_stream_class_init(ObjectClass *klass, void *data)
{
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    ssc->push = data;
}

static const TypeInfo xilinx_enet_info = {
    .name          = TYPE_XILINX_AXI_ENET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAXIEnet),
    .class_init    = xilinx_enet_class_init,
    .instance_init = xilinx_enet_init,
};

static const TypeInfo xilinx_enet_data_stream_info = {
    .name          = TYPE_XILINX_AXI_ENET_DATA_STREAM,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(struct XilinxAXIEnetStreamSlave),
    .class_init    = xilinx_enet_stream_class_init,
    .class_data    = xilinx_axienet_data_stream_push,
    .interfaces = (InterfaceInfo[]) {
            { TYPE_STREAM_SLAVE },
            { }
    }
};

static const TypeInfo xilinx_enet_control_stream_info = {
    .name          = TYPE_XILINX_AXI_ENET_CONTROL_STREAM,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(struct XilinxAXIEnetStreamSlave),
    .class_init    = xilinx_enet_stream_class_init,
    .class_data    = xilinx_axienet_control_stream_push,
    .interfaces = (InterfaceInfo[]) {
            { TYPE_STREAM_SLAVE },
            { }
    }
};

static void xilinx_enet_register_types(void)
{
    type_register_static(&xilinx_enet_info);
    type_register_static(&xilinx_enet_data_stream_info);
    type_register_static(&xilinx_enet_control_stream_info);
}

type_init(xilinx_enet_register_types)
