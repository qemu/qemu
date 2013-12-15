/* hw/dm9000.c
 *
 * DM9000 Ethernet interface
 *
 * Copyright 2006, 2008 Daniel Silverstone and Vincent Sanders
 * Copyright 2010, 2012 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <string.h>
#include "qemu-common.h"
#include "irq.h"
#include "net/net.h"
#include "sysbus.h"

/* Comment this out if you don't want register debug on stderr */
#if 0
# define DM9000_DEBUG
#endif

#ifdef DM9000_DEBUG
# define DM9000_DBF(X...) do { fprintf(stderr, X); } while(0)
#else
# define DM9000_DBF(X...) do {} while (0)
#endif

#define DM9000_REG_NCR 0x00
#define DM9000_REG_NSR 0x01
#define DM9000_REG_TCR 0x02
#define DM9000_REG_TSR1 0x03
#define DM9000_REG_TSR2 0x04
#define DM9000_REG_RCR 0x05
#define DM9000_REG_RSR 0x06
#define DM9000_REG_ROCR 0x07
#define DM9000_REG_BPTR 0x08
#define DM9000_REG_FCTR 0x09
#define DM9000_REG_FCR 0x0A
#define DM9000_REG_EPCR 0x0B
#define DM9000_REG_EPAR 0x0C
#define DM9000_REG_EPDRL 0x0D
#define DM9000_REG_EPDRH 0x0E
#define DM9000_REG_WCR 0x0F
#define DM9000_REG_PAR0 0x10
#define DM9000_REG_PAR1 0x11
#define DM9000_REG_PAR2 0x12
#define DM9000_REG_PAR3 0x13
#define DM9000_REG_PAR4 0x14
#define DM9000_REG_PAR5 0x15
#define DM9000_REG_MAR0 0x16
#define DM9000_REG_MAR1 0x17
#define DM9000_REG_MAR2 0x18
#define DM9000_REG_MAR3 0x19
#define DM9000_REG_MAR4 0x1A
#define DM9000_REG_MAR5 0x1B
#define DM9000_REG_MAR6 0x1C
#define DM9000_REG_MAR7 0x1D
#define DM9000_REG_GPCR 0x1E
#define DM9000_REG_GPR 0x1F
#define DM9000_REG_TRPAL 0x22
#define DM9000_REG_TRPAH 0x23
#define DM9000_REG_RWPAL 0x24
#define DM9000_REG_RWPAH 0x25
#define DM9000_REG_VIDL 0x28
#define DM9000_REG_VIDH 0x29
#define DM9000_REG_PIDL 0x2A
#define DM9000_REG_PIDH 0x2B
#define DM9000_REG_CHIPR 0x2C
#define DM9000_REG_SMCR 0x2F
#define DM9000_REG_MRCMDX 0xF0
#define DM9000_REG_MRCMD 0xF2
#define DM9000_REG_MRRL 0xF4
#define DM9000_REG_MRRH 0xF5
#define DM9000_REG_MWCMDX 0xF6
#define DM9000_REG_MWCMD 0xF8
#define DM9000_REG_MWRL 0xFA
#define DM9000_REG_MWRH 0xFB
#define DM9000_REG_TXPLL 0xFC
#define DM9000_REG_TXPLH 0xFD
#define DM9000_REG_ISR 0xFE
#define DM9000_REG_IMR 0xFF

#define DM9000_NCR_RESET 0x01
#define DM9000_NSR_TX1END 0x04
#define DM9000_NSR_TX2END 0x08
#define DM9000_TCR_TXREQ 0x01

#define DM9000_IMR_AUTOWRAP 0x80

#define DM9000_MII_READ 0x0C
#define DM9000_MII_WRITE 0x0A

