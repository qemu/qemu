/*
 * QEMU e1000 emulation
 *
 * Nir Peleg, Tutis Systems Ltd. for Qumranet Inc.
 * Copyright (c) 2008 Qumranet
 * Based on work done by:
 * Copyright (c) 2007 Dan Aloni
 * Copyright (c) 2004 Antony T Curtis
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */


#include "hw.h"
#include "pci.h"
#include "net.h"

#include "e1000_hw.h"

#define DEBUG

#ifdef DEBUG
enum {
    DEBUG_GENERAL,	DEBUG_IO,	DEBUG_MMIO,	DEBUG_INTERRUPT,
    DEBUG_RX,		DEBUG_TX,	DEBUG_MDIC,	DEBUG_EEPROM,
    DEBUG_UNKNOWN,	DEBUG_TXSUM,	DEBUG_TXERR,	DEBUG_RXERR,
    DEBUG_RXFILTER,	DEBUG_NOTYET,
};
#define DBGBIT(x)	(1<<DEBUG_##x)
static int debugflags = DBGBIT(TXERR) | DBGBIT(GENERAL);

#define	DBGOUT(what, fmt, params...) do { \
    if (debugflags & DBGBIT(what)) \
        fprintf(stderr, "e1000: " fmt, ##params); \
    } while (0)
#else
#define	DBGOUT(what, fmt, params...) do {} while (0)
#endif

#define IOPORT_SIZE       0x40
#define PNPMMIO_SIZE      0x20000

/*
 * HW models:
 *  E1000_DEV_ID_82540EM works with Windows and Linux
 *  E1000_DEV_ID_82573L OK with windoze and Linux 2.6.22,
 *	appears to perform better than 82540EM, but breaks with Linux 2.6.18
 *  E1000_DEV_ID_82544GC_COPPER appears to work; not well tested
 *  Others never tested
 */
enum { E1000_DEVID = E1000_DEV_ID_82540EM };

/*
 * May need to specify additional MAC-to-PHY entries --
 * Intel's Windows driver refuses to initialize unless they match
 */
enum {
    PHY_ID2_INIT = E1000_DEVID == E1000_DEV_ID_82573L ?		0xcc2 :
                   E1000_DEVID == E1000_DEV_ID_82544GC_COPPER ?	0xc30 :
                   /* default to E1000_DEV_ID_82540EM */	0xc20
};

typedef struct E1000State_st {
    PCIDevice dev;
    VLANClientState *vc;
    int mmio_index;

    uint32_t mac_reg[0x8000];
    uint16_t phy_reg[0x20];
    uint16_t eeprom_data[64];

    uint32_t rxbuf_size;
    uint32_t rxbuf_min_shift;
    int check_rxov;
    struct e1000_tx {
        unsigned char header[256];
        unsigned char vlan_header[4];
        unsigned char vlan[4];
        unsigned char data[0x10000];
        uint16_t size;
        unsigned char sum_needed;
        unsigned char vlan_needed;
        uint8_t ipcss;
        uint8_t ipcso;
        uint16_t ipcse;
        uint8_t tucss;
        uint8_t tucso;
        uint16_t tucse;
        uint8_t hdr_len;
        uint16_t mss;
        uint32_t paylen;
        uint16_t tso_frames;
        char tse;
        int8_t ip;
        int8_t tcp;
        char cptse;     // current packet tse bit
    } tx;

    struct {
        uint32_t val_in;	// shifted in from guest driver
        uint16_t bitnum_in;
        uint16_t bitnum_out;
        uint16_t reading;
        uint32_t old_eecd;
    } eecd_state;
} E1000State;

#define	defreg(x)	x = (E1000_##x>>2)
enum {
    defreg(CTRL),	defreg(EECD),	defreg(EERD),	defreg(GPRC),
    defreg(GPTC),	defreg(ICR),	defreg(ICS),	defreg(IMC),
    defreg(IMS),	defreg(LEDCTL),	defreg(MANC),	defreg(MDIC),
    defreg(MPC),	defreg(PBA),	defreg(RCTL),	defreg(RDBAH),
    defreg(RDBAL),	defreg(RDH),	defreg(RDLEN),	defreg(RDT),
    defreg(STATUS),	defreg(SWSM),	defreg(TCTL),	defreg(TDBAH),
    defreg(TDBAL),	defreg(TDH),	defreg(TDLEN),	defreg(TDT),
    defreg(TORH),	defreg(TORL),	defreg(TOTH),	defreg(TOTL),
    defreg(TPR),	defreg(TPT),	defreg(TXDCTL),	defreg(WUFC),
    defreg(RA),		defreg(MTA),	defreg(CRCERRS),defreg(VFTA),
    defreg(VET),
};

enum { PHY_R = 1, PHY_W = 2, PHY_RW = PHY_R | PHY_W };
static const char phy_regcap[0x20] = {
    [PHY_STATUS] = PHY_R,	[M88E1000_EXT_PHY_SPEC_CTRL] = PHY_RW,
    [PHY_ID1] = PHY_R,		[M88E1000_PHY_SPEC_CTRL] = PHY_RW,
    [PHY_CTRL] = PHY_RW,	[PHY_1000T_CTRL] = PHY_RW,
    [PHY_LP_ABILITY] = PHY_R,	[PHY_1000T_STATUS] = PHY_R,
    [PHY_AUTONEG_ADV] = PHY_RW,	[M88E1000_RX_ERR_CNTR] = PHY_R,
    [PHY_ID2] = PHY_R,		[M88E1000_PHY_SPEC_STATUS] = PHY_R
};

static void
ioport_map(PCIDevice *pci_dev, int region_num, uint32_t addr,
           uint32_t size, int type)
{
    DBGOUT(IO, "e1000_ioport_map addr=0x%04x size=0x%08x\n", addr, size);
}

