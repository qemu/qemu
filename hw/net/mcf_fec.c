/*
 * ColdFire Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "net/net.h"
#include "qemu/module.h"
#include "hw/m68k/mcf.h"
#include "hw/m68k/mcf_fec.h"
#include "hw/net/mii.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
/* For crc32 */
#include <zlib.h>

//#define DEBUG_FEC 1

#ifdef DEBUG_FEC
#define DPRINTF(fmt, ...) \
do { printf("mcf_fec: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#define FEC_MAX_DESC 1024
#define FEC_MAX_FRAME_SIZE 2032
#define FEC_MIB_SIZE 64

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq[FEC_NUM_IRQ];
    NICState *nic;
    NICConf conf;
    uint32_t irq_state;
    uint32_t eir;
    uint32_t eimr;
    int rx_enabled;
    uint32_t rx_descriptor;
    uint32_t tx_descriptor;
    uint32_t ecr;
    uint32_t mmfr;
    uint32_t mscr;
    uint32_t rcr;
    uint32_t tcr;
    uint32_t tfwr;
    uint32_t rfsr;
    uint32_t erdsr;
    uint32_t etdsr;
    uint32_t emrbr;
    uint32_t mib[FEC_MIB_SIZE];
} mcf_fec_state;

#define FEC_INT_HB   0x80000000
#define FEC_INT_BABR 0x40000000
#define FEC_INT_BABT 0x20000000
#define FEC_INT_GRA  0x10000000
#define FEC_INT_TXF  0x08000000
#define FEC_INT_TXB  0x04000000
#define FEC_INT_RXF  0x02000000
#define FEC_INT_RXB  0x01000000
#define FEC_INT_MII  0x00800000
#define FEC_INT_EB   0x00400000
#define FEC_INT_LC   0x00200000
#define FEC_INT_RL   0x00100000
#define FEC_INT_UN   0x00080000

#define FEC_EN      2
#define FEC_RESET   1

/* Map interrupt flags onto IRQ lines.  */
static const uint32_t mcf_fec_irq_map[FEC_NUM_IRQ] = {
    FEC_INT_TXF,
    FEC_INT_TXB,
    FEC_INT_UN,
    FEC_INT_RL,
    FEC_INT_RXF,
    FEC_INT_RXB,
    FEC_INT_MII,
    FEC_INT_LC,
    FEC_INT_HB,
    FEC_INT_GRA,
    FEC_INT_EB,
    FEC_INT_BABT,
    FEC_INT_BABR
};

/* Buffer Descriptor.  */
typedef struct {
    uint16_t flags;
    uint16_t length;
    uint32_t data;
} mcf_fec_bd;

#define FEC_BD_R    0x8000
#define FEC_BD_E    0x8000
#define FEC_BD_O1   0x4000
#define FEC_BD_W    0x2000
#define FEC_BD_O2   0x1000
#define FEC_BD_L    0x0800
#define FEC_BD_TC   0x0400
#define FEC_BD_ABC  0x0200
#define FEC_BD_M    0x0100
#define FEC_BD_BC   0x0080
#define FEC_BD_MC   0x0040
#define FEC_BD_LG   0x0020
#define FEC_BD_NO   0x0010
#define FEC_BD_CR   0x0004
#define FEC_BD_OV   0x0002
#define FEC_BD_TR   0x0001

#define MIB_RMON_T_DROP         0
#define MIB_RMON_T_PACKETS      1
#define MIB_RMON_T_BC_PKT       2
#define MIB_RMON_T_MC_PKT       3
#define MIB_RMON_T_CRC_ALIGN    4
#define MIB_RMON_T_UNDERSIZE    5
#define MIB_RMON_T_OVERSIZE     6
#define MIB_RMON_T_FRAG         7
#define MIB_RMON_T_JAB          8
#define MIB_RMON_T_COL          9
#define MIB_RMON_T_P64          10
#define MIB_RMON_T_P65TO127     11
#define MIB_RMON_T_P128TO255    12
#define MIB_RMON_T_P256TO511    13
#define MIB_RMON_T_P512TO1023   14
#define MIB_RMON_T_P1024TO2047  15
#define MIB_RMON_T_P_GTE2048    16
#define MIB_RMON_T_OCTETS       17
#define MIB_IEEE_T_DROP         18
#define MIB_IEEE_T_FRAME_OK     19
#define MIB_IEEE_T_1COL         20
#define MIB_IEEE_T_MCOL         21
#define MIB_IEEE_T_DEF          22
#define MIB_IEEE_T_LCOL         23
#define MIB_IEEE_T_EXCOL        24
#define MIB_IEEE_T_MACERR       25
#define MIB_IEEE_T_CSERR        26
#define MIB_IEEE_T_SQE          27
#define MIB_IEEE_T_FDXFC        28
#define MIB_IEEE_T_OCTETS_OK    29

#define MIB_RMON_R_DROP         32
#define MIB_RMON_R_PACKETS      33
#define MIB_RMON_R_BC_PKT       34
#define MIB_RMON_R_MC_PKT       35
#define MIB_RMON_R_CRC_ALIGN    36
#define MIB_RMON_R_UNDERSIZE    37
#define MIB_RMON_R_OVERSIZE     38
#define MIB_RMON_R_FRAG         39
#define MIB_RMON_R_JAB          40
#define MIB_RMON_R_RESVD_0      41
#define MIB_RMON_R_P64          42
#define MIB_RMON_R_P65TO127     43
#define MIB_RMON_R_P128TO255    44
#define MIB_RMON_R_P256TO511    45
#define MIB_RMON_R_P512TO1023   46
#define MIB_RMON_R_P1024TO2047  47
#define MIB_RMON_R_P_GTE2048    48
#define MIB_RMON_R_OCTETS       49
#define MIB_IEEE_R_DROP         50
#define MIB_IEEE_R_FRAME_OK     51
#define MIB_IEEE_R_CRC          52
#define MIB_IEEE_R_ALIGN        53
#define MIB_IEEE_R_MACERR       54
#define MIB_IEEE_R_FDXFC        55
#define MIB_IEEE_R_OCTETS_OK    56

static void mcf_fec_read_bd(mcf_fec_bd *bd, uint32_t addr)
{
    cpu_physical_memory_read(addr, bd, sizeof(*bd));
    be16_to_cpus(&bd->flags);
    be16_to_cpus(&bd->length);
    be32_to_cpus(&bd->data);
}

static void mcf_fec_write_bd(mcf_fec_bd *bd, uint32_t addr)
{
    mcf_fec_bd tmp;
    tmp.flags = cpu_to_be16(bd->flags);
    tmp.length = cpu_to_be16(bd->length);
    tmp.data = cpu_to_be32(bd->data);
    cpu_physical_memory_write(addr, &tmp, sizeof(tmp));
}

static void mcf_fec_update(mcf_fec_state *s)
{
    uint32_t active;
    uint32_t changed;
    uint32_t mask;
    int i;

    active = s->eir & s->eimr;
    changed = active ^s->irq_state;
    for (i = 0; i < FEC_NUM_IRQ; i++) {
        mask = mcf_fec_irq_map[i];
        if (changed & mask) {
            DPRINTF("IRQ %d = %d\n", i, (active & mask) != 0);
            qemu_set_irq(s->irq[i], (active & mask) != 0);
        }
    }
    s->irq_state = active;
}

static void mcf_fec_tx_stats(mcf_fec_state *s, int size)
{
    s->mib[MIB_RMON_T_PACKETS]++;
    s->mib[MIB_RMON_T_OCTETS] += size;
    if (size < 64) {
        s->mib[MIB_RMON_T_FRAG]++;
    } else if (size == 64) {
        s->mib[MIB_RMON_T_P64]++;
    } else if (size < 128) {
        s->mib[MIB_RMON_T_P65TO127]++;
    } else if (size < 256) {
        s->mib[MIB_RMON_T_P128TO255]++;
    } else if (size < 512) {
        s->mib[MIB_RMON_T_P256TO511]++;
    } else if (size < 1024) {
        s->mib[MIB_RMON_T_P512TO1023]++;
    } else if (size < 2048) {
        s->mib[MIB_RMON_T_P1024TO2047]++;
    } else {
        s->mib[MIB_RMON_T_P_GTE2048]++;
    }
    s->mib[MIB_IEEE_T_FRAME_OK]++;
    s->mib[MIB_IEEE_T_OCTETS_OK] += size;
}

static void mcf_fec_do_tx(mcf_fec_state *s)
{
    uint32_t addr;
    mcf_fec_bd bd;
    int frame_size;
    int len, descnt = 0;
    uint8_t frame[FEC_MAX_FRAME_SIZE];
    uint8_t *ptr;

    DPRINTF("do_tx\n");
    ptr = frame;
    frame_size = 0;
    addr = s->tx_descriptor;
    while (descnt++ < FEC_MAX_DESC) {
        mcf_fec_read_bd(&bd, addr);
        DPRINTF("tx_bd %x flags %04x len %d data %08x\n",
                addr, bd.flags, bd.length, bd.data);
        if ((bd.flags & FEC_BD_R) == 0) {
            /* Run out of descriptors to transmit.  */
            break;
        }
        len = bd.length;
        if (frame_size + len > FEC_MAX_FRAME_SIZE) {
            len = FEC_MAX_FRAME_SIZE - frame_size;
            s->eir |= FEC_INT_BABT;
        }
        cpu_physical_memory_read(bd.data, ptr, len);
        ptr += len;
        frame_size += len;
        if (bd.flags & FEC_BD_L) {
            /* Last buffer in frame.  */
            DPRINTF("Sending packet\n");
            qemu_send_packet(qemu_get_queue(s->nic), frame, frame_size);
            mcf_fec_tx_stats(s, frame_size);
            ptr = frame;
            frame_size = 0;
            s->eir |= FEC_INT_TXF;
        }
        s->eir |= FEC_INT_TXB;
        bd.flags &= ~FEC_BD_R;
        /* Write back the modified descriptor.  */
        mcf_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & FEC_BD_W) != 0) {
            addr = s->etdsr;
        } else {
            addr += 8;
        }
    }
    s->tx_descriptor = addr;
}