#define DM9000_MII_REG_BMCR 0x00
#define DM9000_MII_REG_STATUS 0x01
#define DM9000_MII_REG_PHYID1 0x02
#define DM9000_MII_REG_PHYID2 0x03
#define DM9000_MII_REG_ANAR 0x04
#define DM9000_MII_REG_ANLPAR 0x05
#define DM9000_MII_REG_ANER 0x06
#define DM9000_MII_REG_DSCR 0x10
#define DM9000_MII_REG_DSCSR 0x11
#define DM9000_MII_REG_10BTCSR 0x12


typedef struct {
    SysBusDevice busdev;
    MemoryRegion mmio;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    uint8_t multihash[8]; /* multicast hash table */
    uint8_t address; /* The internal magical address */
    uint8_t packet_buffer[16 * 1024];
    uint16_t dm9k_mrr, dm9k_mwr; /* Read and write address registers */
    uint16_t dm9k_txpl; /* TX packet length */
    uint16_t dm9k_trpa, dm9k_rwpa; /* TX Read ptr address, RX write ptr address */
    uint8_t dm9k_imr;   /* Interrupt mask register */
    uint8_t dm9k_isr;   /* Interrupt status register */
    uint8_t dm9k_ncr, dm9k_nsr; /* Network control register, network status register */
    uint8_t dm9k_wcr; /* Wakeup control */
    uint8_t dm9k_tcr; /* Transmission control register */
    uint8_t packet_copy_buffer[3 * 1024]; /* packet copy buffer */
    unsigned int packet_index:1; /* 0 == packet I, 1 == packet II */

    /* Internal MII PHY state */
    uint8_t dm9k_epcr; /* EEPROM/PHY control register */
    uint8_t dm9k_epar; /* EEPROM/PHY address register */
    uint16_t dm9k_epdr; /* EEPROM/PHY data register */
    /* MII Regs */
    uint16_t dm9k_mii_bmcr;
    uint16_t dm9k_mii_anar;
    uint16_t dm9k_mii_dscr;
} dm9000_state;

static void dm9000_raise_irq(dm9000_state *state)
{
    int level = ((state->dm9k_isr & state->dm9k_imr) & 0x03) != 0;
    DM9000_DBF("DM9000: Set IRQ level %d\n", level);
    qemu_set_irq(state->irq, level);
}

static void dm9000_soft_reset_mii(dm9000_state *state)
{
    state->dm9k_mii_bmcr = 0x3100; /* 100Mbps, AUTONEG, FULL DUPLEX */
    state->dm9k_mii_anar = 0x01E1;
    state->dm9k_mii_dscr = 0x0410;
}

static void dm9000_soft_reset(dm9000_state *state)
{
    DM9000_DBF("DM9000: Soft Reset\n");
    state->dm9k_mrr = state->dm9k_mwr = state->dm9k_txpl = state->dm9k_trpa = 0x0000;
    state->dm9k_rwpa = 0x0C04;
    state->dm9k_imr = 0;
    state->dm9k_isr = 0; /* 16 bit mode, no interrupts asserted */
    state->dm9k_tcr = 0;
    state->packet_index = 0;
    memset(state->packet_buffer, 0, sizeof(state->packet_buffer));
    memset(state->packet_copy_buffer, 0, sizeof(state->packet_copy_buffer));
    /* These registers have some bits "unaffected by software reset" */
    /* Clear the reset bits */
    state->dm9k_ncr &= 0xA0;
    state->dm9k_nsr &= 0xD0;
    /* Claim full duplex */
    state->dm9k_ncr |= 1<<3;
    /* Set link status to 1 */
    state->dm9k_nsr |= 1<<6;
    /* dm9k_wcr is unaffected or reserved, never reset */
    /* MII control regs */
    state->dm9k_epcr = 0x00;
    state->dm9k_epar = 0x40;
    /* reset the MII */
    dm9000_soft_reset_mii(state);
    /* Clear any potentially pending IRQ */
    qemu_irq_lower(state->irq);
}

static void dm9000_hard_reset(dm9000_state *state)
{
    state->dm9k_ncr = 0x00;
    state->dm9k_nsr = 0x00;
    state->dm9k_wcr = 0x00;
    dm9000_soft_reset(state);
}