static void
set_interrupt_cause(E1000State *s, int index, uint32_t val)
{
    if (val)
        val |= E1000_ICR_INT_ASSERTED;
    s->mac_reg[ICR] = val;
    qemu_set_irq(s->dev.irq[0], (s->mac_reg[IMS] & s->mac_reg[ICR]) != 0);
}

static void
set_ics(E1000State *s, int index, uint32_t val)
{
    DBGOUT(INTERRUPT, "set_ics %x, ICR %x, IMR %x\n", val, s->mac_reg[ICR],
        s->mac_reg[IMS]);
    set_interrupt_cause(s, 0, val | s->mac_reg[ICR]);
}

static int
rxbufsize(uint32_t v)
{
    v &= E1000_RCTL_BSEX | E1000_RCTL_SZ_16384 | E1000_RCTL_SZ_8192 |
         E1000_RCTL_SZ_4096 | E1000_RCTL_SZ_2048 | E1000_RCTL_SZ_1024 |
         E1000_RCTL_SZ_512 | E1000_RCTL_SZ_256;
    switch (v) {
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_16384:
        return 16384;
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_8192:
        return 8192;
    case E1000_RCTL_BSEX | E1000_RCTL_SZ_4096:
        return 4096;
    case E1000_RCTL_SZ_1024:
        return 1024;
    case E1000_RCTL_SZ_512:
        return 512;
    case E1000_RCTL_SZ_256:
        return 256;
    }
    return 2048;
}

static void
set_rx_control(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[RCTL] = val;
    s->rxbuf_size = rxbufsize(val);
    s->rxbuf_min_shift = ((val / E1000_RCTL_RDMTS_QUAT) & 3) + 1;
    DBGOUT(RX, "RCTL: %d, mac_reg[RCTL] = 0x%x\n", s->mac_reg[RDT],
           s->mac_reg[RCTL]);
}

static void
set_mdic(E1000State *s, int index, uint32_t val)
{
    uint32_t data = val & E1000_MDIC_DATA_MASK;
    uint32_t addr = ((val & E1000_MDIC_REG_MASK) >> E1000_MDIC_REG_SHIFT);

    if ((val & E1000_MDIC_PHY_MASK) >> E1000_MDIC_PHY_SHIFT != 1) // phy #
        val = s->mac_reg[MDIC] | E1000_MDIC_ERROR;
    else if (val & E1000_MDIC_OP_READ) {
        DBGOUT(MDIC, "MDIC read reg 0x%x\n", addr);
        if (!(phy_regcap[addr] & PHY_R)) {
            DBGOUT(MDIC, "MDIC read reg %x unhandled\n", addr);
            val |= E1000_MDIC_ERROR;
        } else
            val = (val ^ data) | s->phy_reg[addr];
    } else if (val & E1000_MDIC_OP_WRITE) {
        DBGOUT(MDIC, "MDIC write reg 0x%x, value 0x%x\n", addr, data);
        if (!(phy_regcap[addr] & PHY_W)) {
            DBGOUT(MDIC, "MDIC write reg %x unhandled\n", addr);
            val |= E1000_MDIC_ERROR;
        } else
            s->phy_reg[addr] = data;
    }
    s->mac_reg[MDIC] = val | E1000_MDIC_READY;
    set_ics(s, 0, E1000_ICR_MDAC);
}

static uint32_t
get_eecd(E1000State *s, int index)
{
    uint32_t ret = E1000_EECD_PRES|E1000_EECD_GNT | s->eecd_state.old_eecd;

    DBGOUT(EEPROM, "reading eeprom bit %d (reading %d)\n",
           s->eecd_state.bitnum_out, s->eecd_state.reading);
    if (!s->eecd_state.reading ||
        ((s->eeprom_data[(s->eecd_state.bitnum_out >> 4) & 0x3f] >>
          ((s->eecd_state.bitnum_out & 0xf) ^ 0xf))) & 1)
        ret |= E1000_EECD_DO;
    return ret;
}

static void
set_eecd(E1000State *s, int index, uint32_t val)
{
    uint32_t oldval = s->eecd_state.old_eecd;

    s->eecd_state.old_eecd = val & (E1000_EECD_SK | E1000_EECD_CS |
            E1000_EECD_DI|E1000_EECD_FWE_MASK|E1000_EECD_REQ);
    if (!(E1000_EECD_SK & (val ^ oldval)))	// no clock edge
        return;
    if (!(E1000_EECD_SK & val)) {		// falling edge
        s->eecd_state.bitnum_out++;
        return;
    }
    if (!(val & E1000_EECD_CS)) {		// rising, no CS (EEPROM reset)
        memset(&s->eecd_state, 0, sizeof s->eecd_state);
        return;
    }
    s->eecd_state.val_in <<= 1;
    if (val & E1000_EECD_DI)
        s->eecd_state.val_in |= 1;
    if (++s->eecd_state.bitnum_in == 9 && !s->eecd_state.reading) {
        s->eecd_state.bitnum_out = ((s->eecd_state.val_in & 0x3f)<<4)-1;
        s->eecd_state.reading = (((s->eecd_state.val_in >> 6) & 7) ==
            EEPROM_READ_OPCODE_MICROWIRE);
    }
    DBGOUT(EEPROM, "eeprom bitnum in %d out %d, reading %d\n",
           s->eecd_state.bitnum_in, s->eecd_state.bitnum_out,
           s->eecd_state.reading);
}

static uint32_t
flash_eerd_read(E1000State *s, int x)
{
    unsigned int index, r = s->mac_reg[EERD] & ~E1000_EEPROM_RW_REG_START;

    if ((index = r >> E1000_EEPROM_RW_ADDR_SHIFT) > EEPROM_CHECKSUM_REG)
        return 0;
    return (s->eeprom_data[index] << E1000_EEPROM_RW_REG_DATA) |
           E1000_EEPROM_RW_REG_DONE | r;
}

