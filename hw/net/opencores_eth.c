/*
 * OpenCores Ethernet MAC 10/100 + subset of
 * National Semiconductors DP83848C 10/100 PHY
 *
 * http://opencores.org/svnget,ethmac?file=%2Ftrunk%2F%2Fdoc%2Feth_speci.pdf
 * http://cache.national.com/ds/DP/DP83848C.pdf
 *
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "trace.h"

/* RECSMALL is not used because it breaks tap networking in linux:
 * incoming ARP responses are too short
 */
#undef USE_RECSMALL

#define GET_FIELD(v, field) (((v) & (field)) >> (field ## _LBN))
#define GET_REGBIT(s, reg, field) ((s)->regs[reg] & (reg ## _ ## field))
#define GET_REGFIELD(s, reg, field) \
    GET_FIELD((s)->regs[reg], reg ## _ ## field)

#define SET_FIELD(v, field, data) \
    ((v) = (((v) & ~(field)) | (((data) << (field ## _LBN)) & (field))))
#define SET_REGFIELD(s, reg, field, data) \
    SET_FIELD((s)->regs[reg], reg ## _ ## field, data)

/* PHY MII registers */
enum {
    MII_BMCR,
    MII_BMSR,
    MII_PHYIDR1,
    MII_PHYIDR2,
    MII_ANAR,
    MII_ANLPAR,
    MII_REG_MAX = 16,
};

typedef struct Mii {
    uint16_t regs[MII_REG_MAX];
    bool link_ok;
} Mii;

static void mii_set_link(Mii *s, bool link_ok)
{
    if (link_ok) {
        s->regs[MII_BMSR] |= 0x4;
        s->regs[MII_ANLPAR] |= 0x01e1;
    } else {
        s->regs[MII_BMSR] &= ~0x4;
        s->regs[MII_ANLPAR] &= 0x01ff;
    }
    s->link_ok = link_ok;
}

static void mii_reset(Mii *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[MII_BMCR] = 0x1000;
    s->regs[MII_BMSR] = 0x7848; /* no ext regs */
    s->regs[MII_PHYIDR1] = 0x2000;
    s->regs[MII_PHYIDR2] = 0x5c90;
    s->regs[MII_ANAR] = 0x01e1;
    mii_set_link(s, s->link_ok);
}

static void mii_ro(Mii *s, uint16_t v)
{
}

static void mii_write_bmcr(Mii *s, uint16_t v)
{
    if (v & 0x8000) {
        mii_reset(s);
    } else {
        s->regs[MII_BMCR] = v;
    }
}

static void mii_write_host(Mii *s, unsigned idx, uint16_t v)
{
    static void (*reg_write[MII_REG_MAX])(Mii *s, uint16_t v) = {
        [MII_BMCR] = mii_write_bmcr,
        [MII_BMSR] = mii_ro,
        [MII_PHYIDR1] = mii_ro,
        [MII_PHYIDR2] = mii_ro,
    };

    if (idx < MII_REG_MAX) {
        trace_open_eth_mii_write(idx, v);
        if (reg_write[idx]) {
            reg_write[idx](s, v);
        } else {
            s->regs[idx] = v;
        }
    }
}

static uint16_t mii_read_host(Mii *s, unsigned idx)
{
    trace_open_eth_mii_read(idx, s->regs[idx]);
    return s->regs[idx];
}

/* OpenCores Ethernet registers */
enum {
    MODER,
    INT_SOURCE,
    INT_MASK,
    IPGT,
    IPGR1,
    IPGR2,
    PACKETLEN,
    COLLCONF,
    TX_BD_NUM,
    CTRLMODER,
    MIIMODER,
    MIICOMMAND,
    MIIADDRESS,
    MIITX_DATA,
    MIIRX_DATA,
    MIISTATUS,
    MAC_ADDR0,
    MAC_ADDR1,
    HASH0,
    HASH1,
    TXCTRL,
    REG_MAX,
};

enum {
    MODER_RECSMALL = 0x10000,
    MODER_PAD = 0x8000,
    MODER_HUGEN = 0x4000,
    MODER_RST = 0x800,
    MODER_LOOPBCK = 0x80,
    MODER_PRO = 0x20,
    MODER_IAM = 0x10,
    MODER_BRO = 0x8,
    MODER_TXEN = 0x2,
    MODER_RXEN = 0x1,
};

enum {
    INT_SOURCE_BUSY = 0x10,
    INT_SOURCE_RXB = 0x4,
    INT_SOURCE_TXB = 0x1,
};

enum {
    PACKETLEN_MINFL = 0xffff0000,
    PACKETLEN_MINFL_LBN = 16,
    PACKETLEN_MAXFL = 0xffff,
    PACKETLEN_MAXFL_LBN = 0,
};

enum {
    MIICOMMAND_WCTRLDATA = 0x4,
    MIICOMMAND_RSTAT = 0x2,
    MIICOMMAND_SCANSTAT = 0x1,
};

enum {
    MIIADDRESS_RGAD = 0x1f00,
    MIIADDRESS_RGAD_LBN = 8,
    MIIADDRESS_FIAD = 0x1f,
    MIIADDRESS_FIAD_LBN = 0,
};

enum {
    MIITX_DATA_CTRLDATA = 0xffff,
    MIITX_DATA_CTRLDATA_LBN = 0,
};

enum {
    MIIRX_DATA_PRSD = 0xffff,
    MIIRX_DATA_PRSD_LBN = 0,
};

enum {
    MIISTATUS_LINKFAIL = 0x1,
    MIISTATUS_LINKFAIL_LBN = 0,
};

enum {
    MAC_ADDR0_BYTE2 = 0xff000000,
    MAC_ADDR0_BYTE2_LBN = 24,
    MAC_ADDR0_BYTE3 = 0xff0000,
    MAC_ADDR0_BYTE3_LBN = 16,
    MAC_ADDR0_BYTE4 = 0xff00,
    MAC_ADDR0_BYTE4_LBN = 8,
    MAC_ADDR0_BYTE5 = 0xff,
    MAC_ADDR0_BYTE5_LBN = 0,
};

enum {
    MAC_ADDR1_BYTE0 = 0xff00,
    MAC_ADDR1_BYTE0_LBN = 8,
    MAC_ADDR1_BYTE1 = 0xff,
    MAC_ADDR1_BYTE1_LBN = 0,
};

enum {
    TXD_LEN = 0xffff0000,
    TXD_LEN_LBN = 16,
    TXD_RD = 0x8000,
    TXD_IRQ = 0x4000,
    TXD_WR = 0x2000,
    TXD_PAD = 0x1000,
    TXD_CRC = 0x800,
    TXD_UR = 0x100,
    TXD_RTRY = 0xf0,
    TXD_RTRY_LBN = 4,
    TXD_RL = 0x8,
    TXD_LC = 0x4,
    TXD_DF = 0x2,
    TXD_CS = 0x1,
};

enum {
    RXD_LEN = 0xffff0000,
    RXD_LEN_LBN = 16,
    RXD_E = 0x8000,
    RXD_IRQ = 0x4000,
    RXD_WRAP = 0x2000,
    RXD_CF = 0x100,
    RXD_M = 0x80,
    RXD_OR = 0x40,
    RXD_IS = 0x20,
    RXD_DN = 0x10,
    RXD_TL = 0x8,
    RXD_SF = 0x4,
    RXD_CRC = 0x2,
    RXD_LC = 0x1,
};

typedef struct desc {
    uint32_t len_flags;
    uint32_t buf_ptr;
} desc;

#define DEFAULT_PHY 1

#define TYPE_OPEN_ETH "open_eth"
#define OPEN_ETH(obj) OBJECT_CHECK(OpenEthState, (obj), TYPE_OPEN_ETH)

typedef struct OpenEthState {
    SysBusDevice parent_obj;

    NICState *nic;
    NICConf conf;
    MemoryRegion reg_io;
    MemoryRegion desc_io;
    qemu_irq irq;

    Mii mii;
    uint32_t regs[REG_MAX];
    unsigned tx_desc;
    unsigned rx_desc;
    desc desc[128];
} OpenEthState;

static desc *rx_desc(OpenEthState *s)
{
    return s->desc + s->rx_desc;
}

static desc *tx_desc(OpenEthState *s)
{
    return s->desc + s->tx_desc;
}

static void open_eth_update_irq(OpenEthState *s,
        uint32_t old, uint32_t new)
{
    if (!old != !new) {
        trace_open_eth_update_irq(new);
        qemu_set_irq(s->irq, new);
    }
}

static void open_eth_int_source_write(OpenEthState *s,
        uint32_t val)
{
    uint32_t old_val = s->regs[INT_SOURCE];

    s->regs[INT_SOURCE] = val;
    open_eth_update_irq(s, old_val & s->regs[INT_MASK],
            s->regs[INT_SOURCE] & s->regs[INT_MASK]);
}

static void open_eth_set_link_status(NetClientState *nc)
{
    OpenEthState *s = qemu_get_nic_opaque(nc);

    if (GET_REGBIT(s, MIICOMMAND, SCANSTAT)) {
        SET_REGFIELD(s, MIISTATUS, LINKFAIL, nc->link_down);
    }
    mii_set_link(&s->mii, !nc->link_down);
}

static void open_eth_reset(void *opaque)
{
    OpenEthState *s = opaque;

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[MODER] = 0xa000;
    s->regs[IPGT] = 0x12;
    s->regs[IPGR1] = 0xc;
    s->regs[IPGR2] = 0x12;
    s->regs[PACKETLEN] = 0x400600;
    s->regs[COLLCONF] = 0xf003f;
    s->regs[TX_BD_NUM] = 0x40;
    s->regs[MIIMODER] = 0x64;

    s->tx_desc = 0;
    s->rx_desc = 0x40;

    mii_reset(&s->mii);
    open_eth_set_link_status(qemu_get_queue(s->nic));
}

static int open_eth_can_receive(NetClientState *nc)
{
    OpenEthState *s = qemu_get_nic_opaque(nc);

    return GET_REGBIT(s, MODER, RXEN) &&
        (s->regs[TX_BD_NUM] < 0x80);
}

static ssize_t open_eth_receive(NetClientState *nc,
        const uint8_t *buf, size_t size)
{
    OpenEthState *s = qemu_get_nic_opaque(nc);
    size_t maxfl = GET_REGFIELD(s, PACKETLEN, MAXFL);
    size_t minfl = GET_REGFIELD(s, PACKETLEN, MINFL);
    size_t fcsl = 4;
    bool miss = true;

    trace_open_eth_receive((unsigned)size);

    if (size >= 6) {
        static const uint8_t bcast_addr[] = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        };
        if (memcmp(buf, bcast_addr, sizeof(bcast_addr)) == 0) {
            miss = GET_REGBIT(s, MODER, BRO);
        } else if ((buf[0] & 0x1) || GET_REGBIT(s, MODER, IAM)) {
            unsigned mcast_idx = compute_mcast_idx(buf);
            miss = !(s->regs[HASH0 + mcast_idx / 32] &
                    (1 << (mcast_idx % 32)));
            trace_open_eth_receive_mcast(
                    mcast_idx, s->regs[HASH0], s->regs[HASH1]);
        } else {
            miss = GET_REGFIELD(s, MAC_ADDR1, BYTE0) != buf[0] ||
                GET_REGFIELD(s, MAC_ADDR1, BYTE1) != buf[1] ||
                GET_REGFIELD(s, MAC_ADDR0, BYTE2) != buf[2] ||
                GET_REGFIELD(s, MAC_ADDR0, BYTE3) != buf[3] ||
                GET_REGFIELD(s, MAC_ADDR0, BYTE4) != buf[4] ||
                GET_REGFIELD(s, MAC_ADDR0, BYTE5) != buf[5];
        }
    }

    if (miss && !GET_REGBIT(s, MODER, PRO)) {
        trace_open_eth_receive_reject();
        return size;
    }

#ifdef USE_RECSMALL
    if (GET_REGBIT(s, MODER, RECSMALL) || size >= minfl) {
#else
    {
#endif
        static const uint8_t zero[64] = {0};
        desc *desc = rx_desc(s);
        size_t copy_size = GET_REGBIT(s, MODER, HUGEN) ? 65536 : maxfl;

        if (!(desc->len_flags & RXD_E)) {
            open_eth_int_source_write(s,
                    s->regs[INT_SOURCE] | INT_SOURCE_BUSY);
            return size;
        }

        desc->len_flags &= ~(RXD_CF | RXD_M | RXD_OR |
                RXD_IS | RXD_DN | RXD_TL | RXD_SF | RXD_CRC | RXD_LC);

        if (copy_size > size) {
            copy_size = size;
        } else {
            fcsl = 0;
        }
        if (miss) {
            desc->len_flags |= RXD_M;
        }
        if (GET_REGBIT(s, MODER, HUGEN) && size > maxfl) {
            desc->len_flags |= RXD_TL;
        }
#ifdef USE_RECSMALL
        if (size < minfl) {
            desc->len_flags |= RXD_SF;
        }
#endif

        cpu_physical_memory_write(desc->buf_ptr, buf, copy_size);

        if (GET_REGBIT(s, MODER, PAD) && copy_size < minfl) {
            if (minfl - copy_size > fcsl) {
                fcsl = 0;
            } else {
                fcsl -= minfl - copy_size;
            }
            while (copy_size < minfl) {
                size_t zero_sz = minfl - copy_size < sizeof(zero) ?
                    minfl - copy_size : sizeof(zero);

                cpu_physical_memory_write(desc->buf_ptr + copy_size,
                        zero, zero_sz);
                copy_size += zero_sz;
            }
        }

        /* There's no FCS in the frames handed to us by the QEMU, zero fill it.
         * Don't do it if the frame is cut at the MAXFL or padded with 4 or
         * more bytes to the MINFL.
         */
        cpu_physical_memory_write(desc->buf_ptr + copy_size, zero, fcsl);
        copy_size += fcsl;

        SET_FIELD(desc->len_flags, RXD_LEN, copy_size);

        if ((desc->len_flags & RXD_WRAP) || s->rx_desc == 0x7f) {
            s->rx_desc = s->regs[TX_BD_NUM];
        } else {
            ++s->rx_desc;
        }
        desc->len_flags &= ~RXD_E;

        trace_open_eth_receive_desc(desc->buf_ptr, desc->len_flags);

        if (desc->len_flags & RXD_IRQ) {
            open_eth_int_source_write(s,
                    s->regs[INT_SOURCE] | INT_SOURCE_RXB);
        }
    }
    return size;
}

static void open_eth_cleanup(NetClientState *nc)
{
}

static NetClientInfo net_open_eth_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = open_eth_can_receive,
    .receive = open_eth_receive,
    .cleanup = open_eth_cleanup,
    .link_status_changed = open_eth_set_link_status,
};

static void open_eth_start_xmit(OpenEthState *s, desc *tx)
{
    uint8_t buf[65536];
    unsigned len = GET_FIELD(tx->len_flags, TXD_LEN);
    unsigned tx_len = len;

    if ((tx->len_flags & TXD_PAD) &&
            tx_len < GET_REGFIELD(s, PACKETLEN, MINFL)) {
        tx_len = GET_REGFIELD(s, PACKETLEN, MINFL);
    }
    if (!GET_REGBIT(s, MODER, HUGEN) &&
            tx_len > GET_REGFIELD(s, PACKETLEN, MAXFL)) {
        tx_len = GET_REGFIELD(s, PACKETLEN, MAXFL);
    }

    trace_open_eth_start_xmit(tx->buf_ptr, len, tx_len);

    if (len > tx_len) {
        len = tx_len;
    }
    cpu_physical_memory_read(tx->buf_ptr, buf, len);
    if (tx_len > len) {
        memset(buf + len, 0, tx_len - len);
    }
    qemu_send_packet(qemu_get_queue(s->nic), buf, tx_len);

    if (tx->len_flags & TXD_WR) {
        s->tx_desc = 0;
    } else {
        ++s->tx_desc;
        if (s->tx_desc >= s->regs[TX_BD_NUM]) {
            s->tx_desc = 0;
        }
    }
    tx->len_flags &= ~(TXD_RD | TXD_UR |
            TXD_RTRY | TXD_RL | TXD_LC | TXD_DF | TXD_CS);
    if (tx->len_flags & TXD_IRQ) {
        open_eth_int_source_write(s, s->regs[INT_SOURCE] | INT_SOURCE_TXB);
    }

}

static void open_eth_check_start_xmit(OpenEthState *s)
{
    desc *tx = tx_desc(s);
    if (GET_REGBIT(s, MODER, TXEN) && s->regs[TX_BD_NUM] > 0 &&
            (tx->len_flags & TXD_RD) &&
            GET_FIELD(tx->len_flags, TXD_LEN) > 4) {
        open_eth_start_xmit(s, tx);
    }
}

static uint64_t open_eth_reg_read(void *opaque,
        hwaddr addr, unsigned int size)
{
    static uint32_t (*reg_read[REG_MAX])(OpenEthState *s) = {
    };
    OpenEthState *s = opaque;
    unsigned idx = addr / 4;
    uint64_t v = 0;

    if (idx < REG_MAX) {
        if (reg_read[idx]) {
            v = reg_read[idx](s);
        } else {
            v = s->regs[idx];
        }
    }
    trace_open_eth_reg_read((uint32_t)addr, (uint32_t)v);
    return v;
}

static void open_eth_notify_can_receive(OpenEthState *s)
{
    NetClientState *nc = qemu_get_queue(s->nic);

    if (open_eth_can_receive(nc)) {
        qemu_flush_queued_packets(nc);
    }
}

static void open_eth_ro(OpenEthState *s, uint32_t val)
{
}

static void open_eth_moder_host_write(OpenEthState *s, uint32_t val)
{
    uint32_t set = val & ~s->regs[MODER];

    if (set & MODER_RST) {
        open_eth_reset(s);
    }

    s->regs[MODER] = val;

    if (set & MODER_RXEN) {
        s->rx_desc = s->regs[TX_BD_NUM];
        open_eth_notify_can_receive(s);
    }
    if (set & MODER_TXEN) {
        s->tx_desc = 0;
        open_eth_check_start_xmit(s);
    }
}

static void open_eth_int_source_host_write(OpenEthState *s, uint32_t val)
{
    uint32_t old = s->regs[INT_SOURCE];

    s->regs[INT_SOURCE] &= ~val;
    open_eth_update_irq(s, old & s->regs[INT_MASK],
            s->regs[INT_SOURCE] & s->regs[INT_MASK]);
}

static void open_eth_int_mask_host_write(OpenEthState *s, uint32_t val)
{
    uint32_t old = s->regs[INT_MASK];

    s->regs[INT_MASK] = val;
    open_eth_update_irq(s, s->regs[INT_SOURCE] & old,
            s->regs[INT_SOURCE] & s->regs[INT_MASK]);
}

static void open_eth_tx_bd_num_host_write(OpenEthState *s, uint32_t val)
{
    if (val < 0x80) {
        bool enable = s->regs[TX_BD_NUM] == 0x80;

        s->regs[TX_BD_NUM] = val;
        if (enable) {
            open_eth_notify_can_receive(s);
        }
    }
}

static void open_eth_mii_command_host_write(OpenEthState *s, uint32_t val)
{
    unsigned fiad = GET_REGFIELD(s, MIIADDRESS, FIAD);
    unsigned rgad = GET_REGFIELD(s, MIIADDRESS, RGAD);

    if (val & MIICOMMAND_WCTRLDATA) {
        if (fiad == DEFAULT_PHY) {
            mii_write_host(&s->mii, rgad,
                    GET_REGFIELD(s, MIITX_DATA, CTRLDATA));
        }
    }
    if (val & MIICOMMAND_RSTAT) {
        if (fiad == DEFAULT_PHY) {
            SET_REGFIELD(s, MIIRX_DATA, PRSD,
                    mii_read_host(&s->mii, rgad));
        } else {
            s->regs[MIIRX_DATA] = 0xffff;
        }
        SET_REGFIELD(s, MIISTATUS, LINKFAIL, qemu_get_queue(s->nic)->link_down);
    }
}

static void open_eth_mii_tx_host_write(OpenEthState *s, uint32_t val)
{
    SET_REGFIELD(s, MIITX_DATA, CTRLDATA, val);
    if (GET_REGFIELD(s, MIIADDRESS, FIAD) == DEFAULT_PHY) {
        mii_write_host(&s->mii, GET_REGFIELD(s, MIIADDRESS, RGAD),
                GET_REGFIELD(s, MIITX_DATA, CTRLDATA));
    }
}

static void open_eth_reg_write(void *opaque,
        hwaddr addr, uint64_t val, unsigned int size)
{
    static void (*reg_write[REG_MAX])(OpenEthState *s, uint32_t val) = {
        [MODER] = open_eth_moder_host_write,
        [INT_SOURCE] = open_eth_int_source_host_write,
        [INT_MASK] = open_eth_int_mask_host_write,
        [TX_BD_NUM] = open_eth_tx_bd_num_host_write,
        [MIICOMMAND] = open_eth_mii_command_host_write,
        [MIITX_DATA] = open_eth_mii_tx_host_write,
        [MIISTATUS] = open_eth_ro,
    };
    OpenEthState *s = opaque;
    unsigned idx = addr / 4;

    if (idx < REG_MAX) {
        trace_open_eth_reg_write((uint32_t)addr, (uint32_t)val);
        if (reg_write[idx]) {
            reg_write[idx](s, val);
        } else {
            s->regs[idx] = val;
        }
    }
}

static uint64_t open_eth_desc_read(void *opaque,
        hwaddr addr, unsigned int size)
{
    OpenEthState *s = opaque;
    uint64_t v = 0;

    addr &= 0x3ff;
    memcpy(&v, (uint8_t *)s->desc + addr, size);
    trace_open_eth_desc_read((uint32_t)addr, (uint32_t)v);
    return v;
}

static void open_eth_desc_write(void *opaque,
        hwaddr addr, uint64_t val, unsigned int size)
{
    OpenEthState *s = opaque;

    addr &= 0x3ff;
    trace_open_eth_desc_write((uint32_t)addr, (uint32_t)val);
    memcpy((uint8_t *)s->desc + addr, &val, size);
    open_eth_check_start_xmit(s);
}


static const MemoryRegionOps open_eth_reg_ops = {
    .read = open_eth_reg_read,
    .write = open_eth_reg_write,
};

static const MemoryRegionOps open_eth_desc_ops = {
    .read = open_eth_desc_read,
    .write = open_eth_desc_write,
};

static int sysbus_open_eth_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    OpenEthState *s = OPEN_ETH(dev);

    memory_region_init_io(&s->reg_io, OBJECT(dev), &open_eth_reg_ops, s,
            "open_eth.regs", 0x54);
    sysbus_init_mmio(sbd, &s->reg_io);

    memory_region_init_io(&s->desc_io, OBJECT(dev), &open_eth_desc_ops, s,
            "open_eth.desc", 0x400);
    sysbus_init_mmio(sbd, &s->desc_io);

    sysbus_init_irq(sbd, &s->irq);

    s->nic = qemu_new_nic(&net_open_eth_info, &s->conf,
                          object_get_typename(OBJECT(s)), dev->id, s);
    return 0;
}

static void qdev_open_eth_reset(DeviceState *dev)
{
    OpenEthState *d = OPEN_ETH(dev);

    open_eth_reset(d);
}

static Property open_eth_properties[] = {
    DEFINE_NIC_PROPERTIES(OpenEthState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void open_eth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = sysbus_open_eth_init;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "Opencores 10/100 Mbit Ethernet";
    dc->reset = qdev_open_eth_reset;
    dc->props = open_eth_properties;
}

static const TypeInfo open_eth_info = {
    .name          = TYPE_OPEN_ETH,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OpenEthState),
    .class_init    = open_eth_class_init,
};

static void open_eth_register_types(void)
{
    type_register_static(&open_eth_info);
}

type_init(open_eth_register_types)