static void dm9000_do_transmit(dm9000_state *state)
{
    uint16_t idx, cnt, tptr;
    idx = state->dm9k_trpa;
    cnt = state->dm9k_txpl;
    tptr = 0;
    if (cnt > 3*1024 ) {
        cnt = 3*1024; /* HARD CAP AT 3KiB */
    }
    DM9000_DBF("TX_Packet: %d bytes from %04x\n", cnt, idx);
    while(cnt--) {
        assert(idx < sizeof(state->packet_buffer));
        assert(tptr < sizeof(state->packet_copy_buffer));
        state->packet_copy_buffer[tptr++] = state->packet_buffer[idx++];
        if (idx == 0x0C00) {
            idx = 0;
        }
    }
    /* DM9KNOTE: Assumes 16bit wiring */
    idx = (idx+1) & ~1; /* Round up to nearest 16bit boundary */
    if( idx == 0x0C00 ) idx = 0;
    state->dm9k_trpa = idx;
    /* We have the copy buffer, now we do the transmit */
    qemu_send_packet(qemu_get_queue(state->nic), state->packet_copy_buffer,
                     state->dm9k_txpl);
    /* Clear the "please xmit" bit */
    state->dm9k_tcr &= ~DM9000_TCR_TXREQ;
    /* Set the TXEND bit */
    state->dm9k_nsr |= 1<<(2+state->packet_index);
    DM9000_DBF("TX: NSR=%02x PI=%d\n", state->dm9k_nsr, state->packet_index);
    /* Claim a TX complete IRQ */
    state->dm9k_isr |= 0x02; /* Packet transmitted latch */
    /* And flip the next-packet bit */
    state->packet_index = !state->packet_index;
    dm9000_raise_irq(state);
}

static void dm9000_mii_read(dm9000_state *state)
{
    int mii_reg = (state->dm9k_epar) & 0x3f;
    uint16_t ret = 0;
    switch(mii_reg) {
    case DM9000_MII_REG_BMCR:
        ret = state->dm9k_mii_bmcr;
        break;
    case DM9000_MII_REG_STATUS:
        ret = 0x782D; /* No 100/T4, Can 100/FD, Can 100/HD, Can 10/FD, Can 10/HD,
                       * No Preamble suppression, Autoneg complete, No remote fault,
                       * Can autoneg, link up, no jabber, extended capability */
        break;
    case DM9000_MII_REG_PHYID1:
        ret = 0x0181;
        break;
    case DM9000_MII_REG_PHYID2:
        ret = 0xB8C0;
        break;
    case DM9000_MII_REG_ANAR:
        ret = state->dm9k_mii_anar;
        break;
    case DM9000_MII_REG_ANLPAR:
        ret = 0x0400;
        break;
    case DM9000_MII_REG_ANER:
        ret = 0x0001;
        break;
    case DM9000_MII_REG_DSCR:
        ret = state->dm9k_mii_dscr;
        break;
    case DM9000_MII_REG_DSCSR:
        ret = 0xF008;
        break;
    case DM9000_MII_REG_10BTCSR:
        ret = 0x7800;
    }
    state->dm9k_epdr = ret;
    DM9000_DBF("DM9000:MIIPHY: Read of MII reg %d gives %04x\n", mii_reg, state->dm9k_epdr);
}

static void dm9000_mii_write(dm9000_state *state)
{
    int mii_reg = (state->dm9k_epar) & 0x3f;
    DM9000_DBF("DM9000:MIIPHY: Write of MII reg %d value %04x\n", mii_reg, state->dm9k_epdr);
    switch(mii_reg) {
    case DM9000_MII_REG_BMCR:
        state->dm9k_mii_bmcr = (state->dm9k_epdr &~0x8000);
        if( state->dm9k_epdr & 0x8000 ) dm9000_soft_reset_mii(state);
        break;
    case DM9000_MII_REG_ANAR:
        state->dm9k_mii_anar = state->dm9k_epdr;
        break;
    case DM9000_MII_REG_DSCR:
        state->dm9k_mii_dscr = state->dm9k_epdr & ~0x0008;
        break;
    }
}