static void
putsum(uint8_t *data, uint32_t n, uint32_t sloc, uint32_t css, uint32_t cse)
{
    uint32_t sum;

    if (cse && cse < n)
        n = cse + 1;
    if (sloc < n-1) {
        sum = net_checksum_add(n-css, data+css);
        cpu_to_be16wu((uint16_t *)(data + sloc),
                      net_checksum_finish(sum));
    }
}

static inline int
vlan_enabled(E1000State *s)
{
    return ((s->mac_reg[CTRL] & E1000_CTRL_VME) != 0);
}

static inline int
vlan_rx_filter_enabled(E1000State *s)
{
    return ((s->mac_reg[RCTL] & E1000_RCTL_VFE) != 0);
}

static inline int
is_vlan_packet(E1000State *s, const uint8_t *buf)
{
    return (be16_to_cpup((uint16_t *)(buf + 12)) ==
                le16_to_cpup((uint16_t *)(s->mac_reg + VET)));
}

static inline int
is_vlan_txd(uint32_t txd_lower)
{
    return ((txd_lower & E1000_TXD_CMD_VLE) != 0);
}

static void
xmit_seg(E1000State *s)
{
    uint16_t len, *sp;
    unsigned int frames = s->tx.tso_frames, css, sofar, n;
    struct e1000_tx *tp = &s->tx;

    if (tp->tse && tp->cptse) {
        css = tp->ipcss;
        DBGOUT(TXSUM, "frames %d size %d ipcss %d\n",
               frames, tp->size, css);
        if (tp->ip) {		// IPv4
            cpu_to_be16wu((uint16_t *)(tp->data+css+2),
                          tp->size - css);
            cpu_to_be16wu((uint16_t *)(tp->data+css+4),
                          be16_to_cpup((uint16_t *)(tp->data+css+4))+frames);
        } else			// IPv6
            cpu_to_be16wu((uint16_t *)(tp->data+css+4),
                          tp->size - css);
        css = tp->tucss;
        len = tp->size - css;
        DBGOUT(TXSUM, "tcp %d tucss %d len %d\n", tp->tcp, css, len);
        if (tp->tcp) {
            sofar = frames * tp->mss;
            cpu_to_be32wu((uint32_t *)(tp->data+css+4),	// seq
                be32_to_cpupu((uint32_t *)(tp->data+css+4))+sofar);
            if (tp->paylen - sofar > tp->mss)
                tp->data[css + 13] &= ~9;		// PSH, FIN
        } else	// UDP
            cpu_to_be16wu((uint16_t *)(tp->data+css+4), len);
        if (tp->sum_needed & E1000_TXD_POPTS_TXSM) {
            // add pseudo-header length before checksum calculation
            sp = (uint16_t *)(tp->data + tp->tucso);
            cpu_to_be16wu(sp, be16_to_cpup(sp) + len);
        }
        tp->tso_frames++;
    }

    if (tp->sum_needed & E1000_TXD_POPTS_TXSM)
        putsum(tp->data, tp->size, tp->tucso, tp->tucss, tp->tucse);
    if (tp->sum_needed & E1000_TXD_POPTS_IXSM)
        putsum(tp->data, tp->size, tp->ipcso, tp->ipcss, tp->ipcse);
    if (tp->vlan_needed) {
        memmove(tp->vlan, tp->data, 12);
        memcpy(tp->data + 8, tp->vlan_header, 4);
        qemu_send_packet(s->vc, tp->vlan, tp->size + 4);
    } else
        qemu_send_packet(s->vc, tp->data, tp->size);
    s->mac_reg[TPT]++;
    s->mac_reg[GPTC]++;
    n = s->mac_reg[TOTL];
    if ((s->mac_reg[TOTL] += s->tx.size) < n)
        s->mac_reg[TOTH]++;
}

static void
process_tx_desc(E1000State *s, struct e1000_tx_desc *dp)
{
    uint32_t txd_lower = le32_to_cpu(dp->lower.data);
    uint32_t dtype = txd_lower & (E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D);
    unsigned int split_size = txd_lower & 0xffff, bytes, sz, op;
    unsigned int msh = 0xfffff, hdr = 0;
    uint64_t addr;
    struct e1000_context_desc *xp = (struct e1000_context_desc *)dp;
    struct e1000_tx *tp = &s->tx;

    if (dtype == E1000_TXD_CMD_DEXT) {	// context descriptor
        op = le32_to_cpu(xp->cmd_and_length);
        tp->ipcss = xp->lower_setup.ip_fields.ipcss;
        tp->ipcso = xp->lower_setup.ip_fields.ipcso;
        tp->ipcse = le16_to_cpu(xp->lower_setup.ip_fields.ipcse);
        tp->tucss = xp->upper_setup.tcp_fields.tucss;
        tp->tucso = xp->upper_setup.tcp_fields.tucso;
        tp->tucse = le16_to_cpu(xp->upper_setup.tcp_fields.tucse);
        tp->paylen = op & 0xfffff;
        tp->hdr_len = xp->tcp_seg_setup.fields.hdr_len;
        tp->mss = le16_to_cpu(xp->tcp_seg_setup.fields.mss);
        tp->ip = (op & E1000_TXD_CMD_IP) ? 1 : 0;
        tp->tcp = (op & E1000_TXD_CMD_TCP) ? 1 : 0;
        tp->tse = (op & E1000_TXD_CMD_TSE) ? 1 : 0;
        tp->tso_frames = 0;
        if (tp->tucso == 0) {	// this is probably wrong
            DBGOUT(TXSUM, "TCP/UDP: cso 0!\n");
            tp->tucso = tp->tucss + (tp->tcp ? 16 : 6);
        }
        return;
    } else if (dtype == (E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D)) {
        // data descriptor
        tp->sum_needed = le32_to_cpu(dp->upper.data) >> 8;
        tp->cptse = ( txd_lower & E1000_TXD_CMD_TSE ) ? 1 : 0;
    } else
        // legacy descriptor
        tp->cptse = 0;

    if (vlan_enabled(s) && is_vlan_txd(txd_lower) &&
        (tp->cptse || txd_lower & E1000_TXD_CMD_EOP)) {
        tp->vlan_needed = 1;
        cpu_to_be16wu((uint16_t *)(tp->vlan_header),
                      le16_to_cpup((uint16_t *)(s->mac_reg + VET)));
        cpu_to_be16wu((uint16_t *)(tp->vlan_header + 2),
                      le16_to_cpu(dp->upper.fields.special));
    }
        
    addr = le64_to_cpu(dp->buffer_addr);
    if (tp->tse && tp->cptse) {
        hdr = tp->hdr_len;
        msh = hdr + tp->mss;
        do {
            bytes = split_size;
            if (tp->size + bytes > msh)
                bytes = msh - tp->size;
            cpu_physical_memory_read(addr, tp->data + tp->size, bytes);
            if ((sz = tp->size + bytes) >= hdr && tp->size < hdr)
                memmove(tp->header, tp->data, hdr);
            tp->size = sz;
            addr += bytes;
            if (sz == msh) {
                xmit_seg(s);
                memmove(tp->data, tp->header, hdr);
                tp->size = hdr;
            }
        } while (split_size -= bytes);
    } else if (!tp->tse && tp->cptse) {
        // context descriptor TSE is not set, while data descriptor TSE is set
        DBGOUT(TXERR, "TCP segmentaion Error\n");
    } else {
        cpu_physical_memory_read(addr, tp->data + tp->size, split_size);
        tp->size += split_size;
    }

    if (!(txd_lower & E1000_TXD_CMD_EOP))
        return;
    if (!(tp->tse && tp->cptse && tp->size < hdr))
        xmit_seg(s);
    tp->tso_frames = 0;
    tp->sum_needed = 0;
    tp->vlan_needed = 0;
    tp->size = 0;
    tp->cptse = 0;
}