static void mcf_fec_enable_rx(mcf_fec_state *s)
{
    NetClientState *nc = qemu_get_queue(s->nic);
    mcf_fec_bd bd;

    mcf_fec_read_bd(&bd, s->rx_descriptor);
    s->rx_enabled = ((bd.flags & FEC_BD_E) != 0);
    if (s->rx_enabled) {
        qemu_flush_queued_packets(nc);
    }
}

static void mcf_fec_reset(DeviceState *dev)
{
    mcf_fec_state *s = MCF_FEC_NET(dev);

    s->eir = 0;
    s->eimr = 0;
    s->rx_enabled = 0;
    s->ecr = 0;
    s->mscr = 0;
    s->rcr = 0x05ee0001;
    s->tcr = 0;
    s->tfwr = 0;
    s->rfsr = 0x500;
}

#define MMFR_WRITE_OP	(1 << 28)
#define MMFR_READ_OP	(2 << 28)
#define MMFR_PHYADDR(v)	(((v) >> 23) & 0x1f)
#define MMFR_REGNUM(v)	(((v) >> 18) & 0x1f)

static uint64_t mcf_fec_read_mdio(mcf_fec_state *s)
{
    uint64_t v;

    if (s->mmfr & MMFR_WRITE_OP)
        return s->mmfr;
    if (MMFR_PHYADDR(s->mmfr) != 1)
        return s->mmfr |= 0xffff;

    switch (MMFR_REGNUM(s->mmfr)) {
    case MII_BMCR:
        v = MII_BMCR_SPEED | MII_BMCR_AUTOEN | MII_BMCR_FD;
        break;
    case MII_BMSR:
        v = MII_BMSR_100TX_FD | MII_BMSR_100TX_HD | MII_BMSR_10T_FD |
            MII_BMSR_10T_HD | MII_BMSR_MFPS | MII_BMSR_AN_COMP |
            MII_BMSR_AUTONEG | MII_BMSR_LINK_ST;
        break;
    case MII_PHYID1:
        v = DP83848_PHYID1;
        break;
    case MII_PHYID2:
        v = DP83848_PHYID2;
        break;
    case MII_ANAR:
        v = MII_ANAR_TXFD | MII_ANAR_TX | MII_ANAR_10FD |
            MII_ANAR_10 | MII_ANAR_CSMACD;
        break;
    case MII_ANLPAR:
        v = MII_ANLPAR_ACK | MII_ANLPAR_TXFD | MII_ANLPAR_TX |
            MII_ANLPAR_10FD | MII_ANLPAR_10 | MII_ANLPAR_CSMACD;
        break;
    default:
        v = 0xffff;
        break;
    }
    s->mmfr = (s->mmfr & ~0xffff) | v;
    return s->mmfr;
}