static void dm9000_write(void *opaque, hwaddr address,
                         uint64_t value, unsigned size)
{
    dm9000_state *state = opaque;
#ifdef DM9000_DEBUG
    int suppress_debug = 0;
#endif

    if (address == 0x00) {
        if( (value != DM9000_REG_MRCMD) &&
            (value != DM9000_REG_MWCMD) )
            DM9000_DBF("DM9000: Address set to 0x%02x\n", value);
        state->address = value;
        return;
    }

    if (address != 0x40) {
        DM9000_DBF("DM9000: Write to location which is neither data nor address port: " TARGET_FMT_plx "\n", address);
    }

    switch(state->address) {
    case DM9000_REG_NCR:
        state->dm9k_ncr = value & 0xDF;
        if (state->dm9k_ncr & DM9000_NCR_RESET)
            dm9000_soft_reset(state);
        break;
    case DM9000_REG_NSR:
        state->dm9k_nsr &= ~(value & 0x2C);
        break;
    case DM9000_REG_TCR:
        state->dm9k_tcr = value & 0xFF;
        if( value & DM9000_TCR_TXREQ ) dm9000_do_transmit(state);
        break;
    case DM9000_REG_EPCR:
        state->dm9k_epcr = value & 0xFF;
        if( value & DM9000_MII_READ )
            dm9000_mii_read(state);
        else if( value & DM9000_MII_WRITE )
            dm9000_mii_write(state);
        break;
    case DM9000_REG_EPAR:
        state->dm9k_epar = value & 0xFF;
        break;
    case DM9000_REG_EPDRL:
        state->dm9k_epdr &= 0xFF00;
        state->dm9k_epdr |= value & 0xFF;
        break;
    case DM9000_REG_EPDRH:
        state->dm9k_epdr &= 0xFF;
        state->dm9k_epdr |= (value & 0xFF) << 8;
        break;
    case DM9000_REG_PAR0:
    case DM9000_REG_PAR1:
    case DM9000_REG_PAR2:
    case DM9000_REG_PAR3:
    case DM9000_REG_PAR4:
    case DM9000_REG_PAR5:
        state->conf.macaddr.a[state->address - DM9000_REG_PAR0] = value & 0xFF;
        break;
    case DM9000_REG_MAR0:
    case DM9000_REG_MAR1:
    case DM9000_REG_MAR2:
    case DM9000_REG_MAR3:
    case DM9000_REG_MAR4:
    case DM9000_REG_MAR5:
    case DM9000_REG_MAR6:
    case DM9000_REG_MAR7:
        /* multicast hash setup */
        state->multihash[state->address - DM9000_REG_MAR0] = value & 0xFF;
        break;
    case DM9000_REG_MRRL:
        state->dm9k_mrr &= 0xFF00;
        state->dm9k_mrr |= value & 0xFF;
        break;
    case DM9000_REG_MRRH:
        state->dm9k_mrr &= 0xFF;
        state->dm9k_mrr |= (value & 0xFF) << 8;
        break;
    case DM9000_REG_MWCMDX:
    case DM9000_REG_MWCMD:
        /* DM9KNOTE: This assumes a 16bit wide wiring */
        assert(state->dm9k_mwr + 1 < sizeof(state->packet_buffer));
        state->packet_buffer[state->dm9k_mwr] = value & 0xFF;
        state->packet_buffer[state->dm9k_mwr+1] = (value >> 8) & 0xFF;
        if( state->address == DM9000_REG_MWCMD ) {
            state->dm9k_mwr += 2;
            if( state->dm9k_imr & DM9000_IMR_AUTOWRAP ) {
                if( state->dm9k_mwr >= 0x0C00 ) {
                    state->dm9k_mwr -= 0x0C00;
                }
            }
        }
#ifdef DM9000_DEBUG
        suppress_debug = 1;
#endif
        break;
    case DM9000_REG_MWRL:
        state->dm9k_mwr &= 0xFF00;
        state->dm9k_mwr |= value & 0xFF;
        break;
    case DM9000_REG_MWRH:
        state->dm9k_mwr &= 0xFF;
        state->dm9k_mwr |= (value & 0xFF) << 8;
        break;
    case DM9000_REG_TXPLL:
        state->dm9k_txpl &= 0xFF00;
        state->dm9k_txpl |= value & 0xFF;
        break;
    case DM9000_REG_TXPLH:
        state->dm9k_txpl &= 0xFF;
        state->dm9k_txpl |= (value & 0xFF) << 8;
        break;
    case DM9000_REG_ISR:
        state->dm9k_isr &= ~(value & 0x0F);
        dm9000_raise_irq(state);
        break;
    case DM9000_REG_IMR:
        if( !(state->dm9k_imr & DM9000_IMR_AUTOWRAP) &&
            (value & DM9000_IMR_AUTOWRAP) ) {
            state->dm9k_mrr = 0x0C00 | (state->dm9k_mrr & 0xFF);
        }
        state->dm9k_imr = value & 0xFF;
        dm9000_raise_irq(state);
        break;
    }
#ifdef DM9000_DEBUG
    if(!suppress_debug) {
        DM9000_DBF("DM9000: Write value %04x\n", value);
    }
#endif
}