static uint32_t
txdesc_writeback(target_phys_addr_t base, struct e1000_tx_desc *dp)
{
    uint32_t txd_upper, txd_lower = le32_to_cpu(dp->lower.data);

    if (!(txd_lower & (E1000_TXD_CMD_RS|E1000_TXD_CMD_RPS)))
        return 0;
    txd_upper = (le32_to_cpu(dp->upper.data) | E1000_TXD_STAT_DD) &
                ~(E1000_TXD_STAT_EC | E1000_TXD_STAT_LC | E1000_TXD_STAT_TU);
    dp->upper.data = cpu_to_le32(txd_upper);
    cpu_physical_memory_write(base + ((char *)&dp->upper - (char *)dp),
                              (void *)&dp->upper, sizeof(dp->upper));
    return E1000_ICR_TXDW;
}

static void
start_xmit(E1000State *s)
{
    target_phys_addr_t base;
    struct e1000_tx_desc desc;
    uint32_t tdh_start = s->mac_reg[TDH], cause = E1000_ICS_TXQE;

    if (!(s->mac_reg[TCTL] & E1000_TCTL_EN)) {
        DBGOUT(TX, "tx disabled\n");
        return;
    }

    while (s->mac_reg[TDH] != s->mac_reg[TDT]) {
        base = ((uint64_t)s->mac_reg[TDBAH] << 32) + s->mac_reg[TDBAL] +
               sizeof(struct e1000_tx_desc) * s->mac_reg[TDH];
        cpu_physical_memory_read(base, (void *)&desc, sizeof(desc));

        DBGOUT(TX, "index %d: %p : %x %x\n", s->mac_reg[TDH],
               (void *)(intptr_t)desc.buffer_addr, desc.lower.data,
               desc.upper.data);

        process_tx_desc(s, &desc);
        cause |= txdesc_writeback(base, &desc);

        if (++s->mac_reg[TDH] * sizeof(desc) >= s->mac_reg[TDLEN])
            s->mac_reg[TDH] = 0;
        /*
         * the following could happen only if guest sw assigns
         * bogus values to TDT/TDLEN.
         * there's nothing too intelligent we could do about this.
         */
        if (s->mac_reg[TDH] == tdh_start) {
            DBGOUT(TXERR, "TDH wraparound @%x, TDT %x, TDLEN %x\n",
                   tdh_start, s->mac_reg[TDT], s->mac_reg[TDLEN]);
            break;
        }
    }
    set_ics(s, 0, cause);
}