static uint64_t mcf_fec_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    mcf_fec_state *s = (mcf_fec_state *)opaque;
    switch (addr & 0x3ff) {
    case 0x004: return s->eir;
    case 0x008: return s->eimr;
    case 0x010: return s->rx_enabled ? (1 << 24) : 0; /* RDAR */
    case 0x014: return 0; /* TDAR */
    case 0x024: return s->ecr;
    case 0x040: return mcf_fec_read_mdio(s);
    case 0x044: return s->mscr;
    case 0x064: return 0; /* MIBC */
    case 0x084: return s->rcr;
    case 0x0c4: return s->tcr;
    case 0x0e4: /* PALR */
        return (s->conf.macaddr.a[0] << 24) | (s->conf.macaddr.a[1] << 16)
              | (s->conf.macaddr.a[2] << 8) | s->conf.macaddr.a[3];
        break;
    case 0x0e8: /* PAUR */
        return (s->conf.macaddr.a[4] << 24) | (s->conf.macaddr.a[5] << 16) | 0x8808;
    case 0x0ec: return 0x10000; /* OPD */
    case 0x118: return 0;
    case 0x11c: return 0;
    case 0x120: return 0;
    case 0x124: return 0;
    case 0x144: return s->tfwr;
    case 0x14c: return 0x600;
    case 0x150: return s->rfsr;
    case 0x180: return s->erdsr;
    case 0x184: return s->etdsr;
    case 0x188: return s->emrbr;
    case 0x200 ... 0x2e0: return s->mib[(addr & 0x1ff) / 4];
    default:
        hw_error("mcf_fec_read: Bad address 0x%x\n", (int)addr);
        return 0;
    }
}