static uint64_t dm9000_read(void *opaque, hwaddr address,
                            unsigned size)
{
    dm9000_state *state = opaque;
    uint32_t ret = 0;
#ifdef DM9000_DEBUG
    int suppress_debug = 0;
#endif

    if (address == 0x00) {
        return state->address;
    }

    if (address != 0x40) {
        DM9000_DBF("DM9000: Read from location which is neither data nor address port: " TARGET_FMT_plx "\n", address);
    }

    switch(state->address) {
    case DM9000_REG_NCR:
        ret = state->dm9k_ncr;
        break;
    case DM9000_REG_NSR:
        ret = state->dm9k_nsr;
        /* Note, TX1END and TX2END are *CLEAR ON READ* */
        state->dm9k_nsr &= ~(DM9000_NSR_TX1END | DM9000_NSR_TX2END);
        break;
    case DM9000_REG_TCR:
        ret = state->dm9k_tcr;
        break;
    case DM9000_REG_TSR1:
    case DM9000_REG_TSR2:
        ret = 0x00; /* No error, yay! */
        break;
    case DM9000_REG_EPCR:
        ret = state->dm9k_epcr;
        break;
    case DM9000_REG_EPAR:
        ret = state->dm9k_epar;
        break;
    case DM9000_REG_EPDRL:
        ret = state->dm9k_epdr & 0xFF;
        break;
    case DM9000_REG_EPDRH:
        ret = (state->dm9k_epdr >> 8) & 0xFF;
        break;
    case DM9000_REG_PAR0:
    case DM9000_REG_PAR1:
    case DM9000_REG_PAR2:
    case DM9000_REG_PAR3:
    case DM9000_REG_PAR4:
    case DM9000_REG_PAR5:
        ret = state->conf.macaddr.a[state->address - DM9000_REG_PAR0];
        break;
    case DM9000_REG_MAR0:
    case DM9000_REG_MAR1:
    case DM9000_REG_MAR2:
    case DM9000_REG_MAR3:
    case DM9000_REG_MAR4:
    case DM9000_REG_MAR5:
    case DM9000_REG_MAR6:
    case DM9000_REG_MAR7:
        /* multicast hash  */
        ret = state->multihash[state->address - DM9000_REG_MAR0];
        break;
    case DM9000_REG_TRPAL:
        ret = state->dm9k_trpa & 0xFF;
        break;
    case DM9000_REG_TRPAH:
        ret = state->dm9k_trpa >> 8;
        break;
    case DM9000_REG_RWPAL:
        ret = state->dm9k_rwpa & 0xFF;
        break;
    case DM9000_REG_RWPAH:
        ret = state->dm9k_rwpa >> 8;
        break;
    case DM9000_REG_VIDL:
        ret = 0x46;
        break;
    case DM9000_REG_VIDH:
        ret = 0x0A;
        break;
    case DM9000_REG_PIDL:
        ret = 0x00;
        break;
    case DM9000_REG_PIDH:
        ret = 0x90;
        break;
    case DM9000_REG_CHIPR:
        ret = 0x00;
        break;
    case DM9000_REG_MRCMDX:
    case DM9000_REG_MRCMD:
        /* DM9KNOTE: This assumes a 16bit wide wiring */
        assert(state->dm9k_mrr + 1 < sizeof(state->packet_buffer));
        ret = state->packet_buffer[state->dm9k_mrr];
        ret |= state->packet_buffer[state->dm9k_mrr+1] << 8;
        if( state->address == DM9000_REG_MRCMD ) {
            state->dm9k_mrr += 2;
            if (state->dm9k_mrr >= (16*1024)) {
                state->dm9k_mrr -= (16*1024);
            }
            if (state->dm9k_imr & DM9000_IMR_AUTOWRAP) {
                if (state->dm9k_mrr < 0x0C00) {
                    state->dm9k_mrr += 0x0C00;
                }
            }
        }
#ifdef DM9000_DEBUG
        if (state->address==DM9000_REG_MRCMD) {
            suppress_debug = 1;
        }
#endif
        break;
    case DM9000_REG_MRRL:
        ret = state->dm9k_mrr & 0xFF;
        break;
    case DM9000_REG_MRRH:
        ret = state->dm9k_mrr >> 8;
        break;
    case DM9000_REG_MWRL:
        ret = state->dm9k_mwr & 0xFF;
        break;
    case DM9000_REG_MWRH:
        ret = state->dm9k_mwr >> 8;
        break;
    case DM9000_REG_TXPLL:
        ret = state->dm9k_txpl & 0xFF;
        break;
    case DM9000_REG_TXPLH:
        ret = state->dm9k_txpl >> 8;
        break;
    case DM9000_REG_ISR:
        ret = state->dm9k_isr;
        break;
    case DM9000_REG_IMR:
        ret = state->dm9k_imr;
        break;
    default:
        ret = 0;
    }

#ifdef DM9000_DEBUG
    if (!suppress_debug) {
        DM9000_DBF("DM9000: Read gives: %04x\n", ret);
    }
#endif
    return ret;
}