static int
receive_filter(E1000State *s, const uint8_t *buf, int size)
{
    static uint8_t bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static int mta_shift[] = {4, 3, 2, 0};
    uint32_t f, rctl = s->mac_reg[RCTL], ra[2], *rp;

    if (is_vlan_packet(s, buf) && vlan_rx_filter_enabled(s)) {
        uint16_t vid = be16_to_cpup((uint16_t *)(buf + 14));
        uint32_t vfta = le32_to_cpup((uint32_t *)(s->mac_reg + VFTA) +
                                     ((vid >> 5) & 0x7f));
        if ((vfta & (1 << (vid & 0x1f))) == 0)
            return 0;
    }

    if (rctl & E1000_RCTL_UPE)			// promiscuous
        return 1;

    if ((buf[0] & 1) && (rctl & E1000_RCTL_MPE))	// promiscuous mcast
        return 1;

    if ((rctl & E1000_RCTL_BAM) && !memcmp(buf, bcast, sizeof bcast))
        return 1;

    for (rp = s->mac_reg + RA; rp < s->mac_reg + RA + 32; rp += 2) {
        if (!(rp[1] & E1000_RAH_AV))
            continue;
        ra[0] = cpu_to_le32(rp[0]);
        ra[1] = cpu_to_le32(rp[1]);
        if (!memcmp(buf, (uint8_t *)ra, 6)) {
            DBGOUT(RXFILTER,
                   "unicast match[%d]: %02x:%02x:%02x:%02x:%02x:%02x\n",
                   (int)(rp - s->mac_reg - RA)/2,
                   buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
            return 1;
        }
    }
    DBGOUT(RXFILTER, "unicast mismatch: %02x:%02x:%02x:%02x:%02x:%02x\n",
           buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    f = mta_shift[(rctl >> E1000_RCTL_MO_SHIFT) & 3];
    f = (((buf[5] << 8) | buf[4]) >> f) & 0xfff;
    if (s->mac_reg[MTA + (f >> 5)] & (1 << (f & 0x1f)))
        return 1;
    DBGOUT(RXFILTER,
           "dropping, inexact filter mismatch: %02x:%02x:%02x:%02x:%02x:%02x MO %d MTA[%d] %x\n",
           buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
           (rctl >> E1000_RCTL_MO_SHIFT) & 3, f >> 5,
           s->mac_reg[MTA + (f >> 5)]);

    return 0;
}

static void
e1000_set_link_status(VLANClientState *vc)
{
    E1000State *s = vc->opaque;
    uint32_t old_status = s->mac_reg[STATUS];

    if (vc->link_down)
        s->mac_reg[STATUS] &= ~E1000_STATUS_LU;
    else
        s->mac_reg[STATUS] |= E1000_STATUS_LU;

    if (s->mac_reg[STATUS] != old_status)
        set_ics(s, 0, E1000_ICR_LSC);
}

static int
e1000_can_receive(void *opaque)
{
    E1000State *s = opaque;

    return (s->mac_reg[RCTL] & E1000_RCTL_EN);
}

static void
e1000_receive(void *opaque, const uint8_t *buf, int size)
{
    E1000State *s = opaque;
    struct e1000_rx_desc desc;
    target_phys_addr_t base;
    unsigned int n, rdt;
    uint32_t rdh_start;
    uint16_t vlan_special = 0;
    uint8_t vlan_status = 0, vlan_offset = 0;

    if (!(s->mac_reg[RCTL] & E1000_RCTL_EN))
        return;

    if (size > s->rxbuf_size) {
        DBGOUT(RX, "packet too large for buffers (%d > %d)\n", size,
               s->rxbuf_size);
        return;
    }

    if (!receive_filter(s, buf, size))
        return;

    if (vlan_enabled(s) && is_vlan_packet(s, buf)) {
        vlan_special = cpu_to_le16(be16_to_cpup((uint16_t *)(buf + 14)));
        memmove((void *)(buf + 4), buf, 12);
        vlan_status = E1000_RXD_STAT_VP;
        vlan_offset = 4;
        size -= 4;
    }

    rdh_start = s->mac_reg[RDH];
    size += 4; // for the header
    do {
        if (s->mac_reg[RDH] == s->mac_reg[RDT] && s->check_rxov) {
            set_ics(s, 0, E1000_ICS_RXO);
            return;
        }
        base = ((uint64_t)s->mac_reg[RDBAH] << 32) + s->mac_reg[RDBAL] +
               sizeof(desc) * s->mac_reg[RDH];
        cpu_physical_memory_read(base, (void *)&desc, sizeof(desc));
        desc.special = vlan_special;
        desc.status |= (vlan_status | E1000_RXD_STAT_DD);
        if (desc.buffer_addr) {
            cpu_physical_memory_write(le64_to_cpu(desc.buffer_addr),
                                      (void *)(buf + vlan_offset), size);
            desc.length = cpu_to_le16(size);
            desc.status |= E1000_RXD_STAT_EOP|E1000_RXD_STAT_IXSM;
        } else // as per intel docs; skip descriptors with null buf addr
            DBGOUT(RX, "Null RX descriptor!!\n");
        cpu_physical_memory_write(base, (void *)&desc, sizeof(desc));

        if (++s->mac_reg[RDH] * sizeof(desc) >= s->mac_reg[RDLEN])
            s->mac_reg[RDH] = 0;
        s->check_rxov = 1;
        /* see comment in start_xmit; same here */
        if (s->mac_reg[RDH] == rdh_start) {
            DBGOUT(RXERR, "RDH wraparound @%x, RDT %x, RDLEN %x\n",
                   rdh_start, s->mac_reg[RDT], s->mac_reg[RDLEN]);
            set_ics(s, 0, E1000_ICS_RXO);
            return;
        }
    } while (desc.buffer_addr == 0);

    s->mac_reg[GPRC]++;
    s->mac_reg[TPR]++;
    n = s->mac_reg[TORL];
    if ((s->mac_reg[TORL] += size) < n)
        s->mac_reg[TORH]++;

    n = E1000_ICS_RXT0;
    if ((rdt = s->mac_reg[RDT]) < s->mac_reg[RDH])
        rdt += s->mac_reg[RDLEN] / sizeof(desc);
    if (((rdt - s->mac_reg[RDH]) * sizeof(desc)) <= s->mac_reg[RDLEN] >>
        s->rxbuf_min_shift)
        n |= E1000_ICS_RXDMT0;

    set_ics(s, 0, n);
}

static uint32_t
mac_readreg(E1000State *s, int index)
{
    return s->mac_reg[index];
}

static uint32_t
mac_icr_read(E1000State *s, int index)
{
    uint32_t ret = s->mac_reg[ICR];

    DBGOUT(INTERRUPT, "ICR read: %x\n", ret);
    set_interrupt_cause(s, 0, 0);
    return ret;
}

static uint32_t
mac_read_clr4(E1000State *s, int index)
{
    uint32_t ret = s->mac_reg[index];

    s->mac_reg[index] = 0;
    return ret;
}

static uint32_t
mac_read_clr8(E1000State *s, int index)
{
    uint32_t ret = s->mac_reg[index];

    s->mac_reg[index] = 0;
    s->mac_reg[index-1] = 0;
    return ret;
}

static void
mac_writereg(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val;
}

static void
set_rdt(E1000State *s, int index, uint32_t val)
{
    s->check_rxov = 0;
    s->mac_reg[index] = val & 0xffff;
}

static void
set_16bit(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val & 0xffff;
}

static void
set_dlen(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val & 0xfff80;
}

static void
set_tctl(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[index] = val;
    s->mac_reg[TDT] &= 0xffff;
    start_xmit(s);
}

static void
set_icr(E1000State *s, int index, uint32_t val)
{
    DBGOUT(INTERRUPT, "set_icr %x\n", val);
    set_interrupt_cause(s, 0, s->mac_reg[ICR] & ~val);
}

static void
set_imc(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[IMS] &= ~val;
    set_ics(s, 0, 0);
}

static void
set_ims(E1000State *s, int index, uint32_t val)
{
    s->mac_reg[IMS] |= val;
    set_ics(s, 0, 0);
}

#define getreg(x)	[x] = mac_readreg
static uint32_t (*macreg_readops[])(E1000State *, int) = {
    getreg(PBA),	getreg(RCTL),	getreg(TDH),	getreg(TXDCTL),
    getreg(WUFC),	getreg(TDT),	getreg(CTRL),	getreg(LEDCTL),
    getreg(MANC),	getreg(MDIC),	getreg(SWSM),	getreg(STATUS),
    getreg(TORL),	getreg(TOTL),	getreg(IMS),	getreg(TCTL),
    getreg(RDH),	getreg(RDT),	getreg(VET),

    [TOTH] = mac_read_clr8,	[TORH] = mac_read_clr8,	[GPRC] = mac_read_clr4,
    [GPTC] = mac_read_clr4,	[TPR] = mac_read_clr4,	[TPT] = mac_read_clr4,
    [ICR] = mac_icr_read,	[EECD] = get_eecd,	[EERD] = flash_eerd_read,
    [CRCERRS ... MPC] = &mac_readreg,
    [RA ... RA+31] = &mac_readreg,
    [MTA ... MTA+127] = &mac_readreg,
    [VFTA ... VFTA+127] = &mac_readreg,
};
enum { NREADOPS = ARRAY_SIZE(macreg_readops) };

#define putreg(x)	[x] = mac_writereg
static void (*macreg_writeops[])(E1000State *, int, uint32_t) = {
    putreg(PBA),	putreg(EERD),	putreg(SWSM),	putreg(WUFC),
    putreg(TDBAL),	putreg(TDBAH),	putreg(TXDCTL),	putreg(RDBAH),
    putreg(RDBAL),	putreg(LEDCTL), putreg(CTRL),	putreg(VET),
    [TDLEN] = set_dlen,	[RDLEN] = set_dlen,	[TCTL] = set_tctl,
    [TDT] = set_tctl,	[MDIC] = set_mdic,	[ICS] = set_ics,
    [TDH] = set_16bit,	[RDH] = set_16bit,	[RDT] = set_rdt,
    [IMC] = set_imc,	[IMS] = set_ims,	[ICR] = set_icr,
    [EECD] = set_eecd,	[RCTL] = set_rx_control,
    [RA ... RA+31] = &mac_writereg,
    [MTA ... MTA+127] = &mac_writereg,
    [VFTA ... VFTA+127] = &mac_writereg,
};
enum { NWRITEOPS = ARRAY_SIZE(macreg_writeops) };

static void
e1000_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    E1000State *s = opaque;
    unsigned int index = (addr & 0x1ffff) >> 2;

#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    if (index < NWRITEOPS && macreg_writeops[index])
        macreg_writeops[index](s, index, val);
    else if (index < NREADOPS && macreg_readops[index])
        DBGOUT(MMIO, "e1000_mmio_writel RO %x: 0x%04x\n", index<<2, val);
    else
        DBGOUT(UNKNOWN, "MMIO unknown write addr=0x%08x,val=0x%08x\n",
               index<<2, val);
}

static void
e1000_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    // emulate hw without byte enables: no RMW
    e1000_mmio_writel(opaque, addr & ~3,
                      (val & 0xffff) << (8*(addr & 3)));
}