static void mcf_fec_write(void *opaque, hwaddr addr,
                          uint64_t value, unsigned size)
{
    mcf_fec_state *s = (mcf_fec_state *)opaque;
    switch (addr & 0x3ff) {
    case 0x004:
        s->eir &= ~value;
        break;
    case 0x008:
        s->eimr = value;
        break;
    case 0x010: /* RDAR */
        if ((s->ecr & FEC_EN) && !s->rx_enabled) {
            DPRINTF("RX enable\n");
            mcf_fec_enable_rx(s);
        }
        break;
    case 0x014: /* TDAR */
        if (s->ecr & FEC_EN) {
            mcf_fec_do_tx(s);
        }
        break;
    case 0x024:
        s->ecr = value;
        if (value & FEC_RESET) {
            DPRINTF("Reset\n");
            mcf_fec_reset(opaque);
        }
        if ((s->ecr & FEC_EN) == 0) {
            s->rx_enabled = 0;
        }
        break;
    case 0x040:
        s->mmfr = value;
        s->eir |= FEC_INT_MII;
        break;
    case 0x044:
        s->mscr = value & 0xfe;
        break;
    case 0x064:
        /* TODO: Implement MIB.  */
        break;
    case 0x084:
        s->rcr = value & 0x07ff003f;
        /* TODO: Implement LOOP mode.  */
        break;
    case 0x0c4: /* TCR */
        /* We transmit immediately, so raise GRA immediately.  */
        s->tcr = value;
        if (value & 1)
            s->eir |= FEC_INT_GRA;
        break;
    case 0x0e4: /* PALR */
        s->conf.macaddr.a[0] = value >> 24;
        s->conf.macaddr.a[1] = value >> 16;
        s->conf.macaddr.a[2] = value >> 8;
        s->conf.macaddr.a[3] = value;
        break;
    case 0x0e8: /* PAUR */
        s->conf.macaddr.a[4] = value >> 24;
        s->conf.macaddr.a[5] = value >> 16;
        break;
    case 0x0ec:
        /* OPD */
        break;
    case 0x118:
    case 0x11c:
    case 0x120:
    case 0x124:
        /* TODO: implement MAC hash filtering.  */
        break;
    case 0x144:
        s->tfwr = value & 3;
        break;
    case 0x14c:
        /* FRBR writes ignored.  */
        break;
    case 0x150:
        s->rfsr = (value & 0x3fc) | 0x400;
        break;
    case 0x180:
        s->erdsr = value & ~3;
        s->rx_descriptor = s->erdsr;
        break;
    case 0x184:
        s->etdsr = value & ~3;
        s->tx_descriptor = s->etdsr;
        break;
    case 0x188:
        s->emrbr = value > 0 ? value & 0x7F0 : 0x7F0;
        break;
    case 0x200 ... 0x2e0:
        s->mib[(addr & 0x1ff) / 4] = value;
        break;
    default:
        hw_error("mcf_fec_write Bad address 0x%x\n", (int)addr);
    }
    mcf_fec_update(s);
}