static int dm9000_can_receive(NetClientState *nc)
{
    dm9000_state *state = qemu_get_nic_opaque(nc);
    uint16_t rx_space;
    if( state->dm9k_rwpa < state->dm9k_mrr )
        rx_space = state->dm9k_mrr - state->dm9k_rwpa;
    else
        rx_space = (13*1024) - (state->dm9k_rwpa - state->dm9k_mrr);
    DM9000_DBF("DM9000:RX_Packet: Asked about RX, rwpa=%d mrr=%d => space is %d bytes\n",
               state->dm9k_rwpa, state->dm9k_mrr, rx_space);
    if (rx_space > 2048) {
        return 1;
    }
    return 0;
}

static ssize_t
dm9000_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    dm9000_state *state = qemu_get_nic_opaque(nc);
    uint16_t rxptr = state->dm9k_rwpa;
    uint8_t magic_padding = 4;
    if( size > 2048 ) {
        return -1; /* La La La, I can't hear you */
    }
    /* Fill out the magical header structure */
    DM9000_DBF("DM9000:RX_Packet: %zu bytes into buffer at %04x\n", size, rxptr);
    if( size < 64 ) magic_padding += (64 - size);
    DM9000_DBF("DM9000:RX_Packet: Magical padding is %d bytes\n", magic_padding);
    size += magic_padding; /* The magical CRC word */
    assert(state->dm9k_rwpa >= 4 && state->dm9k_rwpa - 1 < sizeof(state->packet_buffer));
    state->packet_buffer[state->dm9k_rwpa-4] = 0x01; /* Packet read */
    state->packet_buffer[state->dm9k_rwpa-3] = 0x00; /* Status OK */
    state->packet_buffer[state->dm9k_rwpa-2] = size & 0xFF; /* Size LOW */
    state->packet_buffer[state->dm9k_rwpa-1] = (size & 0xFF00)>>8; /* Size HIGH */
    size += 4; /* The magical next header (which we zero for fun) */
    while(size--) {
        assert(rxptr < sizeof(state->packet_buffer));
        if (size > (magic_padding + 3)) {
            state->packet_buffer[rxptr++] = *buf++;
        } else {
            state->packet_buffer[rxptr++] = 0x00; /* Clear to the next header */
        }
        /* DM9KNOTE: Assumes 16 bit wired config */
        if (size == 4) {
            rxptr = (rxptr+1) & ~1; /* At end of packet, realign */
        }
        if (rxptr >= (16*1024)) {
            rxptr -= (16*1024);
        }
        if (rxptr < 0x0C00) {
            rxptr += 0x0C00;
        }
    }
    state->dm9k_rwpa = rxptr;
    state->dm9k_isr |= 0x01; /* RX interrupt, yay */
    dm9000_raise_irq(state);
    return size;
}