static void
e1000_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    // emulate hw without byte enables: no RMW
    e1000_mmio_writel(opaque, addr & ~3,
                      (val & 0xff) << (8*(addr & 3)));
}

static uint32_t
e1000_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    E1000State *s = opaque;
    unsigned int index = (addr & 0x1ffff) >> 2;

    if (index < NREADOPS && macreg_readops[index])
    {
        uint32_t val = macreg_readops[index](s, index);
#ifdef TARGET_WORDS_BIGENDIAN
        val = bswap32(val);
#endif
        return val;
    }
    DBGOUT(UNKNOWN, "MMIO unknown read addr=0x%08x\n", index<<2);
    return 0;
}

static uint32_t
e1000_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    return ((e1000_mmio_readl(opaque, addr & ~3)) >>
            (8 * (addr & 3))) & 0xff;
}

static uint32_t
e1000_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    return ((e1000_mmio_readl(opaque, addr & ~3)) >>
            (8 * (addr & 3))) & 0xffff;
}

static const int mac_regtosave[] = {
    CTRL,	EECD,	EERD,	GPRC,	GPTC,	ICR,	ICS,	IMC,	IMS,
    LEDCTL,	MANC,	MDIC,	MPC,	PBA,	RCTL,	RDBAH,	RDBAL,	RDH,
    RDLEN,	RDT,	STATUS,	SWSM,	TCTL,	TDBAH,	TDBAL,	TDH,	TDLEN,
    TDT,	TORH,	TORL,	TOTH,	TOTL,	TPR,	TPT,	TXDCTL,	WUFC,
    VET,
};
enum { MAC_NSAVE = ARRAY_SIZE(mac_regtosave) };

static const struct {
    int size;
    int array0;
} mac_regarraystosave[] = { {32, RA}, {128, MTA}, {128, VFTA} };
enum { MAC_NARRAYS = ARRAY_SIZE(mac_regarraystosave) };