static void mcf_fec_rx_stats(mcf_fec_state *s, int size)
{
    s->mib[MIB_RMON_R_PACKETS]++;
    s->mib[MIB_RMON_R_OCTETS] += size;
    if (size < 64) {
        s->mib[MIB_RMON_R_FRAG]++;
    } else if (size == 64) {
        s->mib[MIB_RMON_R_P64]++;
    } else if (size < 128) {
        s->mib[MIB_RMON_R_P65TO127]++;
    } else if (size < 256) {
        s->mib[MIB_RMON_R_P128TO255]++;
    } else if (size < 512) {
        s->mib[MIB_RMON_R_P256TO511]++;
    } else if (size < 1024) {
        s->mib[MIB_RMON_R_P512TO1023]++;
    } else if (size < 2048) {
        s->mib[MIB_RMON_R_P1024TO2047]++;
    } else {
        s->mib[MIB_RMON_R_P_GTE2048]++;
    }
    s->mib[MIB_IEEE_R_FRAME_OK]++;
    s->mib[MIB_IEEE_R_OCTETS_OK] += size;
}

static int mcf_fec_have_receive_space(mcf_fec_state *s, size_t want)
{
    mcf_fec_bd bd;
    uint32_t addr;

    /* Walk descriptor list to determine if we have enough buffer */
    addr = s->rx_descriptor;
    while (want > 0) {
        mcf_fec_read_bd(&bd, addr);
        if ((bd.flags & FEC_BD_E) == 0) {
            return 0;
        }
        if (want < s->emrbr) {
            return 1;
        }
        want -= s->emrbr;
        /* Advance to the next descriptor.  */
        if ((bd.flags & FEC_BD_W) != 0) {
            addr = s->erdsr;
        } else {
            addr += 8;
        }
    }
    return 0;
}

