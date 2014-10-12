/*
 * QEMU Ethernet Physical Layer (PHY) support
 *
 * Copyright (c) 2007 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* This code emulates a National Semiconductor DP83840A PHY. */

#define DEBUG_PHY

typedef enum {
    PHY_BMCR = 0x00,    /* Basic Mode Control Register */
    PHY_BMSR = 0x01,    /* Basic Mode Status */
    PHY_PHYIDR1 = 0x02, /* PHY Identifier 1 */
    PHY_PHYIDR2 = 0x03, /* PHY Identifier 2 */
    PHY_ANAR = 0x04,    /* Auto-Negotiation Advertisement */
    PHY_ANLPAR = 0x05,  /* Auto-Negotiation Link Partner Ability */
    PHY_ANER = 0x06,    /* Auto-Negotiation Expansion */
    PHY_DCR = 0x12,     /* Disconnect Counter */
    PHY_FCSCR = 0x13,   /* False Carrier Sense Counter */
    PHY_RECR = 0x15,    /* Receive Error Counter */
    PHY_SRR = 0x16,     /* Silicon Revision */
    PHY_PCR = 0x17,     /* PCS Sublayer Configuration */
    PHY_LBREMR = 0x18,  /* Loopback, Bypass and Receiver Error Mask */
    PHY_PAR = 0x19,     /* PHY Address */
    PHY_10BTSR = 0x1b,  /* 10Base-T Status */
    PHY_10BTCR = 0x1c,  /* 10Base-T Configuration */
} phy_register_t;

typedef enum {
    PHY_RESET = BIT(15),
    PHY_LOOP = BIT(14),
    PHY_100 = BIT(13),
    AUTO_NEGOTIATE_EN = BIT(12),
    PHY_PDOWN = BIT(11),
    PHY_ISOLATE = BIT(10),
    RENEGOTIATE = BIT(9),
    PHY_FD = BIT(8),
    PHY_COLLISION_TEST = BIT(7),
} phy_bmcr_bits;

typedef enum {
    PHY_100BASE_T4 = BIT(15),
    PHY_100BASE_TX_FD = BIT(14),
    PHY_100BASE_TX_HD = BIT(13),
    PHY_10BASE_T_FD = BIT(12),
    PHY_10BASE_T_HD = BIT(11),
    NWAY_COMPLETE = BIT(5),
    NWAY_CAPABLE = BIT(3),
    PHY_LINKED = BIT(2),
    PHY_EXTENDED_CAPABILITY = BIT(0),
} phy_bmsr_bits;

typedef enum {
    PHY_Identifier_1 = 2,
} phy_phyidr1_bits;

typedef enum {
    PHY_Identifier_2 = 3,
} phy_phyidr2_bits;

typedef enum {
    NWAY_FD100 = BIT(8),
    NWAY_HD100 = BIT(7),
    NWAY_FD10 = BIT(6),
    NWAY_HD10 = BIT(5),
    NWAY_SEL = BITS(4, 0),
    NWAY_AUTO = BIT(0),
} phy_anar_bits;

#define PHY_AUTO_NEG_EXPANSION           6
#define PHY_GENERIC_CONFIG_REG           0x10
  #define PHY_IFSEL                      (3<<14)
  #define PHY_LBKMD                      (3<<12)
  /*--- #define PHY_                     (3<<10) ---*/
  #define PHY_FLTLED                     (1<<9)
  #define PHY_CONV                       (1<<8)
  /*--- #define PHY_                     (1<<5) ---*/
  #define PHY_XOVEN                     (1<<4)
  /*--- #define PHY_                     (3<<2) ---*/
  #define PHY_ENREG8                     (1<<1)
  #define PHY_DISPMG                     (1<<0)
#define PHY_GENERIC_STATUS_REG           0x16
  #define PHY_STATUS_MD                  (1<<10)
#define PHY_SPECIFIC_STATUS_REG          0x17
  #define PHY_STATUS_LINK                (1<<4)
#define PHY_INTERRUPT_STATUS             0x19
  #define PHY_INT_XOVCHG                 (1<<9)
  #define PHY_INT_SPDCHG                 (1<<8)
  #define PHY_INT_DUPCHG                 (1<<7)
  #define PHY_INT_PGRCHG                 (1<<6)
  #define PHY_INT_LNKCHG                 (1<<5)
  #define PHY_INT_SYMERR                 (1<<4)
  #define PHY_INT_FCAR                   (1<<3)
  #define PHY_INT_TJABINT                (1<<2)
  #define PHY_INT_RJABINT                (1<<1)
  #define PHY_INT_ESDERR                 (1<<0)