static void
nic_save(QEMUFile *f, void *opaque)
{
    E1000State *s = (E1000State *)opaque;
    int i, j;

    pci_device_save(&s->dev, f);
    qemu_put_be32(f, 0);
    qemu_put_be32s(f, &s->rxbuf_size);
    qemu_put_be32s(f, &s->rxbuf_min_shift);
    qemu_put_be32s(f, &s->eecd_state.val_in);
    qemu_put_be16s(f, &s->eecd_state.bitnum_in);
    qemu_put_be16s(f, &s->eecd_state.bitnum_out);
    qemu_put_be16s(f, &s->eecd_state.reading);
    qemu_put_be32s(f, &s->eecd_state.old_eecd);
    qemu_put_8s(f, &s->tx.ipcss);
    qemu_put_8s(f, &s->tx.ipcso);
    qemu_put_be16s(f, &s->tx.ipcse);
    qemu_put_8s(f, &s->tx.tucss);
    qemu_put_8s(f, &s->tx.tucso);
    qemu_put_be16s(f, &s->tx.tucse);
    qemu_put_be32s(f, &s->tx.paylen);
    qemu_put_8s(f, &s->tx.hdr_len);
    qemu_put_be16s(f, &s->tx.mss);
    qemu_put_be16s(f, &s->tx.size);
    qemu_put_be16s(f, &s->tx.tso_frames);
    qemu_put_8s(f, &s->tx.sum_needed);
    qemu_put_s8s(f, &s->tx.ip);
    qemu_put_s8s(f, &s->tx.tcp);
    qemu_put_buffer(f, s->tx.header, sizeof s->tx.header);
    qemu_put_buffer(f, s->tx.data, sizeof s->tx.data);
    for (i = 0; i < 64; i++)
        qemu_put_be16s(f, s->eeprom_data + i);
    for (i = 0; i < 0x20; i++)
        qemu_put_be16s(f, s->phy_reg + i);
    for (i = 0; i < MAC_NSAVE; i++)
        qemu_put_be32s(f, s->mac_reg + mac_regtosave[i]);
    for (i = 0; i < MAC_NARRAYS; i++)
        for (j = 0; j < mac_regarraystosave[i].size; j++)
            qemu_put_be32s(f,
                           s->mac_reg + mac_regarraystosave[i].array0 + j);
}

static int
nic_load(QEMUFile *f, void *opaque, int version_id)
{
    E1000State *s = (E1000State *)opaque;
    int i, j, ret;

    if ((ret = pci_device_load(&s->dev, f)) < 0)
        return ret;
    if (version_id == 1)
        qemu_get_sbe32s(f, &i); /* once some unused instance id */
    qemu_get_be32(f); /* Ignored.  Was mmio_base.  */
    qemu_get_be32s(f, &s->rxbuf_size);
    qemu_get_be32s(f, &s->rxbuf_min_shift);
    qemu_get_be32s(f, &s->eecd_state.val_in);
    qemu_get_be16s(f, &s->eecd_state.bitnum_in);
    qemu_get_be16s(f, &s->eecd_state.bitnum_out);
    qemu_get_be16s(f, &s->eecd_state.reading);
    qemu_get_be32s(f, &s->eecd_state.old_eecd);
    qemu_get_8s(f, &s->tx.ipcss);
    qemu_get_8s(f, &s->tx.ipcso);
    qemu_get_be16s(f, &s->tx.ipcse);
    qemu_get_8s(f, &s->tx.tucss);
    qemu_get_8s(f, &s->tx.tucso);
    qemu_get_be16s(f, &s->tx.tucse);
    qemu_get_be32s(f, &s->tx.paylen);
    qemu_get_8s(f, &s->tx.hdr_len);
    qemu_get_be16s(f, &s->tx.mss);
    qemu_get_be16s(f, &s->tx.size);
    qemu_get_be16s(f, &s->tx.tso_frames);
    qemu_get_8s(f, &s->tx.sum_needed);
    qemu_get_s8s(f, &s->tx.ip);
    qemu_get_s8s(f, &s->tx.tcp);
    qemu_get_buffer(f, s->tx.header, sizeof s->tx.header);
    qemu_get_buffer(f, s->tx.data, sizeof s->tx.data);
    for (i = 0; i < 64; i++)
        qemu_get_be16s(f, s->eeprom_data + i);
    for (i = 0; i < 0x20; i++)
        qemu_get_be16s(f, s->phy_reg + i);
    for (i = 0; i < MAC_NSAVE; i++)
        qemu_get_be32s(f, s->mac_reg + mac_regtosave[i]);
    for (i = 0; i < MAC_NARRAYS; i++)
        for (j = 0; j < mac_regarraystosave[i].size; j++)
            qemu_get_be32s(f,
                           s->mac_reg + mac_regarraystosave[i].array0 + j);
    return 0;
}

static const uint16_t e1000_eeprom_template[64] = {
    0x0000, 0x0000, 0x0000, 0x0000,      0xffff, 0x0000,      0x0000, 0x0000,
    0x3000, 0x1000, 0x6403, E1000_DEVID, 0x8086, E1000_DEVID, 0x8086, 0x3040,
    0x0008, 0x2000, 0x7e14, 0x0048,      0x1000, 0x00d8,      0x0000, 0x2700,
    0x6cc9, 0x3150, 0x0722, 0x040b,      0x0984, 0x0000,      0xc000, 0x0706,
    0x1008, 0x0000, 0x0f04, 0x7fff,      0x4d01, 0xffff,      0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff,      0xffff, 0xffff,      0xffff, 0xffff,
    0x0100, 0x4000, 0x121c, 0xffff,      0xffff, 0xffff,      0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff,      0xffff, 0xffff,      0xffff, 0x0000,
};