static ssize_t mcf_fec_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    mcf_fec_state *s = qemu_get_nic_opaque(nc);
    mcf_fec_bd bd;
    uint32_t flags = 0;
    uint32_t addr;
    uint32_t crc;
    uint32_t buf_addr;
    uint8_t *crc_ptr;
    unsigned int buf_len;
    size_t retsize;

    DPRINTF("do_rx len %d\n", size);
    if (!s->rx_enabled) {
        return -1;
    }
    /* 4 bytes for the CRC.  */
    size += 4;
    crc = cpu_to_be32(crc32(~0, buf, size));
    crc_ptr = (uint8_t *)&crc;
    /* Huge frames are truncted.  */
    if (size > FEC_MAX_FRAME_SIZE) {
        size = FEC_MAX_FRAME_SIZE;
        flags |= FEC_BD_TR | FEC_BD_LG;
    }
    /* Frames larger than the user limit just set error flags.  */
    if (size > (s->rcr >> 16)) {
        flags |= FEC_BD_LG;
    }
    /* Check if we have enough space in current descriptors */
    if (!mcf_fec_have_receive_space(s, size)) {
        return 0;
    }
    addr = s->rx_descriptor;
    retsize = size;
    while (size > 0) {
        mcf_fec_read_bd(&bd, addr);
        buf_len = (size <= s->emrbr) ? size: s->emrbr;
        bd.length = buf_len;
        size -= buf_len;
        DPRINTF("rx_bd %x length %d\n", addr, bd.length);
        /* The last 4 bytes are the CRC.  */
        if (size < 4)
            buf_len += size - 4;
        buf_addr = bd.data;
        cpu_physical_memory_write(buf_addr, buf, buf_len);
        buf += buf_len;
        if (size < 4) {
            cpu_physical_memory_write(buf_addr + buf_len, crc_ptr, 4 - size);
            crc_ptr += 4 - size;
        }
        bd.flags &= ~FEC_BD_E;
        if (size == 0) {
            /* Last buffer in frame.  */
            bd.flags |= flags | FEC_BD_L;
            DPRINTF("rx frame flags %04x\n", bd.flags);
            s->eir |= FEC_INT_RXF;
        } else {
            s->eir |= FEC_INT_RXB;
        }
        mcf_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & FEC_BD_W) != 0) {
            addr = s->erdsr;
        } else {
            addr += 8;
        }
    }
    s->rx_descriptor = addr;
    mcf_fec_rx_stats(s, retsize);
    mcf_fec_enable_rx(s);
    mcf_fec_update(s);
    return retsize;
}

static const MemoryRegionOps mcf_fec_ops = {
    .read = mcf_fec_read,
    .write = mcf_fec_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static NetClientInfo net_mcf_fec_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = mcf_fec_receive,
};

static void mcf_fec_realize(DeviceState *dev, Error **errp)
{
    mcf_fec_state *s = MCF_FEC_NET(dev);

    s->nic = qemu_new_nic(&net_mcf_fec_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void mcf_fec_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    mcf_fec_state *s = MCF_FEC_NET(obj);
    int i;

    memory_region_init_io(&s->iomem, obj, &mcf_fec_ops, s, "fec", 0x400);
    sysbus_init_mmio(sbd, &s->iomem);
    for (i = 0; i < FEC_NUM_IRQ; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
}

static Property mcf_fec_properties[] = {
    DEFINE_NIC_PROPERTIES(mcf_fec_state, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void mcf_fec_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->realize = mcf_fec_realize;
    dc->desc = "MCF Fast Ethernet Controller network device";
    dc->reset = mcf_fec_reset;
    dc->props = mcf_fec_properties;
}

static const TypeInfo mcf_fec_info = {
    .name          = TYPE_MCF_FEC_NET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mcf_fec_state),
    .instance_init = mcf_fec_instance_init,
    .class_init    = mcf_fec_class_init,
};

static void mcf_fec_register_types(void)
{
    type_register_static(&mcf_fec_info);
}

type_init(mcf_fec_register_types)