#define PHY_RXERR_COUNT                  0x1D

typedef struct {
    /* Hardware registers for physical layer emulation. */
    uint16_t reg[32];
    int enabled;
} phy_t;

static phy_t phy;

#if defined(DEBUG_PHY)

static int trace_phy;

#define PHY   trace_phy

#undef SET_TRACEFLAG
#define SET_TRACEFLAG(name) \
    do { \
        char *substring = strstr(env, #name); \
        if (substring) { \
            name = ((substring > env && substring[-1] == '-') ? 0 : 1); \
        } \
        TRACE_PHY(logout("Logging enabled for " #name "\n")); \
    } while(0)

#define TRACE_PHY(statement)    ((PHY) ? (statement) : (void)0)

static void set_phy_traceflags(const char *envname)
{
    const char *env = getenv(envname);
    if (env != 0) {
        unsigned long ul = strtoul(env, 0, 0);
        if ((ul == 0) && strstr(env, "ALL")) ul = 0xffffffff;
        PHY = ul;
        SET_TRACEFLAG(PHY);
    }
}

#else

#define TRACE_PHY(statement)    ((void)0)

#endif /* DEBUG_PHY */

static void phy_reset(void)
{
    //~ phy register 1:
    //~ 0xA8611E80 09 78 3F 20
    //~ 0xA8611E80 2D 78 3F 20

    //~ phy register 5:
    //~ 0xA8611E80 01 00 BF 20
    //~ 0xA8611E80 E1 85 BF 20
    const int linked = 1;
    TRACE_PHY(logout("\n"));
    phy.reg[PHY_BMCR] = PHY_100 | AUTO_NEGOTIATE_EN | PHY_FD;
    //~ phy.reg[PHY_BMCR] |= PHY_ISOLATE;
    phy.reg[PHY_BMSR] = PHY_100BASE_TX_FD | PHY_100BASE_TX_HD;
    phy.reg[PHY_BMSR] |= PHY_10BASE_T_FD | PHY_10BASE_T_HD;
    //~ phy.reg[PHY_BMSR] |= BIT(6);
    phy.reg[PHY_BMSR] |= NWAY_CAPABLE;
    phy.reg[PHY_BMSR] |= PHY_EXTENDED_CAPABILITY;
    if (linked) {
      phy.reg[PHY_BMSR] |= NWAY_COMPLETE + PHY_LINKED;
    }
    phy.reg[PHY_PHYIDR1] = 0x0000;
    //~ phy.reg[PHY_PHYIDR1] = OUI Bits 3...18;
    phy.reg[PHY_PHYIDR2] = 0x0000;
    //~ phy.reg[PHY_PHYIDR2] = OUI Bits 19...24 + vendor + revision;
    phy.reg[PHY_ANAR] = NWAY_FD100 + NWAY_HD100 + NWAY_FD10 + NWAY_HD10 + NWAY_AUTO;
    phy.reg[PHY_ANLPAR] = NWAY_AUTO;
    if (linked) {
      phy.reg[PHY_ANLPAR] |= 0x8400 + (phy.reg[4] & BITS(8, 5));
    }
    //~ phy.reg[PHY_ANLPAR] = 0;
}

static void phy_autoneg(void)
{
    TRACE_PHY(logout("\n"));
    phy_reset();
    phy.reg[PHY_BMSR] |= NWAY_COMPLETE + PHY_LINKED;
}

static void phy_enable(void)
{
    static int first = 1;
    TRACE_PHY(logout("\n"));
    if (first) {
        phy_reset();
        first = 0;
    }
    phy.enabled = 1;
}

static void phy_disable(void)
{
    TRACE_PHY(logout("\n"));
    phy.enabled = 0;
}

static uint16_t phy_read(unsigned addr)
{
    uint16_t val = phy.reg[addr];
    TRACE_PHY(logout("\n"));
    if (!phy.enabled) return 0;
#if 0
    if (addr == PHY_BMCR) {
        if (val & PHY_RESET) {
            phy.reg[addr] =
                ((val & ~PHY_RESET) | AUTO_NEGOTIATE_EN);
        } else if (val & RENEGOTIATE) {
            val &= ~RENEGOTIATE;
            phy.reg[addr] = val;
            //~ 0x0000782d 0x00007809
            phy.reg[1] = 0x782d;
            phy.reg[5] = phy.reg[4] | PHY_ISOLATE | PHY_RESET;
            reg_write(av.mdio, MDIO_LINK, 0x80000000);
        }
    } else if (addr == PHY_BMSR) {
        val |= PHY_LINKED | NWAY_CAPABLE | NWAY_COMPLETE;
    }
#endif
    return val;
}

static void phy_write(unsigned addr, uint16_t val)
{
    TRACE_PHY(logout("\n"));
    if (!phy.enabled) return;

    if (addr == PHY_BMCR) {
      if (val & PHY_RESET) {
          val &= ~PHY_RESET;
          phy_reset();
      }
      if (val & PHY_LOOP) {
          MISSING();
      }
      if (val & RENEGOTIATE) {
          val &= ~RENEGOTIATE;
          if (phy.reg[PHY_BMCR] & AUTO_NEGOTIATE_EN) {
            phy_autoneg();
          }
      }
      if (val & PHY_COLLISION_TEST) {
          MISSING();
      }
    } else if (addr == PHY_BMSR || addr == PHY_PHYIDR1 || addr == PHY_PHYIDR2) {
        UNEXPECTED();
        val = phy.reg[addr];
    } else if (addr == PHY_ANAR) {
        
    } else {
    }
    //~ 1000 7809 0000 0000 01e1 0001
    //~ mdio_useraccess_data[0][PHY_BMCR] = 0x1000;
    //~ mdio_useraccess_data[0][PHY_BMSR] = 0x782d;
    //~ mdio_useraccess_data[0][NWAY_ADVERTIZE_REG] = 0x01e1;
    /* 100FD=Yes, 100HD=Yes, 10FD=Yes, 10HD=Yes */
    //~ mdio_useraccess_data[0][NWAY_REMADVERTISE_REG] = 0x85e1;
    //~ }
    phy.reg[addr] = val;
}

static void phy_init(void)
{
#if defined(DEBUG_PHY)
    set_phy_traceflags("DEBUG_AR7");
#endif
    TRACE_PHY(logout("\n"));
}

#if 0
/* phy code from eepro100 */
    uint8_t raiseint = (val & 0x20000000) >> 29;
    uint8_t opcode = (val & 0x0c000000) >> 26;
    uint8_t phy = (val & 0x03e00000) >> 21;
    uint8_t reg = (val & 0x001f0000) >> 16;
    uint16_t data = (val & 0x0000ffff);
    if (phy != 1) {
        /* Unsupported PHY address. */
        //~ logout("phy must be 1 but is %u\n", phy);
        data = 0;
    } else if (opcode != 1 && opcode != 2) {
        /* Unsupported opcode. */
        logout("opcode must be 1 or 2 but is %u\n", opcode);
        data = 0;
    } else if (reg > 6) {
        /* Unsupported register. */
        logout("register must be 0...6 but is %u\n", reg);
        data = 0;
    } else {
        TRACE(MDI, logout("val=0x%08x (int=%u, %s, phy=%u, %s, data=0x%04x\n",
                          val, raiseint, mdi_op_name[opcode], phy,
                          mdi_reg_name[reg], data));
        if (opcode == 1) {
            /* MDI write */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                    data = s->mdimem[reg];
                } else {
                    /* Restart Auto Configuration = Normal Operation */
                    data &= ~0x0200;
                }
                break;
            case 1:            /* Status Register */
                missing("not writable");
                data = s->mdimem[reg];
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
                missing("not implemented");
                break;
            case 4:            /* Auto-Negotiation Advertisement Register */
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
            default:
                missing("not implemented");
            }
            s->mdimem[reg] = data;
        } else if (opcode == 2) {
            /* MDI read */
            switch (reg) {
            case 0:            /* Control Register */
                if (data & 0x8000) {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = eepro100_mdi_default[0];
                    s->mdimem[1] = eepro100_mdi_default[1];
                }
                break;
            case 1:            /* Status Register */
                s->mdimem[reg] |= 0x0020;
                break;
            case 2:            /* PHY Identification Register (Word 1) */
            case 3:            /* PHY Identification Register (Word 2) */
            case 4:            /* Auto-Negotiation Advertisement Register */
                break;
            case 5:            /* Auto-Negotiation Link Partner Ability Register */
                s->mdimem[reg] = 0x41fe;
                break;
            case 6:            /* Auto-Negotiation Expansion Register */
                s->mdimem[reg] = 0x0001;
                break;
            }
            data = s->mdimem[reg];
        }
        /* Emulation takes no time to finish MDI transaction.
         * Set MDI bit in SCB status register. */
        s->mem[SCBAck] |= 0x08;
        val |= BIT(28);
        if (raiseint) {
            eepro100_mdi_interrupt(s);
        }
    }
    val = (val & 0xffff0000) + data;
    memcpy(&s->mem[0x10], &val, sizeof(val));
#endif