static const uint16_t phy_reg_init[] = {
    [PHY_CTRL] = 0x1140,			[PHY_STATUS] = 0x796d, // link initially up
    [PHY_ID1] = 0x141,				[PHY_ID2] = PHY_ID2_INIT,
    [PHY_1000T_CTRL] = 0x0e00,			[M88E1000_PHY_SPEC_CTRL] = 0x360,
    [M88E1000_EXT_PHY_SPEC_CTRL] = 0x0d60,	[PHY_AUTONEG_ADV] = 0xde1,
    [PHY_LP_ABILITY] = 0x1e0,			[PHY_1000T_STATUS] = 0x3c00,
    [M88E1000_PHY_SPEC_STATUS] = 0xac00,
};

static const uint32_t mac_reg_init[] = {
    [PBA] =     0x00100030,
    [LEDCTL] =  0x602,
    [CTRL] =    E1000_CTRL_SWDPIN2 | E1000_CTRL_SWDPIN0 |
                E1000_CTRL_SPD_1000 | E1000_CTRL_SLU,
    [STATUS] =  0x80000000 | E1000_STATUS_GIO_MASTER_ENABLE |
                E1000_STATUS_ASDV | E1000_STATUS_MTXCKOK |
                E1000_STATUS_SPEED_1000 | E1000_STATUS_FD |
                E1000_STATUS_LU,
    [MANC] =    E1000_MANC_EN_MNG2HOST | E1000_MANC_RCV_TCO_EN |
                E1000_MANC_ARP_EN | E1000_MANC_0298_EN |
                E1000_MANC_RMCP_EN,
};

/* PCI interface */

static CPUWriteMemoryFunc *e1000_mmio_write[] = {
    e1000_mmio_writeb,	e1000_mmio_writew,	e1000_mmio_writel
};

static CPUReadMemoryFunc *e1000_mmio_read[] = {
    e1000_mmio_readb,	e1000_mmio_readw,	e1000_mmio_readl
};

static void
e1000_mmio_map(PCIDevice *pci_dev, int region_num,
                uint32_t addr, uint32_t size, int type)
{
    E1000State *d = (E1000State *)pci_dev;
    int i;
    const uint32_t excluded_regs[] = {
        E1000_MDIC, E1000_ICR, E1000_ICS, E1000_IMS,
        E1000_IMC, E1000_TCTL, E1000_TDT, PNPMMIO_SIZE
    };


    DBGOUT(MMIO, "e1000_mmio_map addr=0x%08x 0x%08x\n", addr, size);

    cpu_register_physical_memory(addr, PNPMMIO_SIZE, d->mmio_index);
    qemu_register_coalesced_mmio(addr, excluded_regs[0]);

    for (i = 0; excluded_regs[i] != PNPMMIO_SIZE; i++)
        qemu_register_coalesced_mmio(addr + excluded_regs[i] + 4,
                                     excluded_regs[i + 1] -
                                     excluded_regs[i] - 4);
}

static void
e1000_cleanup(VLANClientState *vc)
{
    E1000State *d = vc->opaque;

    unregister_savevm("e1000", d);
}

static int
pci_e1000_uninit(PCIDevice *dev)
{
    E1000State *d = (E1000State *) dev;

    cpu_unregister_io_memory(d->mmio_index);

    return 0;
}

PCIDevice *
pci_e1000_init(PCIBus *bus, NICInfo *nd, int devfn)
{
    E1000State *d;
    uint8_t *pci_conf;
    uint16_t checksum = 0;
    static const char info_str[] = "e1000";
    int i;

    d = (E1000State *)pci_register_device(bus, "e1000",
                sizeof(E1000State), devfn, NULL, NULL);

    pci_conf = d->dev.config;
    memset(pci_conf, 0, 256);

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, E1000_DEVID);
    *(uint16_t *)(pci_conf+0x04) = cpu_to_le16(0x0407);
    *(uint16_t *)(pci_conf+0x06) = cpu_to_le16(0x0010);
    pci_conf[0x08] = 0x03;
    pci_config_set_class(pci_conf, PCI_CLASS_NETWORK_ETHERNET);
    pci_conf[0x0c] = 0x10;

    pci_conf[0x3d] = 1; // interrupt pin 0

    d->mmio_index = cpu_register_io_memory(0, e1000_mmio_read,
            e1000_mmio_write, d);

    pci_register_io_region((PCIDevice *)d, 0, PNPMMIO_SIZE,
                           PCI_ADDRESS_SPACE_MEM, e1000_mmio_map);

    pci_register_io_region((PCIDevice *)d, 1, IOPORT_SIZE,
                           PCI_ADDRESS_SPACE_IO, ioport_map);

    memmove(d->eeprom_data, e1000_eeprom_template,
        sizeof e1000_eeprom_template);
    for (i = 0; i < 3; i++)
        d->eeprom_data[i] = (nd->macaddr[2*i+1]<<8) | nd->macaddr[2*i];
    for (i = 0; i < EEPROM_CHECKSUM_REG; i++)
        checksum += d->eeprom_data[i];
    checksum = (uint16_t) EEPROM_SUM - checksum;
    d->eeprom_data[EEPROM_CHECKSUM_REG] = checksum;

    memset(d->phy_reg, 0, sizeof d->phy_reg);
    memmove(d->phy_reg, phy_reg_init, sizeof phy_reg_init);
    memset(d->mac_reg, 0, sizeof d->mac_reg);
    memmove(d->mac_reg, mac_reg_init, sizeof mac_reg_init);
    d->rxbuf_min_shift = 1;
    memset(&d->tx, 0, sizeof d->tx);

    d->vc = qemu_new_vlan_client(nd->vlan, nd->model, nd->name,
                                 e1000_receive, e1000_can_receive,
                                 e1000_cleanup, d);
    d->vc->link_status_changed = e1000_set_link_status;

    qemu_format_nic_info_str(d->vc, nd->macaddr);

    register_savevm(info_str, -1, 2, nic_save, nic_load, d);
    d->dev.unregister = pci_e1000_uninit;

    return (PCIDevice *)d;
}