static const MemoryRegionOps dm9000_ops = {
    .read = dm9000_read,
    .write = dm9000_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static NetClientInfo net_dm9000_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = dm9000_can_receive,
    .receive = dm9000_receive,
    //~ .cleanup = dm9000_cleanup,
};

/* Initialise a dm9000 ethernet controller.
 * The dm9k has a single 16bit wide address and data port through which all
 * operations are multiplexed, there is a single IRQ.
 */
static int dm9000_init(SysBusDevice *dev)
{
    dm9000_state *s = FROM_SYSBUS(dm9000_state, dev);
    sysbus_init_irq(dev, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_dm9000_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->qdev.id, s);
    memory_region_init_io(&s->mmio, &dm9000_ops, s, "dm9000", 0x1000);
    sysbus_init_mmio(dev, &s->mmio);
    dm9000_hard_reset(s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    return 0;
}

static const VMStateDescription dm9000_vmsd = {
    .name = "dm9000",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        // TODO.
        //~ VMSTATE_UINT32(smir, mv88w8618_eth_state),
        VMSTATE_UINT8(dm9k_imr, dm9000_state),
        VMSTATE_UINT8(dm9k_isr, dm9000_state),
        //~ VMSTATE_UINT32(vlan_header, mv88w8618_eth_state),
        //~ VMSTATE_UINT32_ARRAY(tx_queue, mv88w8618_eth_state, 2),
        //~ VMSTATE_UINT32_ARRAY(rx_queue, mv88w8618_eth_state, 4),
        //~ VMSTATE_UINT32_ARRAY(frx_queue, mv88w8618_eth_state, 4),
        //~ VMSTATE_UINT32_ARRAY(cur_rx, mv88w8618_eth_state, 4),
        VMSTATE_END_OF_LIST()
    }
};

static Property dm9000_properties[] = {
    DEFINE_NIC_PROPERTIES(dm9000_state, conf),
    DEFINE_PROP_END_OF_LIST()
};

static void dm9000_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->vmsd = &dm9000_vmsd;
    dc->props = dm9000_properties;
    k->init = dm9000_init;
}

static const TypeInfo dm9000_info = {
    .name = "dm9000",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(dm9000_state),
    .class_init = dm9000_class_init
};

static void dm9000_register_types(void)
{
    type_register_static(&dm9000_info);
}

type_init(dm9000_register_types)
