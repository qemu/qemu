/**
 * QEMU RTL8139 emulation
 *
 * Copyright (c) 2006 Igor Kovalenko
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

 * Modifications:
 *  2006-Jan-28  Mark Malakanov :   TSAD and CSCR implementation (for Windows driver)
 *
 *  2006-Apr-28  Juergen Lock   :   EEPROM emulation changes for FreeBSD driver
 *                                  HW revision ID changes for FreeBSD driver
 *
 *  2006-Jul-01  Igor Kovalenko :   Implemented loopback mode for FreeBSD driver
 *                                  Corrected packet transfer reassembly routine for 8139C+ mode
 *                                  Rearranged debugging print statements
 *                                  Implemented PCI timer interrupt (disabled by default)
 *                                  Implemented Tally Counters, increased VM load/save version
 *                                  Implemented IP/TCP/UDP checksum task offloading
 *
 *  2006-Jul-04  Igor Kovalenko :   Implemented TCP segmentation offloading
 *                                  Fixed MTU=1500 for produced ethernet frames
 *
 *  2006-Jul-09  Igor Kovalenko :   Fixed TCP header length calculation while processing
 *                                  segmentation offloading
 *                                  Removed slirp.h dependency
 *                                  Added rx/tx buffer reset when enabling rx/tx operation
 *
 *  2010-Feb-04  Frediano Ziglio:   Rewrote timer support using QEMU timer only
 *                                  when strictly needed (required for
 *                                  Darwin)
 *  2011-Mar-22  Benjamin Poirier:  Implemented VLAN offloading
 */

/* For crc32 */

#include "qemu/osdep.h"
#include <zlib.h>

#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "net/net.h"
#include "net/eth.h"
#include "sysemu/sysemu.h"
#include "qom/object.h"

/* debug RTL8139 card */
//#define DEBUG_RTL8139 1

#define PCI_PERIOD 30    /* 30 ns period = 33.333333 Mhz frequency */

#define SET_MASKED(input, mask, curr) \
    ( ( (input) & ~(mask) ) | ( (curr) & (mask) ) )

/* arg % size for size which is a power of 2 */
#define MOD2(input, size) \
    ( ( input ) & ( size - 1 )  )

#define ETHER_TYPE_LEN 2

#define VLAN_TCI_LEN 2
#define VLAN_HLEN (ETHER_TYPE_LEN + VLAN_TCI_LEN)

#if defined (DEBUG_RTL8139)
#  define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "RTL8139: " fmt, ## __VA_ARGS__); } while (0)
#else
static inline G_GNUC_PRINTF(1, 2) int DPRINTF(const char *fmt, ...)
{
    return 0;
}
#endif

#define TYPE_RTL8139 "rtl8139"

OBJECT_DECLARE_SIMPLE_TYPE(RTL8139State, RTL8139)

/* Symbolic offsets to registers. */
enum RTL8139_registers {
    MAC0 = 0,        /* Ethernet hardware address. */
    MAR0 = 8,        /* Multicast filter. */
    TxStatus0 = 0x10,/* Transmit status (Four 32bit registers). C mode only */
                     /* Dump Tally Counter control register(64bit). C+ mode only */
    TxAddr0 = 0x20,  /* Tx descriptors (also four 32bit). */
    RxBuf = 0x30,
    ChipCmd = 0x37,
    RxBufPtr = 0x38,
    RxBufAddr = 0x3A,
    IntrMask = 0x3C,
    IntrStatus = 0x3E,
    TxConfig = 0x40,
    RxConfig = 0x44,
    Timer = 0x48,        /* A general-purpose counter. */
    RxMissed = 0x4C,    /* 24 bits valid, write clears. */
    Cfg9346 = 0x50,
    Config0 = 0x51,
    Config1 = 0x52,
    FlashReg = 0x54,
    MediaStatus = 0x58,
    Config3 = 0x59,
    Config4 = 0x5A,        /* absent on RTL-8139A */
    HltClk = 0x5B,
    MultiIntr = 0x5C,
    PCIRevisionID = 0x5E,
    TxSummary = 0x60, /* TSAD register. Transmit Status of All Descriptors*/
    BasicModeCtrl = 0x62,
    BasicModeStatus = 0x64,
    NWayAdvert = 0x66,
    NWayLPAR = 0x68,
    NWayExpansion = 0x6A,
    /* Undocumented registers, but required for proper operation. */
    FIFOTMS = 0x70,        /* FIFO Control and test. */
    CSCR = 0x74,        /* Chip Status and Configuration Register. */
    PARA78 = 0x78,
    PARA7c = 0x7c,        /* Magic transceiver parameter register. */
    Config5 = 0xD8,        /* absent on RTL-8139A */
    /* C+ mode */
    TxPoll        = 0xD9,    /* Tell chip to check Tx descriptors for work */
    RxMaxSize    = 0xDA, /* Max size of an Rx packet (8169 only) */
    CpCmd        = 0xE0, /* C+ Command register (C+ mode only) */
    IntrMitigate    = 0xE2,    /* rx/tx interrupt mitigation control */
    RxRingAddrLO    = 0xE4, /* 64-bit start addr of Rx ring */
    RxRingAddrHI    = 0xE8, /* 64-bit start addr of Rx ring */
    TxThresh    = 0xEC, /* Early Tx threshold */
};

enum ClearBitMasks {
    MultiIntrClear = 0xF000,
    ChipCmdClear = 0xE2,
    Config1Clear = (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1),
};

enum ChipCmdBits {
    CmdReset = 0x10,
    CmdRxEnb = 0x08,
    CmdTxEnb = 0x04,
    RxBufEmpty = 0x01,
};

/* C+ mode */
enum CplusCmdBits {
    CPlusRxVLAN   = 0x0040, /* enable receive VLAN detagging */
    CPlusRxChkSum = 0x0020, /* enable receive checksum offloading */
    CPlusRxEnb    = 0x0002,
    CPlusTxEnb    = 0x0001,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
    PCIErr = 0x8000,
    PCSTimeout = 0x4000,
    RxFIFOOver = 0x40,
    RxUnderrun = 0x20, /* Packet Underrun / Link Change */
    RxOverflow = 0x10,
    TxErr = 0x08,
    TxOK = 0x04,
    RxErr = 0x02,
    RxOK = 0x01,

    RxAckBits = RxFIFOOver | RxOverflow | RxOK,
};

enum TxStatusBits {
    TxHostOwns = 0x2000,
    TxUnderrun = 0x4000,
    TxStatOK = 0x8000,
    TxOutOfWindow = 0x20000000,
    TxAborted = 0x40000000,
    TxCarrierLost = 0x80000000,
};
enum RxStatusBits {
    RxMulticast = 0x8000,
    RxPhysical = 0x4000,
    RxBroadcast = 0x2000,
    RxBadSymbol = 0x0020,
    RxRunt = 0x0010,
    RxTooLong = 0x0008,
    RxCRCErr = 0x0004,
    RxBadAlign = 0x0002,
    RxStatusOK = 0x0001,
};

/* Bits in RxConfig. */
enum rx_mode_bits {
    AcceptErr = 0x20,
    AcceptRunt = 0x10,
    AcceptBroadcast = 0x08,
    AcceptMulticast = 0x04,
    AcceptMyPhys = 0x02,
    AcceptAllPhys = 0x01,
};

/* Bits in TxConfig. */
enum tx_config_bits {

        /* Interframe Gap Time. Only TxIFG96 doesn't violate IEEE 802.3 */
        TxIFGShift = 24,
        TxIFG84 = (0 << TxIFGShift),    /* 8.4us / 840ns (10 / 100Mbps) */
        TxIFG88 = (1 << TxIFGShift),    /* 8.8us / 880ns (10 / 100Mbps) */
        TxIFG92 = (2 << TxIFGShift),    /* 9.2us / 920ns (10 / 100Mbps) */
        TxIFG96 = (3 << TxIFGShift),    /* 9.6us / 960ns (10 / 100Mbps) */

    TxLoopBack = (1 << 18) | (1 << 17), /* enable loopback test mode */
    TxCRC = (1 << 16),    /* DISABLE appending CRC to end of Tx packets */
    TxClearAbt = (1 << 0),    /* Clear abort (WO) */
    TxDMAShift = 8,        /* DMA burst value (0-7) is shifted this many bits */
    TxRetryShift = 4,    /* TXRR value (0-15) is shifted this many bits */

    TxVersionMask = 0x7C800000, /* mask out version bits 30-26, 23 */
};


/* Transmit Status of All Descriptors (TSAD) Register */
enum TSAD_bits {
 TSAD_TOK3 = 1<<15, // TOK bit of Descriptor 3
 TSAD_TOK2 = 1<<14, // TOK bit of Descriptor 2
 TSAD_TOK1 = 1<<13, // TOK bit of Descriptor 1
 TSAD_TOK0 = 1<<12, // TOK bit of Descriptor 0
 TSAD_TUN3 = 1<<11, // TUN bit of Descriptor 3
 TSAD_TUN2 = 1<<10, // TUN bit of Descriptor 2
 TSAD_TUN1 = 1<<9, // TUN bit of Descriptor 1
 TSAD_TUN0 = 1<<8, // TUN bit of Descriptor 0
 TSAD_TABT3 = 1<<07, // TABT bit of Descriptor 3
 TSAD_TABT2 = 1<<06, // TABT bit of Descriptor 2
 TSAD_TABT1 = 1<<05, // TABT bit of Descriptor 1
 TSAD_TABT0 = 1<<04, // TABT bit of Descriptor 0
 TSAD_OWN3 = 1<<03, // OWN bit of Descriptor 3
 TSAD_OWN2 = 1<<02, // OWN bit of Descriptor 2
 TSAD_OWN1 = 1<<01, // OWN bit of Descriptor 1
 TSAD_OWN0 = 1<<00, // OWN bit of Descriptor 0
};


/* Bits in Config1 */
enum Config1Bits {
    Cfg1_PM_Enable = 0x01,
    Cfg1_VPD_Enable = 0x02,
    Cfg1_PIO = 0x04,
    Cfg1_MMIO = 0x08,
    LWAKE = 0x10,        /* not on 8139, 8139A */
    Cfg1_Driver_Load = 0x20,
    Cfg1_LED0 = 0x40,
    Cfg1_LED1 = 0x80,
    SLEEP = (1 << 1),    /* only on 8139, 8139A */
    PWRDN = (1 << 0),    /* only on 8139, 8139A */
};

/* Bits in Config3 */
enum Config3Bits {
    Cfg3_FBtBEn    = (1 << 0), /* 1 = Fast Back to Back */
    Cfg3_FuncRegEn = (1 << 1), /* 1 = enable CardBus Function registers */
    Cfg3_CLKRUN_En = (1 << 2), /* 1 = enable CLKRUN */
    Cfg3_CardB_En  = (1 << 3), /* 1 = enable CardBus registers */
    Cfg3_LinkUp    = (1 << 4), /* 1 = wake up on link up */
    Cfg3_Magic     = (1 << 5), /* 1 = wake up on Magic Packet (tm) */
    Cfg3_PARM_En   = (1 << 6), /* 0 = software can set twister parameters */
    Cfg3_GNTSel    = (1 << 7), /* 1 = delay 1 clock from PCI GNT signal */
};

/* Bits in Config4 */
enum Config4Bits {
    LWPTN = (1 << 2),    /* not on 8139, 8139A */
};

/* Bits in Config5 */
enum Config5Bits {
    Cfg5_PME_STS     = (1 << 0), /* 1 = PCI reset resets PME_Status */
    Cfg5_LANWake     = (1 << 1), /* 1 = enable LANWake signal */
    Cfg5_LDPS        = (1 << 2), /* 0 = save power when link is down */
    Cfg5_FIFOAddrPtr = (1 << 3), /* Realtek internal SRAM testing */
    Cfg5_UWF         = (1 << 4), /* 1 = accept unicast wakeup frame */
    Cfg5_MWF         = (1 << 5), /* 1 = accept multicast wakeup frame */
    Cfg5_BWF         = (1 << 6), /* 1 = accept broadcast wakeup frame */
};

enum RxConfigBits {
    /* rx fifo threshold */
    RxCfgFIFOShift = 13,
    RxCfgFIFONone = (7 << RxCfgFIFOShift),

    /* Max DMA burst */
    RxCfgDMAShift = 8,
    RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

    /* rx ring buffer length */
    RxCfgRcv8K = 0,
    RxCfgRcv16K = (1 << 11),
    RxCfgRcv32K = (1 << 12),
    RxCfgRcv64K = (1 << 11) | (1 << 12),

    /* Disable packet wrap at end of Rx buffer. (not possible with 64k) */
    RxNoWrap = (1 << 7),
};

/* Twister tuning parameters from RealTek.
   Completely undocumented, but required to tune bad links on some boards. */
/*
enum CSCRBits {
    CSCR_LinkOKBit = 0x0400,
    CSCR_LinkChangeBit = 0x0800,
    CSCR_LinkStatusBits = 0x0f000,
    CSCR_LinkDownOffCmd = 0x003c0,
    CSCR_LinkDownCmd = 0x0f3c0,
*/
enum CSCRBits {
    CSCR_Testfun = 1<<15, /* 1 = Auto-neg speeds up internal timer, WO, def 0 */
    CSCR_LD  = 1<<9,  /* Active low TPI link disable signal. When low, TPI still transmits link pulses and TPI stays in good link state. def 1*/
    CSCR_HEART_BIT = 1<<8,  /* 1 = HEART BEAT enable, 0 = HEART BEAT disable. HEART BEAT function is only valid in 10Mbps mode. def 1*/
    CSCR_JBEN = 1<<7,  /* 1 = enable jabber function. 0 = disable jabber function, def 1*/
    CSCR_F_LINK_100 = 1<<6, /* Used to login force good link in 100Mbps for diagnostic purposes. 1 = DISABLE, 0 = ENABLE. def 1*/
    CSCR_F_Connect  = 1<<5,  /* Assertion of this bit forces the disconnect function to be bypassed. def 0*/
    CSCR_Con_status = 1<<3, /* This bit indicates the status of the connection. 1 = valid connected link detected; 0 = disconnected link detected. RO def 0*/
    CSCR_Con_status_En = 1<<2, /* Assertion of this bit configures LED1 pin to indicate connection status. def 0*/
    CSCR_PASS_SCR = 1<<0, /* Bypass Scramble, def 0*/
};

enum Cfg9346Bits {
    Cfg9346_Normal = 0x00,
    Cfg9346_Autoload = 0x40,
    Cfg9346_Programming = 0x80,
    Cfg9346_ConfigWrite = 0xC0,
};

typedef enum {
    CH_8139 = 0,
    CH_8139_K,
    CH_8139A,
    CH_8139A_G,
    CH_8139B,
    CH_8130,
    CH_8139C,
    CH_8100,
    CH_8100B_8139D,
    CH_8101,
} chip_t;

enum chip_flags {
    HasHltClk = (1 << 0),
    HasLWake = (1 << 1),
};

#define HW_REVID(b30, b29, b28, b27, b26, b23, b22) \
    (b30<<30 | b29<<29 | b28<<28 | b27<<27 | b26<<26 | b23<<23 | b22<<22)
#define HW_REVID_MASK    HW_REVID(1, 1, 1, 1, 1, 1, 1)

#define RTL8139_PCI_REVID_8139      0x10
#define RTL8139_PCI_REVID_8139CPLUS 0x20

#define RTL8139_PCI_REVID           RTL8139_PCI_REVID_8139CPLUS

/* Size is 64 * 16bit words */
#define EEPROM_9346_ADDR_BITS 6
#define EEPROM_9346_SIZE  (1 << EEPROM_9346_ADDR_BITS)
#define EEPROM_9346_ADDR_MASK (EEPROM_9346_SIZE - 1)

enum Chip9346Operation
{
    Chip9346_op_mask = 0xc0,          /* 10 zzzzzz */
    Chip9346_op_read = 0x80,          /* 10 AAAAAA */
    Chip9346_op_write = 0x40,         /* 01 AAAAAA D(15)..D(0) */
    Chip9346_op_ext_mask = 0xf0,      /* 11 zzzzzz */
    Chip9346_op_write_enable = 0x30,  /* 00 11zzzz */
    Chip9346_op_write_all = 0x10,     /* 00 01zzzz */
    Chip9346_op_write_disable = 0x00, /* 00 00zzzz */
};

enum Chip9346Mode
{
    Chip9346_none = 0,
    Chip9346_enter_command_mode,
    Chip9346_read_command,
    Chip9346_data_read,      /* from output register */
    Chip9346_data_write,     /* to input register, then to contents at specified address */
    Chip9346_data_write_all, /* to input register, then filling contents */
};

typedef struct EEprom9346
{
    uint16_t contents[EEPROM_9346_SIZE];
    int      mode;
    uint32_t tick;
    uint8_t  address;
    uint16_t input;
    uint16_t output;

    uint8_t eecs;
    uint8_t eesk;
    uint8_t eedi;
    uint8_t eedo;
} EEprom9346;

typedef struct RTL8139TallyCounters
{
    /* Tally counters */
    uint64_t   TxOk;
    uint64_t   RxOk;
    uint64_t   TxERR;
    uint32_t   RxERR;
    uint16_t   MissPkt;
    uint16_t   FAE;
    uint32_t   Tx1Col;
    uint32_t   TxMCol;
    uint64_t   RxOkPhy;
    uint64_t   RxOkBrd;
    uint32_t   RxOkMul;
    uint16_t   TxAbt;
    uint16_t   TxUndrn;
} RTL8139TallyCounters;

/* Clears all tally counters */
static void RTL8139TallyCounters_clear(RTL8139TallyCounters* counters);

struct RTL8139State {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    uint8_t phys[8]; /* mac address */
    uint8_t mult[8]; /* multicast mask array */

    uint32_t TxStatus[4]; /* TxStatus0 in C mode*/ /* also DTCCR[0] and DTCCR[1] in C+ mode */
    uint32_t TxAddr[4];   /* TxAddr0 */
    uint32_t RxBuf;       /* Receive buffer */
    uint32_t RxBufferSize;/* internal variable, receive ring buffer size in C mode */
    uint32_t RxBufPtr;
    uint32_t RxBufAddr;

    uint16_t IntrStatus;
    uint16_t IntrMask;

    uint32_t TxConfig;
    uint32_t RxConfig;
    uint32_t RxMissed;

    uint16_t CSCR;

    uint8_t  Cfg9346;
    uint8_t  Config0;
    uint8_t  Config1;
    uint8_t  Config3;
    uint8_t  Config4;
    uint8_t  Config5;

    uint8_t  clock_enabled;
    uint8_t  bChipCmdState;

    uint16_t MultiIntr;

    uint16_t BasicModeCtrl;
    uint16_t BasicModeStatus;
    uint16_t NWayAdvert;
    uint16_t NWayLPAR;
    uint16_t NWayExpansion;

    uint16_t CpCmd;
    uint8_t  TxThresh;

    NICState *nic;
    NICConf conf;

    /* C ring mode */
    uint32_t   currTxDesc;

    /* C+ mode */
    uint32_t   cplus_enabled;

    uint32_t   currCPlusRxDesc;
    uint32_t   currCPlusTxDesc;

    uint32_t   RxRingAddrLO;
    uint32_t   RxRingAddrHI;

    EEprom9346 eeprom;

    uint32_t   TCTR;
    uint32_t   TimerInt;
    int64_t    TCTR_base;

    /* Tally counters */
    RTL8139TallyCounters tally_counters;

    /* Non-persistent data */
    uint8_t   *cplus_txbuffer;
    int        cplus_txbuffer_len;
    int        cplus_txbuffer_offset;

    /* PCI interrupt timer */
    QEMUTimer *timer;

    MemoryRegion bar_io;
    MemoryRegion bar_mem;

    /* Support migration to/from old versions */
    int rtl8139_mmio_io_addr_dummy;
};

/* Writes tally counters to memory via DMA */
static void RTL8139TallyCounters_dma_write(RTL8139State *s, dma_addr_t tc_addr);

static void rtl8139_set_next_tctr_time(RTL8139State *s);

static void prom9346_decode_command(EEprom9346 *eeprom, uint8_t command)
{
    DPRINTF("eeprom command 0x%02x\n", command);

    switch (command & Chip9346_op_mask)
    {
        case Chip9346_op_read:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->output = eeprom->contents[eeprom->address];
            eeprom->eedo = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_data_read;
            DPRINTF("eeprom read from address 0x%02x data=0x%04x\n",
                eeprom->address, eeprom->output);
        }
        break;

        case Chip9346_op_write:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->input = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_none; /* Chip9346_data_write */
            DPRINTF("eeprom begin write to address 0x%02x\n",
                eeprom->address);
        }
        break;
        default:
            eeprom->mode = Chip9346_none;
            switch (command & Chip9346_op_ext_mask)
            {
                case Chip9346_op_write_enable:
                    DPRINTF("eeprom write enabled\n");
                    break;
                case Chip9346_op_write_all:
                    DPRINTF("eeprom begin write all\n");
                    break;
                case Chip9346_op_write_disable:
                    DPRINTF("eeprom write disabled\n");
                    break;
            }
            break;
    }
}

static void prom9346_shift_clock(EEprom9346 *eeprom)
{
    int bit = eeprom->eedi?1:0;

    ++ eeprom->tick;

    DPRINTF("eeprom: tick %d eedi=%d eedo=%d\n", eeprom->tick, eeprom->eedi,
        eeprom->eedo);

    switch (eeprom->mode)
    {
        case Chip9346_enter_command_mode:
            if (bit)
            {
                eeprom->mode = Chip9346_read_command;
                eeprom->tick = 0;
                eeprom->input = 0;
                DPRINTF("eeprom: +++ synchronized, begin command read\n");
            }
            break;

        case Chip9346_read_command:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 8)
            {
                prom9346_decode_command(eeprom, eeprom->input & 0xff);
            }
            break;

        case Chip9346_data_read:
            eeprom->eedo = (eeprom->output & 0x8000)?1:0;
            eeprom->output <<= 1;
            if (eeprom->tick == 16)
            {
#if 1
        // the FreeBSD drivers (rl and re) don't explicitly toggle
        // CS between reads (or does setting Cfg9346 to 0 count too?),
        // so we need to enter wait-for-command state here
                eeprom->mode = Chip9346_enter_command_mode;
                eeprom->input = 0;
                eeprom->tick = 0;

                DPRINTF("eeprom: +++ end of read, awaiting next command\n");
#else
        // original behaviour
                ++eeprom->address;
                eeprom->address &= EEPROM_9346_ADDR_MASK;
                eeprom->output = eeprom->contents[eeprom->address];
                eeprom->tick = 0;

                DPRINTF("eeprom: +++ read next address 0x%02x data=0x%04x\n",
                    eeprom->address, eeprom->output);
#endif
            }
            break;

        case Chip9346_data_write:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 16)
            {
                DPRINTF("eeprom write to address 0x%02x data=0x%04x\n",
                    eeprom->address, eeprom->input);

                eeprom->contents[eeprom->address] = eeprom->input;
                eeprom->mode = Chip9346_none; /* waiting for next command after CS cycle */
                eeprom->tick = 0;
                eeprom->input = 0;
            }
            break;

        case Chip9346_data_write_all:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 16)
            {
                int i;
                for (i = 0; i < EEPROM_9346_SIZE; i++)
                {
                    eeprom->contents[i] = eeprom->input;
                }
                DPRINTF("eeprom filled with data=0x%04x\n", eeprom->input);

                eeprom->mode = Chip9346_enter_command_mode;
                eeprom->tick = 0;
                eeprom->input = 0;
            }
            break;

        default:
            break;
    }
}

static int prom9346_get_wire(RTL8139State *s)
{
    EEprom9346 *eeprom = &s->eeprom;
    if (!eeprom->eecs)
        return 0;

    return eeprom->eedo;
}

/* FIXME: This should be merged into/replaced by eeprom93xx.c.  */
static void prom9346_set_wire(RTL8139State *s, int eecs, int eesk, int eedi)
{
    EEprom9346 *eeprom = &s->eeprom;
    uint8_t old_eecs = eeprom->eecs;
    uint8_t old_eesk = eeprom->eesk;

    eeprom->eecs = eecs;
    eeprom->eesk = eesk;
    eeprom->eedi = eedi;

    DPRINTF("eeprom: +++ wires CS=%d SK=%d DI=%d DO=%d\n", eeprom->eecs,
        eeprom->eesk, eeprom->eedi, eeprom->eedo);

    if (!old_eecs && eecs)
    {
        /* Synchronize start */
        eeprom->tick = 0;
        eeprom->input = 0;
        eeprom->output = 0;
        eeprom->mode = Chip9346_enter_command_mode;

        DPRINTF("=== eeprom: begin access, enter command mode\n");
    }

    if (!eecs)
    {
        DPRINTF("=== eeprom: end access\n");
        return;
    }

    if (!old_eesk && eesk)
    {
        /* SK front rules */
        prom9346_shift_clock(eeprom);
    }
}

static void rtl8139_update_irq(RTL8139State *s)
{
    PCIDevice *d = PCI_DEVICE(s);
    int isr;
    isr = (s->IntrStatus & s->IntrMask) & 0xffff;

    DPRINTF("Set IRQ to %d (%04x %04x)\n", isr ? 1 : 0, s->IntrStatus,
        s->IntrMask);

    pci_set_irq(d, (isr != 0));
}

static int rtl8139_RxWrap(RTL8139State *s)
{
    /* wrapping enabled; assume 1.5k more buffer space if size < 65536 */
    return (s->RxConfig & (1 << 7));
}

static int rtl8139_receiver_enabled(RTL8139State *s)
{
    return s->bChipCmdState & CmdRxEnb;
}

static int rtl8139_transmitter_enabled(RTL8139State *s)
{
    return s->bChipCmdState & CmdTxEnb;
}

static int rtl8139_cp_receiver_enabled(RTL8139State *s)
{
    return s->CpCmd & CPlusRxEnb;
}

static int rtl8139_cp_transmitter_enabled(RTL8139State *s)
{
    return s->CpCmd & CPlusTxEnb;
}

static void rtl8139_write_buffer(RTL8139State *s, const void *buf, int size)
{
    PCIDevice *d = PCI_DEVICE(s);

    if (s->RxBufAddr + size > s->RxBufferSize)
    {
        int wrapped = MOD2(s->RxBufAddr + size, s->RxBufferSize);

        /* write packet data */
        if (wrapped && !(s->RxBufferSize < 65536 && rtl8139_RxWrap(s)))
        {
            DPRINTF(">>> rx packet wrapped in buffer at %d\n", size - wrapped);

            if (size > wrapped)
            {
                pci_dma_write(d, s->RxBuf + s->RxBufAddr,
                              buf, size-wrapped);
            }

            /* reset buffer pointer */
            s->RxBufAddr = 0;

            pci_dma_write(d, s->RxBuf + s->RxBufAddr,
                          buf + (size-wrapped), wrapped);

            s->RxBufAddr = wrapped;

            return;
        }
    }

    /* non-wrapping path or overwrapping enabled */
    pci_dma_write(d, s->RxBuf + s->RxBufAddr, buf, size);

    s->RxBufAddr += size;
}

#define MIN_BUF_SIZE 60
static inline dma_addr_t rtl8139_addr64(uint32_t low, uint32_t high)
{
    return low | ((uint64_t)high << 32);
}

/* Workaround for buggy guest driver such as linux who allocates rx
 * rings after the receiver were enabled. */
static bool rtl8139_cp_rx_valid(RTL8139State *s)
{
    return !(s->RxRingAddrLO == 0 && s->RxRingAddrHI == 0);
}

static bool rtl8139_can_receive(NetClientState *nc)
{
    RTL8139State *s = qemu_get_nic_opaque(nc);
    int avail;

    /* Receive (drop) packets if card is disabled.  */
    if (!s->clock_enabled) {
        return true;
    }
    if (!rtl8139_receiver_enabled(s)) {
        return true;
    }

    if (rtl8139_cp_receiver_enabled(s) && rtl8139_cp_rx_valid(s)) {
        /* ??? Flow control not implemented in c+ mode.
           This is a hack to work around slirp deficiencies anyway.  */
        return true;
    }

    avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr,
                 s->RxBufferSize);
    return avail == 0 || avail >= 1514 || (s->IntrMask & RxOverflow);
}

static ssize_t rtl8139_do_receive(NetClientState *nc, const uint8_t *buf, size_t size_, int do_interrupt)
{
    RTL8139State *s = qemu_get_nic_opaque(nc);
    PCIDevice *d = PCI_DEVICE(s);
    /* size is the length of the buffer passed to the driver */
    size_t size = size_;
    const uint8_t *dot1q_buf = NULL;

    uint32_t packet_header = 0;

    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    DPRINTF(">>> received len=%zu\n", size);

    /* test if board clock is stopped */
    if (!s->clock_enabled)
    {
        DPRINTF("stopped ==========================\n");
        return -1;
    }

    /* first check if receiver is enabled */

    if (!rtl8139_receiver_enabled(s))
    {
        DPRINTF("receiver disabled ================\n");
        return -1;
    }

    /* XXX: check this */
    if (s->RxConfig & AcceptAllPhys) {
        /* promiscuous: receive all */
        DPRINTF(">>> packet received in promiscuous mode\n");

    } else {
        if (!memcmp(buf,  broadcast_macaddr, 6)) {
            /* broadcast address */
            if (!(s->RxConfig & AcceptBroadcast))
            {
                DPRINTF(">>> broadcast packet rejected\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxBroadcast;

            DPRINTF(">>> broadcast packet received\n");

            /* update tally counter */
            ++s->tally_counters.RxOkBrd;

        } else if (buf[0] & 0x01) {
            /* multicast */
            if (!(s->RxConfig & AcceptMulticast))
            {
                DPRINTF(">>> multicast packet rejected\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            int mcast_idx = net_crc32(buf, ETH_ALEN) >> 26;

            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))))
            {
                DPRINTF(">>> multicast address mismatch\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxMulticast;

            DPRINTF(">>> multicast packet received\n");

            /* update tally counter */
            ++s->tally_counters.RxOkMul;

        } else if (s->phys[0] == buf[0] &&
                   s->phys[1] == buf[1] &&
                   s->phys[2] == buf[2] &&
                   s->phys[3] == buf[3] &&
                   s->phys[4] == buf[4] &&
                   s->phys[5] == buf[5]) {
            /* match */
            if (!(s->RxConfig & AcceptMyPhys))
            {
                DPRINTF(">>> rejecting physical address matching packet\n");

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return size;
            }

            packet_header |= RxPhysical;

            DPRINTF(">>> physical address matching packet received\n");

            /* update tally counter */
            ++s->tally_counters.RxOkPhy;

        } else {

            DPRINTF(">>> unknown packet\n");

            /* update tally counter */
            ++s->tally_counters.RxERR;

            return size;
        }
    }

    if (rtl8139_cp_receiver_enabled(s))
    {
        if (!rtl8139_cp_rx_valid(s)) {
            return size;
        }

        DPRINTF("in C+ Rx mode ================\n");

        /* begin C+ receiver mode */

/* w0 ownership flag */
#define CP_RX_OWN (1<<31)
/* w0 end of ring flag */
#define CP_RX_EOR (1<<30)
/* w0 bits 0...12 : buffer size */
#define CP_RX_BUFFER_SIZE_MASK ((1<<13) - 1)
/* w1 tag available flag */
#define CP_RX_TAVA (1<<16)
/* w1 bits 0...15 : VLAN tag */
#define CP_RX_VLAN_TAG_MASK ((1<<16) - 1)
/* w2 low  32bit of Rx buffer ptr */
/* w3 high 32bit of Rx buffer ptr */

        int descriptor = s->currCPlusRxDesc;
        dma_addr_t cplus_rx_ring_desc;

        cplus_rx_ring_desc = rtl8139_addr64(s->RxRingAddrLO, s->RxRingAddrHI);
        cplus_rx_ring_desc += 16 * descriptor;

        DPRINTF("+++ C+ mode reading RX descriptor %d from host memory at "
            "%08x %08x = "DMA_ADDR_FMT"\n", descriptor, s->RxRingAddrHI,
            s->RxRingAddrLO, cplus_rx_ring_desc);

        uint32_t val, rxdw0,rxdw1,rxbufLO,rxbufHI;

        pci_dma_read(d, cplus_rx_ring_desc, &val, 4);
        rxdw0 = le32_to_cpu(val);
        pci_dma_read(d, cplus_rx_ring_desc+4, &val, 4);
        rxdw1 = le32_to_cpu(val);
        pci_dma_read(d, cplus_rx_ring_desc+8, &val, 4);
        rxbufLO = le32_to_cpu(val);
        pci_dma_read(d, cplus_rx_ring_desc+12, &val, 4);
        rxbufHI = le32_to_cpu(val);

        DPRINTF("+++ C+ mode RX descriptor %d %08x %08x %08x %08x\n",
            descriptor, rxdw0, rxdw1, rxbufLO, rxbufHI);

        if (!(rxdw0 & CP_RX_OWN))
        {
            DPRINTF("C+ Rx mode : descriptor %d is owned by host\n",
                descriptor);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            /* update tally counter */
            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return size_;
        }

        uint32_t rx_space = rxdw0 & CP_RX_BUFFER_SIZE_MASK;

        /* write VLAN info to descriptor variables. */
        if (s->CpCmd & CPlusRxVLAN &&
            lduw_be_p(&buf[ETH_ALEN * 2]) == ETH_P_VLAN) {
            dot1q_buf = &buf[ETH_ALEN * 2];
            size -= VLAN_HLEN;
            /* if too small buffer, use the tailroom added duing expansion */
            if (size < MIN_BUF_SIZE) {
                size = MIN_BUF_SIZE;
            }

            rxdw1 &= ~CP_RX_VLAN_TAG_MASK;
            /* BE + ~le_to_cpu()~ + cpu_to_le() = BE */
            rxdw1 |= CP_RX_TAVA | lduw_le_p(&dot1q_buf[ETHER_TYPE_LEN]);

            DPRINTF("C+ Rx mode : extracted vlan tag with tci: ""%u\n",
                lduw_be_p(&dot1q_buf[ETHER_TYPE_LEN]));
        } else {
            /* reset VLAN tag flag */
            rxdw1 &= ~CP_RX_TAVA;
        }

        /* TODO: scatter the packet over available receive ring descriptors space */

        if (size+4 > rx_space)
        {
            DPRINTF("C+ Rx mode : descriptor %d size %d received %zu + 4\n",
                descriptor, rx_space, size);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            /* update tally counter */
            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return size_;
        }

        dma_addr_t rx_addr = rtl8139_addr64(rxbufLO, rxbufHI);

        /* receive/copy to target memory */
        if (dot1q_buf) {
            pci_dma_write(d, rx_addr, buf, 2 * ETH_ALEN);
            pci_dma_write(d, rx_addr + 2 * ETH_ALEN,
                          buf + 2 * ETH_ALEN + VLAN_HLEN,
                          size - 2 * ETH_ALEN);
        } else {
            pci_dma_write(d, rx_addr, buf, size);
        }

        if (s->CpCmd & CPlusRxChkSum)
        {
            /* do some packet checksumming */
        }

        /* write checksum */
        val = cpu_to_le32(crc32(0, buf, size_));
        pci_dma_write(d, rx_addr+size, (uint8_t *)&val, 4);

/* first segment of received packet flag */
#define CP_RX_STATUS_FS (1<<29)
/* last segment of received packet flag */
#define CP_RX_STATUS_LS (1<<28)
/* multicast packet flag */
#define CP_RX_STATUS_MAR (1<<26)
/* physical-matching packet flag */
#define CP_RX_STATUS_PAM (1<<25)
/* broadcast packet flag */
#define CP_RX_STATUS_BAR (1<<24)
/* runt packet flag */
#define CP_RX_STATUS_RUNT (1<<19)
/* crc error flag */
#define CP_RX_STATUS_CRC (1<<18)
/* IP checksum error flag */
#define CP_RX_STATUS_IPF (1<<15)
/* UDP checksum error flag */
#define CP_RX_STATUS_UDPF (1<<14)
/* TCP checksum error flag */
#define CP_RX_STATUS_TCPF (1<<13)

        /* transfer ownership to target */
        rxdw0 &= ~CP_RX_OWN;

        /* set first segment bit */
        rxdw0 |= CP_RX_STATUS_FS;

        /* set last segment bit */
        rxdw0 |= CP_RX_STATUS_LS;

        /* set received packet type flags */
        if (packet_header & RxBroadcast)
            rxdw0 |= CP_RX_STATUS_BAR;
        if (packet_header & RxMulticast)
            rxdw0 |= CP_RX_STATUS_MAR;
        if (packet_header & RxPhysical)
            rxdw0 |= CP_RX_STATUS_PAM;

        /* set received size */
        rxdw0 &= ~CP_RX_BUFFER_SIZE_MASK;
        rxdw0 |= (size+4);

        /* update ring data */
        val = cpu_to_le32(rxdw0);
        pci_dma_write(d, cplus_rx_ring_desc, (uint8_t *)&val, 4);
        val = cpu_to_le32(rxdw1);
        pci_dma_write(d, cplus_rx_ring_desc+4, (uint8_t *)&val, 4);

        /* update tally counter */
        ++s->tally_counters.RxOk;

        /* seek to next Rx descriptor */
        if (rxdw0 & CP_RX_EOR)
        {
            s->currCPlusRxDesc = 0;
        }
        else
        {
            ++s->currCPlusRxDesc;
        }

        DPRINTF("done C+ Rx mode ----------------\n");

    }
    else
    {
        DPRINTF("in ring Rx mode ================\n");

        /* begin ring receiver mode */
        int avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr, s->RxBufferSize);

        /* if receiver buffer is empty then avail == 0 */

#define RX_ALIGN(x) (((x) + 3) & ~0x3)

        if (avail != 0 && RX_ALIGN(size + 8) >= avail)
        {
            DPRINTF("rx overflow: rx buffer length %d head 0x%04x "
                "read 0x%04x === available 0x%04x need 0x%04zx\n",
                s->RxBufferSize, s->RxBufAddr, s->RxBufPtr, avail, size + 8);

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;
            rtl8139_update_irq(s);
            return 0;
        }

        packet_header |= RxStatusOK;

        packet_header |= (((size+4) << 16) & 0xffff0000);

        /* write header */
        uint32_t val = cpu_to_le32(packet_header);

        rtl8139_write_buffer(s, (uint8_t *)&val, 4);

        rtl8139_write_buffer(s, buf, size);

        /* write checksum */
        val = cpu_to_le32(crc32(0, buf, size));
        rtl8139_write_buffer(s, (uint8_t *)&val, 4);

        /* correct buffer write pointer */
        s->RxBufAddr = MOD2(RX_ALIGN(s->RxBufAddr), s->RxBufferSize);

        /* now we can signal we have received something */

        DPRINTF("received: rx buffer length %d head 0x%04x read 0x%04x\n",
            s->RxBufferSize, s->RxBufAddr, s->RxBufPtr);
    }

    s->IntrStatus |= RxOK;

    if (do_interrupt)
    {
        rtl8139_update_irq(s);
    }

    return size_;
}

static ssize_t rtl8139_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    return rtl8139_do_receive(nc, buf, size, 1);
}

static void rtl8139_reset_rxring(RTL8139State *s, uint32_t bufferSize)
{
    s->RxBufferSize = bufferSize;
    s->RxBufPtr  = 0;
    s->RxBufAddr = 0;
}

static void rtl8139_reset_phy(RTL8139State *s)
{
    s->BasicModeStatus  = 0x7809;
    s->BasicModeStatus |= 0x0020; /* autonegotiation completed */
    /* preserve link state */
    s->BasicModeStatus |= qemu_get_queue(s->nic)->link_down ? 0 : 0x04;

    s->NWayAdvert    = 0x05e1; /* all modes, full duplex */
    s->NWayLPAR      = 0x05e1; /* all modes, full duplex */
    s->NWayExpansion = 0x0001; /* autonegotiation supported */

    s->CSCR = CSCR_F_LINK_100 | CSCR_HEART_BIT | CSCR_LD;
}

static void rtl8139_reset(DeviceState *d)
{
    RTL8139State *s = RTL8139(d);
    int i;

    /* restore MAC address */
    memcpy(s->phys, s->conf.macaddr.a, 6);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->phys);

    /* reset interrupt mask */
    s->IntrStatus = 0;
    s->IntrMask = 0;

    rtl8139_update_irq(s);

    /* mark all status registers as owned by host */
    for (i = 0; i < 4; ++i)
    {
        s->TxStatus[i] = TxHostOwns;
    }

    s->currTxDesc = 0;
    s->currCPlusRxDesc = 0;
    s->currCPlusTxDesc = 0;

    s->RxRingAddrLO = 0;
    s->RxRingAddrHI = 0;

    s->RxBuf = 0;

    rtl8139_reset_rxring(s, 8192);

    /* ACK the reset */
    s->TxConfig = 0;

#if 0
//    s->TxConfig |= HW_REVID(1, 0, 0, 0, 0, 0, 0); // RTL-8139  HasHltClk
    s->clock_enabled = 0;
#else
    s->TxConfig |= HW_REVID(1, 1, 1, 0, 1, 1, 0); // RTL-8139C+ HasLWake
    s->clock_enabled = 1;
#endif

    s->bChipCmdState = CmdReset; /* RxBufEmpty bit is calculated on read from ChipCmd */;

    /* set initial state data */
    s->Config0 = 0x0; /* No boot ROM */
    s->Config1 = 0xC; /* IO mapped and MEM mapped registers available */
    s->Config3 = 0x1; /* fast back-to-back compatible */
    s->Config5 = 0x0;

    s->CpCmd   = 0x0; /* reset C+ mode */
    s->cplus_enabled = 0;

//    s->BasicModeCtrl = 0x3100; // 100Mbps, full duplex, autonegotiation
//    s->BasicModeCtrl = 0x2100; // 100Mbps, full duplex
    s->BasicModeCtrl = 0x1000; // autonegotiation

    rtl8139_reset_phy(s);

    /* also reset timer and disable timer interrupt */
    s->TCTR = 0;
    s->TimerInt = 0;
    s->TCTR_base = 0;
    rtl8139_set_next_tctr_time(s);

    /* reset tally counters */
    RTL8139TallyCounters_clear(&s->tally_counters);
}

static void RTL8139TallyCounters_clear(RTL8139TallyCounters* counters)
{
    counters->TxOk = 0;
    counters->RxOk = 0;
    counters->TxERR = 0;
    counters->RxERR = 0;
    counters->MissPkt = 0;
    counters->FAE = 0;
    counters->Tx1Col = 0;
    counters->TxMCol = 0;
    counters->RxOkPhy = 0;
    counters->RxOkBrd = 0;
    counters->RxOkMul = 0;
    counters->TxAbt = 0;
    counters->TxUndrn = 0;
}

static void RTL8139TallyCounters_dma_write(RTL8139State *s, dma_addr_t tc_addr)
{
    PCIDevice *d = PCI_DEVICE(s);
    RTL8139TallyCounters *tally_counters = &s->tally_counters;
    uint16_t val16;
    uint32_t val32;
    uint64_t val64;

    val64 = cpu_to_le64(tally_counters->TxOk);
    pci_dma_write(d, tc_addr + 0,     (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->RxOk);
    pci_dma_write(d, tc_addr + 8,     (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->TxERR);
    pci_dma_write(d, tc_addr + 16,    (uint8_t *)&val64, 8);

    val32 = cpu_to_le32(tally_counters->RxERR);
    pci_dma_write(d, tc_addr + 24,    (uint8_t *)&val32, 4);

    val16 = cpu_to_le16(tally_counters->MissPkt);
    pci_dma_write(d, tc_addr + 28,    (uint8_t *)&val16, 2);

    val16 = cpu_to_le16(tally_counters->FAE);
    pci_dma_write(d, tc_addr + 30,    (uint8_t *)&val16, 2);

    val32 = cpu_to_le32(tally_counters->Tx1Col);
    pci_dma_write(d, tc_addr + 32,    (uint8_t *)&val32, 4);

    val32 = cpu_to_le32(tally_counters->TxMCol);
    pci_dma_write(d, tc_addr + 36,    (uint8_t *)&val32, 4);

    val64 = cpu_to_le64(tally_counters->RxOkPhy);
    pci_dma_write(d, tc_addr + 40,    (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->RxOkBrd);
    pci_dma_write(d, tc_addr + 48,    (uint8_t *)&val64, 8);

    val32 = cpu_to_le32(tally_counters->RxOkMul);
    pci_dma_write(d, tc_addr + 56,    (uint8_t *)&val32, 4);

    val16 = cpu_to_le16(tally_counters->TxAbt);
    pci_dma_write(d, tc_addr + 60,    (uint8_t *)&val16, 2);

    val16 = cpu_to_le16(tally_counters->TxUndrn);
    pci_dma_write(d, tc_addr + 62,    (uint8_t *)&val16, 2);
}

static void rtl8139_ChipCmd_write(RTL8139State *s, uint32_t val)
{
    DeviceState *d = DEVICE(s);

    val &= 0xff;

    DPRINTF("ChipCmd write val=0x%08x\n", val);

    if (val & CmdReset)
    {
        DPRINTF("ChipCmd reset\n");
        rtl8139_reset(d);
    }
    if (val & CmdRxEnb)
    {
        DPRINTF("ChipCmd enable receiver\n");

        s->currCPlusRxDesc = 0;
    }
    if (val & CmdTxEnb)
    {
        DPRINTF("ChipCmd enable transmitter\n");

        s->currCPlusTxDesc = 0;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xe3, s->bChipCmdState);

    /* Deassert reset pin before next read */
    val &= ~CmdReset;

    s->bChipCmdState = val;
}

static int rtl8139_RxBufferEmpty(RTL8139State *s)
{
    int unread = MOD2(s->RxBufferSize + s->RxBufAddr - s->RxBufPtr, s->RxBufferSize);

    if (unread != 0)
    {
        DPRINTF("receiver buffer data available 0x%04x\n", unread);
        return 0;
    }

    DPRINTF("receiver buffer is empty\n");

    return 1;
}

static uint32_t rtl8139_ChipCmd_read(RTL8139State *s)
{
    uint32_t ret = s->bChipCmdState;

    if (rtl8139_RxBufferEmpty(s))
        ret |= RxBufEmpty;

    DPRINTF("ChipCmd read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_CpCmd_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DPRINTF("C+ command register write(w) val=0x%04x\n", val);

    s->cplus_enabled = 1;

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xff84, s->CpCmd);

    s->CpCmd = val;
}

static uint32_t rtl8139_CpCmd_read(RTL8139State *s)
{
    uint32_t ret = s->CpCmd;

    DPRINTF("C+ command register read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_IntrMitigate_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("C+ IntrMitigate register write(w) val=0x%04x\n", val);
}

static uint32_t rtl8139_IntrMitigate_read(RTL8139State *s)
{
    uint32_t ret = 0;

    DPRINTF("C+ IntrMitigate register read(w) val=0x%04x\n", ret);

    return ret;
}

static int rtl8139_config_writable(RTL8139State *s)
{
    if ((s->Cfg9346 & Chip9346_op_mask) == Cfg9346_ConfigWrite)
    {
        return 1;
    }

    DPRINTF("Configuration registers are write-protected\n");

    return 0;
}

static void rtl8139_BasicModeCtrl_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DPRINTF("BasicModeCtrl register write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    uint32_t mask = 0xccff;

    if (1 || !rtl8139_config_writable(s))
    {
        /* Speed setting and autonegotiation enable bits are read-only */
        mask |= 0x3000;
        /* Duplex mode setting is read-only */
        mask |= 0x0100;
    }

    if (val & 0x8000) {
        /* Reset PHY */
        rtl8139_reset_phy(s);
    }

    val = SET_MASKED(val, mask, s->BasicModeCtrl);

    s->BasicModeCtrl = val;
}

static uint32_t rtl8139_BasicModeCtrl_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeCtrl;

    DPRINTF("BasicModeCtrl register read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_BasicModeStatus_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DPRINTF("BasicModeStatus register write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xff3f, s->BasicModeStatus);

    s->BasicModeStatus = val;
}

static uint32_t rtl8139_BasicModeStatus_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeStatus;

    DPRINTF("BasicModeStatus register read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_Cfg9346_write(RTL8139State *s, uint32_t val)
{
    DeviceState *d = DEVICE(s);

    val &= 0xff;

    DPRINTF("Cfg9346 write val=0x%02x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x31, s->Cfg9346);

    uint32_t opmode = val & 0xc0;
    uint32_t eeprom_val = val & 0xf;

    if (opmode == 0x80) {
        /* eeprom access */
        int eecs = (eeprom_val & 0x08)?1:0;
        int eesk = (eeprom_val & 0x04)?1:0;
        int eedi = (eeprom_val & 0x02)?1:0;
        prom9346_set_wire(s, eecs, eesk, eedi);
    } else if (opmode == 0x40) {
        /* Reset.  */
        val = 0;
        rtl8139_reset(d);
    }

    s->Cfg9346 = val;
}

static uint32_t rtl8139_Cfg9346_read(RTL8139State *s)
{
    uint32_t ret = s->Cfg9346;

    uint32_t opmode = ret & 0xc0;

    if (opmode == 0x80)
    {
        /* eeprom access */
        int eedo = prom9346_get_wire(s);
        if (eedo)
        {
            ret |=  0x01;
        }
        else
        {
            ret &= ~0x01;
        }
    }

    DPRINTF("Cfg9346 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config0_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config0 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xf8, s->Config0);

    s->Config0 = val;
}

static uint32_t rtl8139_Config0_read(RTL8139State *s)
{
    uint32_t ret = s->Config0;

    DPRINTF("Config0 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config1_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config1 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xC, s->Config1);

    s->Config1 = val;
}

static uint32_t rtl8139_Config1_read(RTL8139State *s)
{
    uint32_t ret = s->Config1;

    DPRINTF("Config1 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config3_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config3 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x8F, s->Config3);

    s->Config3 = val;
}

static uint32_t rtl8139_Config3_read(RTL8139State *s)
{
    uint32_t ret = s->Config3;

    DPRINTF("Config3 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config4_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config4 write val=0x%02x\n", val);

    if (!rtl8139_config_writable(s)) {
        return;
    }

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x0a, s->Config4);

    s->Config4 = val;
}

static uint32_t rtl8139_Config4_read(RTL8139State *s)
{
    uint32_t ret = s->Config4;

    DPRINTF("Config4 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_Config5_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DPRINTF("Config5 write val=0x%02x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x80, s->Config5);

    s->Config5 = val;
}

static uint32_t rtl8139_Config5_read(RTL8139State *s)
{
    uint32_t ret = s->Config5;

    DPRINTF("Config5 read val=0x%02x\n", ret);

    return ret;
}

static void rtl8139_TxConfig_write(RTL8139State *s, uint32_t val)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DPRINTF("transmitter disabled; no TxConfig write val=0x%08x\n", val);
        return;
    }

    DPRINTF("TxConfig write val=0x%08x\n", val);

    val = SET_MASKED(val, TxVersionMask | 0x8070f80f, s->TxConfig);

    s->TxConfig = val;
}

static void rtl8139_TxConfig_writeb(RTL8139State *s, uint32_t val)
{
    DPRINTF("RTL8139C TxConfig via write(b) val=0x%02x\n", val);

    uint32_t tc = s->TxConfig;
    tc &= 0xFFFFFF00;
    tc |= (val & 0x000000FF);
    rtl8139_TxConfig_write(s, tc);
}

static uint32_t rtl8139_TxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->TxConfig;

    DPRINTF("TxConfig read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_RxConfig_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("RxConfig write val=0x%08x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xf0fc0040, s->RxConfig);

    s->RxConfig = val;

    /* reset buffer size and read/write pointers */
    rtl8139_reset_rxring(s, 8192 << ((s->RxConfig >> 11) & 0x3));

    DPRINTF("RxConfig write reset buffer size to %d\n", s->RxBufferSize);
}

static uint32_t rtl8139_RxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->RxConfig;

    DPRINTF("RxConfig read val=0x%08x\n", ret);

    return ret;
}

static void rtl8139_transfer_frame(RTL8139State *s, uint8_t *buf, int size,
    int do_interrupt, const uint8_t *dot1q_buf)
{
    struct iovec *iov = NULL;
    struct iovec vlan_iov[3];

    if (!size)
    {
        DPRINTF("+++ empty ethernet frame\n");
        return;
    }

    if (dot1q_buf && size >= ETH_ALEN * 2) {
        iov = (struct iovec[3]) {
            { .iov_base = buf, .iov_len = ETH_ALEN * 2 },
            { .iov_base = (void *) dot1q_buf, .iov_len = VLAN_HLEN },
            { .iov_base = buf + ETH_ALEN * 2,
                .iov_len = size - ETH_ALEN * 2 },
        };

        memcpy(vlan_iov, iov, sizeof(vlan_iov));
        iov = vlan_iov;
    }

    if (TxLoopBack == (s->TxConfig & TxLoopBack))
    {
        size_t buf2_size;
        uint8_t *buf2;

        if (iov) {
            buf2_size = iov_size(iov, 3);
            buf2 = g_malloc(buf2_size);
            iov_to_buf(iov, 3, 0, buf2, buf2_size);
            buf = buf2;
        }

        DPRINTF("+++ transmit loopback mode\n");
        qemu_receive_packet(qemu_get_queue(s->nic), buf, size);

        if (iov) {
            g_free(buf2);
        }
    }
    else
    {
        if (iov) {
            qemu_sendv_packet(qemu_get_queue(s->nic), iov, 3);
        } else {
            qemu_send_packet(qemu_get_queue(s->nic), buf, size);
        }
    }
}

static int rtl8139_transmit_one(RTL8139State *s, int descriptor)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DPRINTF("+++ cannot transmit from descriptor %d: transmitter "
            "disabled\n", descriptor);
        return 0;
    }

    if (s->TxStatus[descriptor] & TxHostOwns)
    {
        DPRINTF("+++ cannot transmit from descriptor %d: owned by host "
            "(%08x)\n", descriptor, s->TxStatus[descriptor]);
        return 0;
    }

    DPRINTF("+++ transmitting from descriptor %d\n", descriptor);

    PCIDevice *d = PCI_DEVICE(s);
    int txsize = s->TxStatus[descriptor] & 0x1fff;
    uint8_t txbuffer[0x2000];

    DPRINTF("+++ transmit reading %d bytes from host memory at 0x%08x\n",
        txsize, s->TxAddr[descriptor]);

    pci_dma_read(d, s->TxAddr[descriptor], txbuffer, txsize);

    /* Mark descriptor as transferred */
    s->TxStatus[descriptor] |= TxHostOwns;
    s->TxStatus[descriptor] |= TxStatOK;

    rtl8139_transfer_frame(s, txbuffer, txsize, 0, NULL);

    DPRINTF("+++ transmitted %d bytes from descriptor %d\n", txsize,
        descriptor);

    /* update interrupt */
    s->IntrStatus |= TxOK;
    rtl8139_update_irq(s);

    return 1;
}

#define TCP_HEADER_CLEAR_FLAGS(tcp, off) ((tcp)->th_offset_flags &= cpu_to_be16(~TCP_FLAGS_ONLY(off)))

/* produces ones' complement sum of data */
static uint16_t ones_complement_sum(uint8_t *data, size_t len)
{
    uint32_t result = 0;

    for (; len > 1; data+=2, len-=2)
    {
        result += *(uint16_t*)data;
    }

    /* add the remainder byte */
    if (len)
    {
        uint8_t odd[2] = {*data, 0};
        result += *(uint16_t*)odd;
    }

    while (result>>16)
        result = (result & 0xffff) + (result >> 16);

    return result;
}

static uint16_t ip_checksum(void *data, size_t len)
{
    return ~ones_complement_sum((uint8_t*)data, len);
}

static int rtl8139_cplus_transmit_one(RTL8139State *s)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DPRINTF("+++ C+ mode: transmitter disabled\n");
        return 0;
    }

    if (!rtl8139_cp_transmitter_enabled(s))
    {
        DPRINTF("+++ C+ mode: C+ transmitter disabled\n");
        return 0 ;
    }

    PCIDevice *d = PCI_DEVICE(s);
    int descriptor = s->currCPlusTxDesc;

    dma_addr_t cplus_tx_ring_desc = rtl8139_addr64(s->TxAddr[0], s->TxAddr[1]);

    /* Normal priority ring */
    cplus_tx_ring_desc += 16 * descriptor;

    DPRINTF("+++ C+ mode reading TX descriptor %d from host memory at "
        "%08x %08x = 0x"DMA_ADDR_FMT"\n", descriptor, s->TxAddr[1],
        s->TxAddr[0], cplus_tx_ring_desc);

    uint32_t val, txdw0,txdw1,txbufLO,txbufHI;

    pci_dma_read(d, cplus_tx_ring_desc,    (uint8_t *)&val, 4);
    txdw0 = le32_to_cpu(val);
    pci_dma_read(d, cplus_tx_ring_desc+4,  (uint8_t *)&val, 4);
    txdw1 = le32_to_cpu(val);
    pci_dma_read(d, cplus_tx_ring_desc+8,  (uint8_t *)&val, 4);
    txbufLO = le32_to_cpu(val);
    pci_dma_read(d, cplus_tx_ring_desc+12, (uint8_t *)&val, 4);
    txbufHI = le32_to_cpu(val);

    DPRINTF("+++ C+ mode TX descriptor %d %08x %08x %08x %08x\n", descriptor,
        txdw0, txdw1, txbufLO, txbufHI);

/* w0 ownership flag */
#define CP_TX_OWN (1<<31)
/* w0 end of ring flag */
#define CP_TX_EOR (1<<30)
/* first segment of received packet flag */
#define CP_TX_FS (1<<29)
/* last segment of received packet flag */
#define CP_TX_LS (1<<28)
/* large send packet flag */
#define CP_TX_LGSEN (1<<27)
/* large send MSS mask, bits 16...26 */
#define CP_TC_LGSEN_MSS_SHIFT 16
#define CP_TC_LGSEN_MSS_MASK ((1 << 11) - 1)

/* IP checksum offload flag */
#define CP_TX_IPCS (1<<18)
/* UDP checksum offload flag */
#define CP_TX_UDPCS (1<<17)
/* TCP checksum offload flag */
#define CP_TX_TCPCS (1<<16)

/* w0 bits 0...15 : buffer size */
#define CP_TX_BUFFER_SIZE (1<<16)
#define CP_TX_BUFFER_SIZE_MASK (CP_TX_BUFFER_SIZE - 1)
/* w1 add tag flag */
#define CP_TX_TAGC (1<<17)
/* w1 bits 0...15 : VLAN tag (big endian) */
#define CP_TX_VLAN_TAG_MASK ((1<<16) - 1)
/* w2 low  32bit of Rx buffer ptr */
/* w3 high 32bit of Rx buffer ptr */

/* set after transmission */
/* FIFO underrun flag */
#define CP_TX_STATUS_UNF (1<<25)
/* transmit error summary flag, valid if set any of three below */
#define CP_TX_STATUS_TES (1<<23)
/* out-of-window collision flag */
#define CP_TX_STATUS_OWC (1<<22)
/* link failure flag */
#define CP_TX_STATUS_LNKF (1<<21)
/* excessive collisions flag */
#define CP_TX_STATUS_EXC (1<<20)

    if (!(txdw0 & CP_TX_OWN))
    {
        DPRINTF("C+ Tx mode : descriptor %d is owned by host\n", descriptor);
        return 0 ;
    }

    DPRINTF("+++ C+ Tx mode : transmitting from descriptor %d\n", descriptor);

    if (txdw0 & CP_TX_FS)
    {
        DPRINTF("+++ C+ Tx mode : descriptor %d is first segment "
            "descriptor\n", descriptor);

        /* reset internal buffer offset */
        s->cplus_txbuffer_offset = 0;
    }

    int txsize = txdw0 & CP_TX_BUFFER_SIZE_MASK;
    dma_addr_t tx_addr = rtl8139_addr64(txbufLO, txbufHI);

    /* make sure we have enough space to assemble the packet */
    if (!s->cplus_txbuffer)
    {
        s->cplus_txbuffer_len = CP_TX_BUFFER_SIZE;
        s->cplus_txbuffer = g_malloc(s->cplus_txbuffer_len);
        s->cplus_txbuffer_offset = 0;

        DPRINTF("+++ C+ mode transmission buffer allocated space %d\n",
            s->cplus_txbuffer_len);
    }

    if (s->cplus_txbuffer_offset + txsize >= s->cplus_txbuffer_len)
    {
        /* The spec didn't tell the maximum size, stick to CP_TX_BUFFER_SIZE */
        txsize = s->cplus_txbuffer_len - s->cplus_txbuffer_offset;
        DPRINTF("+++ C+ mode transmission buffer overrun, truncated descriptor"
                "length to %d\n", txsize);
    }

    /* append more data to the packet */

    DPRINTF("+++ C+ mode transmit reading %d bytes from host memory at "
            DMA_ADDR_FMT" to offset %d\n", txsize, tx_addr,
            s->cplus_txbuffer_offset);

    pci_dma_read(d, tx_addr,
                 s->cplus_txbuffer + s->cplus_txbuffer_offset, txsize);
    s->cplus_txbuffer_offset += txsize;

    /* seek to next Rx descriptor */
    if (txdw0 & CP_TX_EOR)
    {
        s->currCPlusTxDesc = 0;
    }
    else
    {
        ++s->currCPlusTxDesc;
        if (s->currCPlusTxDesc >= 64)
            s->currCPlusTxDesc = 0;
    }

    /* Build the Tx Status Descriptor */
    uint32_t tx_status = txdw0;

    /* transfer ownership to target */
    tx_status &= ~CP_TX_OWN;

    /* reset error indicator bits */
    tx_status &= ~CP_TX_STATUS_UNF;
    tx_status &= ~CP_TX_STATUS_TES;
    tx_status &= ~CP_TX_STATUS_OWC;
    tx_status &= ~CP_TX_STATUS_LNKF;
    tx_status &= ~CP_TX_STATUS_EXC;

    /* update ring data */
    val = cpu_to_le32(tx_status);
    pci_dma_write(d, cplus_tx_ring_desc, (uint8_t *)&val, 4);

    /* Now decide if descriptor being processed is holding the last segment of packet */
    if (txdw0 & CP_TX_LS)
    {
        uint8_t dot1q_buffer_space[VLAN_HLEN];
        uint16_t *dot1q_buffer;

        DPRINTF("+++ C+ Tx mode : descriptor %d is last segment descriptor\n",
            descriptor);

        /* can transfer fully assembled packet */

        uint8_t *saved_buffer  = s->cplus_txbuffer;
        int      saved_size    = s->cplus_txbuffer_offset;
        int      saved_buffer_len = s->cplus_txbuffer_len;

        /* create vlan tag */
        if (txdw1 & CP_TX_TAGC) {
            /* the vlan tag is in BE byte order in the descriptor
             * BE + le_to_cpu() + ~swap()~ = cpu */
            DPRINTF("+++ C+ Tx mode : inserting vlan tag with ""tci: %u\n",
                bswap16(txdw1 & CP_TX_VLAN_TAG_MASK));

            dot1q_buffer = (uint16_t *) dot1q_buffer_space;
            dot1q_buffer[0] = cpu_to_be16(ETH_P_VLAN);
            /* BE + le_to_cpu() + ~cpu_to_le()~ = BE */
            dot1q_buffer[1] = cpu_to_le16(txdw1 & CP_TX_VLAN_TAG_MASK);
        } else {
            dot1q_buffer = NULL;
        }

        /* reset the card space to protect from recursive call */
        s->cplus_txbuffer = NULL;
        s->cplus_txbuffer_offset = 0;
        s->cplus_txbuffer_len = 0;

        if (txdw0 & (CP_TX_IPCS | CP_TX_UDPCS | CP_TX_TCPCS | CP_TX_LGSEN))
        {
            DPRINTF("+++ C+ mode offloaded task checksum\n");

            /* Large enough for Ethernet and IP headers? */
            if (saved_size < ETH_HLEN + sizeof(struct ip_header)) {
                goto skip_offload;
            }

            /* ip packet header */
            struct ip_header *ip = NULL;
            int hlen = 0;
            uint8_t  ip_protocol = 0;
            uint16_t ip_data_len = 0;

            uint8_t *eth_payload_data = NULL;
            size_t   eth_payload_len  = 0;

            int proto = be16_to_cpu(*(uint16_t *)(saved_buffer + 12));
            if (proto != ETH_P_IP)
            {
                goto skip_offload;
            }

            DPRINTF("+++ C+ mode has IP packet\n");

            /* Note on memory alignment: eth_payload_data is 16-bit aligned
             * since saved_buffer is allocated with g_malloc() and ETH_HLEN is
             * even.  32-bit accesses must use ldl/stl wrappers to avoid
             * unaligned accesses.
             */
            eth_payload_data = saved_buffer + ETH_HLEN;
            eth_payload_len  = saved_size   - ETH_HLEN;

            ip = (struct ip_header*)eth_payload_data;

            if (IP_HEADER_VERSION(ip) != IP_HEADER_VERSION_4) {
                DPRINTF("+++ C+ mode packet has bad IP version %d "
                    "expected %d\n", IP_HEADER_VERSION(ip),
                    IP_HEADER_VERSION_4);
                goto skip_offload;
            }

            hlen = IP_HDR_GET_LEN(ip);
            if (hlen < sizeof(struct ip_header) || hlen > eth_payload_len) {
                goto skip_offload;
            }

            ip_protocol = ip->ip_p;

            ip_data_len = be16_to_cpu(ip->ip_len);
            if (ip_data_len < hlen || ip_data_len > eth_payload_len) {
                goto skip_offload;
            }
            ip_data_len -= hlen;

            if (!(txdw0 & CP_TX_LGSEN) && (txdw0 & CP_TX_IPCS))
            {
                DPRINTF("+++ C+ mode need IP checksum\n");

                ip->ip_sum = 0;
                ip->ip_sum = ip_checksum(ip, hlen);
                DPRINTF("+++ C+ mode IP header len=%d checksum=%04x\n",
                    hlen, ip->ip_sum);
            }

            if ((txdw0 & CP_TX_LGSEN) && ip_protocol == IP_PROTO_TCP)
            {
                /* Large enough for the TCP header? */
                if (ip_data_len < sizeof(tcp_header)) {
                    goto skip_offload;
                }

                int large_send_mss = (txdw0 >> CP_TC_LGSEN_MSS_SHIFT) &
                                     CP_TC_LGSEN_MSS_MASK;
                if (large_send_mss == 0) {
                    goto skip_offload;
                }

                DPRINTF("+++ C+ mode offloaded task TSO IP data %d "
                    "frame data %d specified MSS=%d\n",
                    ip_data_len, saved_size - ETH_HLEN, large_send_mss);

                int tcp_send_offset = 0;

                /* maximum IP header length is 60 bytes */
                uint8_t saved_ip_header[60];

                /* save IP header template; data area is used in tcp checksum calculation */
                memcpy(saved_ip_header, eth_payload_data, hlen);

                /* a placeholder for checksum calculation routine in tcp case */
                uint8_t *data_to_checksum     = eth_payload_data + hlen - 12;
                //                    size_t   data_to_checksum_len = eth_payload_len  - hlen + 12;

                /* pointer to TCP header */
                tcp_header *p_tcp_hdr = (tcp_header*)(eth_payload_data + hlen);

                int tcp_hlen = TCP_HEADER_DATA_OFFSET(p_tcp_hdr);

                /* Invalid TCP data offset? */
                if (tcp_hlen < sizeof(tcp_header) || tcp_hlen > ip_data_len) {
                    goto skip_offload;
                }

                int tcp_data_len = ip_data_len - tcp_hlen;

                DPRINTF("+++ C+ mode TSO IP data len %d TCP hlen %d TCP "
                    "data len %d\n", ip_data_len, tcp_hlen, tcp_data_len);

                /* note the cycle below overwrites IP header data,
                   but restores it from saved_ip_header before sending packet */

                int is_last_frame = 0;

                for (tcp_send_offset = 0; tcp_send_offset < tcp_data_len; tcp_send_offset += large_send_mss)
                {
                    uint16_t chunk_size = large_send_mss;

                    /* check if this is the last frame */
                    if (tcp_send_offset + large_send_mss >= tcp_data_len)
                    {
                        is_last_frame = 1;
                        chunk_size = tcp_data_len - tcp_send_offset;
                    }

                    DPRINTF("+++ C+ mode TSO TCP seqno %08x\n",
                            ldl_be_p(&p_tcp_hdr->th_seq));

                    /* add 4 TCP pseudoheader fields */
                    /* copy IP source and destination fields */
                    memcpy(data_to_checksum, saved_ip_header + 12, 8);

                    DPRINTF("+++ C+ mode TSO calculating TCP checksum for "
                        "packet with %d bytes data\n", tcp_hlen +
                        chunk_size);

                    if (tcp_send_offset)
                    {
                        memcpy((uint8_t*)p_tcp_hdr + tcp_hlen, (uint8_t*)p_tcp_hdr + tcp_hlen + tcp_send_offset, chunk_size);
                    }

                    /* keep PUSH and FIN flags only for the last frame */
                    if (!is_last_frame)
                    {
                        TCP_HEADER_CLEAR_FLAGS(p_tcp_hdr, TH_PUSH | TH_FIN);
                    }

                    /* recalculate TCP checksum */
                    ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *)data_to_checksum;
                    p_tcpip_hdr->zeros      = 0;
                    p_tcpip_hdr->ip_proto   = IP_PROTO_TCP;
                    p_tcpip_hdr->ip_payload = cpu_to_be16(tcp_hlen + chunk_size);

                    p_tcp_hdr->th_sum = 0;

                    int tcp_checksum = ip_checksum(data_to_checksum, tcp_hlen + chunk_size + 12);
                    DPRINTF("+++ C+ mode TSO TCP checksum %04x\n",
                        tcp_checksum);

                    p_tcp_hdr->th_sum = tcp_checksum;

                    /* restore IP header */
                    memcpy(eth_payload_data, saved_ip_header, hlen);

                    /* set IP data length and recalculate IP checksum */
                    ip->ip_len = cpu_to_be16(hlen + tcp_hlen + chunk_size);

                    /* increment IP id for subsequent frames */
                    ip->ip_id = cpu_to_be16(tcp_send_offset/large_send_mss + be16_to_cpu(ip->ip_id));

                    ip->ip_sum = 0;
                    ip->ip_sum = ip_checksum(eth_payload_data, hlen);
                    DPRINTF("+++ C+ mode TSO IP header len=%d "
                        "checksum=%04x\n", hlen, ip->ip_sum);

                    int tso_send_size = ETH_HLEN + hlen + tcp_hlen + chunk_size;
                    DPRINTF("+++ C+ mode TSO transferring packet size "
                        "%d\n", tso_send_size);
                    rtl8139_transfer_frame(s, saved_buffer, tso_send_size,
                        0, (uint8_t *) dot1q_buffer);

                    /* add transferred count to TCP sequence number */
                    stl_be_p(&p_tcp_hdr->th_seq,
                             chunk_size + ldl_be_p(&p_tcp_hdr->th_seq));
                }

                /* Stop sending this frame */
                saved_size = 0;
            }
            else if (!(txdw0 & CP_TX_LGSEN) && (txdw0 & (CP_TX_TCPCS|CP_TX_UDPCS)))
            {
                DPRINTF("+++ C+ mode need TCP or UDP checksum\n");

                /* maximum IP header length is 60 bytes */
                uint8_t saved_ip_header[60];
                memcpy(saved_ip_header, eth_payload_data, hlen);

                uint8_t *data_to_checksum     = eth_payload_data + hlen - 12;
                //                    size_t   data_to_checksum_len = eth_payload_len  - hlen + 12;

                /* add 4 TCP pseudoheader fields */
                /* copy IP source and destination fields */
                memcpy(data_to_checksum, saved_ip_header + 12, 8);

                if ((txdw0 & CP_TX_TCPCS) && ip_protocol == IP_PROTO_TCP)
                {
                    DPRINTF("+++ C+ mode calculating TCP checksum for "
                        "packet with %d bytes data\n", ip_data_len);

                    ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *)data_to_checksum;
                    p_tcpip_hdr->zeros      = 0;
                    p_tcpip_hdr->ip_proto   = IP_PROTO_TCP;
                    p_tcpip_hdr->ip_payload = cpu_to_be16(ip_data_len);

                    tcp_header* p_tcp_hdr = (tcp_header *) (data_to_checksum+12);

                    p_tcp_hdr->th_sum = 0;

                    int tcp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                    DPRINTF("+++ C+ mode TCP checksum %04x\n",
                        tcp_checksum);

                    p_tcp_hdr->th_sum = tcp_checksum;
                }
                else if ((txdw0 & CP_TX_UDPCS) && ip_protocol == IP_PROTO_UDP)
                {
                    DPRINTF("+++ C+ mode calculating UDP checksum for "
                        "packet with %d bytes data\n", ip_data_len);

                    ip_pseudo_header *p_udpip_hdr = (ip_pseudo_header *)data_to_checksum;
                    p_udpip_hdr->zeros      = 0;
                    p_udpip_hdr->ip_proto   = IP_PROTO_UDP;
                    p_udpip_hdr->ip_payload = cpu_to_be16(ip_data_len);

                    udp_header *p_udp_hdr = (udp_header *) (data_to_checksum+12);

                    p_udp_hdr->uh_sum = 0;

                    int udp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                    DPRINTF("+++ C+ mode UDP checksum %04x\n",
                        udp_checksum);

                    p_udp_hdr->uh_sum = udp_checksum;
                }

                /* restore IP header */
                memcpy(eth_payload_data, saved_ip_header, hlen);
            }
        }

skip_offload:
        /* update tally counter */
        ++s->tally_counters.TxOk;

        DPRINTF("+++ C+ mode transmitting %d bytes packet\n", saved_size);

        rtl8139_transfer_frame(s, saved_buffer, saved_size, 1,
            (uint8_t *) dot1q_buffer);

        /* restore card space if there was no recursion and reset offset */
        if (!s->cplus_txbuffer)
        {
            s->cplus_txbuffer        = saved_buffer;
            s->cplus_txbuffer_len    = saved_buffer_len;
            s->cplus_txbuffer_offset = 0;
        }
        else
        {
            g_free(saved_buffer);
        }
    }
    else
    {
        DPRINTF("+++ C+ mode transmission continue to next descriptor\n");
    }

    return 1;
}

static void rtl8139_cplus_transmit(RTL8139State *s)
{
    int txcount = 0;

    while (txcount < 64 && rtl8139_cplus_transmit_one(s))
    {
        ++txcount;
    }

    /* Mark transfer completed */
    if (!txcount)
    {
        DPRINTF("C+ mode : transmitter queue stalled, current TxDesc = %d\n",
            s->currCPlusTxDesc);
    }
    else
    {
        /* update interrupt status */
        s->IntrStatus |= TxOK;
        rtl8139_update_irq(s);
    }
}

static void rtl8139_transmit(RTL8139State *s)
{
    int descriptor = s->currTxDesc, txcount = 0;

    /*while*/
    if (rtl8139_transmit_one(s, descriptor))
    {
        ++s->currTxDesc;
        s->currTxDesc %= 4;
        ++txcount;
    }

    /* Mark transfer completed */
    if (!txcount)
    {
        DPRINTF("transmitter queue stalled, current TxDesc = %d\n",
            s->currTxDesc);
    }
}

static void rtl8139_TxStatus_write(RTL8139State *s, uint32_t txRegOffset, uint32_t val)
{

    int descriptor = txRegOffset/4;

    /* handle C+ transmit mode register configuration */

    if (s->cplus_enabled)
    {
        DPRINTF("RTL8139C+ DTCCR write offset=0x%x val=0x%08x "
            "descriptor=%d\n", txRegOffset, val, descriptor);

        /* handle Dump Tally Counters command */
        s->TxStatus[descriptor] = val;

        if (descriptor == 0 && (val & 0x8))
        {
            hwaddr tc_addr = rtl8139_addr64(s->TxStatus[0] & ~0x3f, s->TxStatus[1]);

            /* dump tally counters to specified memory location */
            RTL8139TallyCounters_dma_write(s, tc_addr);

            /* mark dump completed */
            s->TxStatus[0] &= ~0x8;
        }

        return;
    }

    DPRINTF("TxStatus write offset=0x%x val=0x%08x descriptor=%d\n",
        txRegOffset, val, descriptor);

    /* mask only reserved bits */
    val &= ~0xff00c000; /* these bits are reset on write */
    val = SET_MASKED(val, 0x00c00000, s->TxStatus[descriptor]);

    s->TxStatus[descriptor] = val;

    /* attempt to start transmission */
    rtl8139_transmit(s);
}

static uint32_t rtl8139_TxStatus_TxAddr_read(RTL8139State *s, uint32_t regs[],
                                             uint32_t base, uint8_t addr,
                                             int size)
{
    uint32_t reg = (addr - base) / 4;
    uint32_t offset = addr & 0x3;
    uint32_t ret = 0;

    if (addr & (size - 1)) {
        DPRINTF("not implemented read for TxStatus/TxAddr "
                "addr=0x%x size=0x%x\n", addr, size);
        return ret;
    }

    switch (size) {
    case 1: /* fall through */
    case 2: /* fall through */
    case 4:
        ret = (regs[reg] >> offset * 8) & (((uint64_t)1 << (size * 8)) - 1);
        DPRINTF("TxStatus/TxAddr[%d] read addr=0x%x size=0x%x val=0x%08x\n",
                reg, addr, size, ret);
        break;
    default:
        DPRINTF("unsupported size 0x%x of TxStatus/TxAddr reading\n", size);
        break;
    }

    return ret;
}

static uint16_t rtl8139_TSAD_read(RTL8139State *s)
{
    uint16_t ret = 0;

    /* Simulate TSAD, it is read only anyway */

    ret = ((s->TxStatus[3] & TxStatOK  )?TSAD_TOK3:0)
         |((s->TxStatus[2] & TxStatOK  )?TSAD_TOK2:0)
         |((s->TxStatus[1] & TxStatOK  )?TSAD_TOK1:0)
         |((s->TxStatus[0] & TxStatOK  )?TSAD_TOK0:0)

         |((s->TxStatus[3] & TxUnderrun)?TSAD_TUN3:0)
         |((s->TxStatus[2] & TxUnderrun)?TSAD_TUN2:0)
         |((s->TxStatus[1] & TxUnderrun)?TSAD_TUN1:0)
         |((s->TxStatus[0] & TxUnderrun)?TSAD_TUN0:0)

         |((s->TxStatus[3] & TxAborted )?TSAD_TABT3:0)
         |((s->TxStatus[2] & TxAborted )?TSAD_TABT2:0)
         |((s->TxStatus[1] & TxAborted )?TSAD_TABT1:0)
         |((s->TxStatus[0] & TxAborted )?TSAD_TABT0:0)

         |((s->TxStatus[3] & TxHostOwns )?TSAD_OWN3:0)
         |((s->TxStatus[2] & TxHostOwns )?TSAD_OWN2:0)
         |((s->TxStatus[1] & TxHostOwns )?TSAD_OWN1:0)
         |((s->TxStatus[0] & TxHostOwns )?TSAD_OWN0:0) ;


    DPRINTF("TSAD read val=0x%04x\n", ret);

    return ret;
}

static uint16_t rtl8139_CSCR_read(RTL8139State *s)
{
    uint16_t ret = s->CSCR;

    DPRINTF("CSCR read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_TxAddr_write(RTL8139State *s, uint32_t txAddrOffset, uint32_t val)
{
    DPRINTF("TxAddr write offset=0x%x val=0x%08x\n", txAddrOffset, val);

    s->TxAddr[txAddrOffset/4] = val;
}

static uint32_t rtl8139_TxAddr_read(RTL8139State *s, uint32_t txAddrOffset)
{
    uint32_t ret = s->TxAddr[txAddrOffset/4];

    DPRINTF("TxAddr read offset=0x%x val=0x%08x\n", txAddrOffset, ret);

    return ret;
}

static void rtl8139_RxBufPtr_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("RxBufPtr write val=0x%04x\n", val);

    /* this value is off by 16 */
    s->RxBufPtr = MOD2(val + 0x10, s->RxBufferSize);

    /* more buffer space may be available so try to receive */
    qemu_flush_queued_packets(qemu_get_queue(s->nic));

    DPRINTF(" CAPR write: rx buffer length %d head 0x%04x read 0x%04x\n",
        s->RxBufferSize, s->RxBufAddr, s->RxBufPtr);
}

static uint32_t rtl8139_RxBufPtr_read(RTL8139State *s)
{
    /* this value is off by 16 */
    uint32_t ret = s->RxBufPtr - 0x10;

    DPRINTF("RxBufPtr read val=0x%04x\n", ret);

    return ret;
}

static uint32_t rtl8139_RxBufAddr_read(RTL8139State *s)
{
    /* this value is NOT off by 16 */
    uint32_t ret = s->RxBufAddr;

    DPRINTF("RxBufAddr read val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_RxBuf_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("RxBuf write val=0x%08x\n", val);

    s->RxBuf = val;

    /* may need to reset rxring here */
}

static uint32_t rtl8139_RxBuf_read(RTL8139State *s)
{
    uint32_t ret = s->RxBuf;

    DPRINTF("RxBuf read val=0x%08x\n", ret);

    return ret;
}

static void rtl8139_IntrMask_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("IntrMask write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0x1e00, s->IntrMask);

    s->IntrMask = val;

    rtl8139_update_irq(s);

}

static uint32_t rtl8139_IntrMask_read(RTL8139State *s)
{
    uint32_t ret = s->IntrMask;

    DPRINTF("IntrMask read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_IntrStatus_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("IntrStatus write(w) val=0x%04x\n", val);

#if 0

    /* writing to ISR has no effect */

    return;

#else
    uint16_t newStatus = s->IntrStatus & ~val;

    /* mask unwritable bits */
    newStatus = SET_MASKED(newStatus, 0x1e00, s->IntrStatus);

    /* writing 1 to interrupt status register bit clears it */
    s->IntrStatus = 0;
    rtl8139_update_irq(s);

    s->IntrStatus = newStatus;
    rtl8139_set_next_tctr_time(s);
    rtl8139_update_irq(s);

#endif
}

static uint32_t rtl8139_IntrStatus_read(RTL8139State *s)
{
    uint32_t ret = s->IntrStatus;

    DPRINTF("IntrStatus read(w) val=0x%04x\n", ret);

#if 0

    /* reading ISR clears all interrupts */
    s->IntrStatus = 0;

    rtl8139_update_irq(s);

#endif

    return ret;
}

static void rtl8139_MultiIntr_write(RTL8139State *s, uint32_t val)
{
    DPRINTF("MultiIntr write(w) val=0x%04x\n", val);

    /* mask unwritable bits */
    val = SET_MASKED(val, 0xf000, s->MultiIntr);

    s->MultiIntr = val;
}

static uint32_t rtl8139_MultiIntr_read(RTL8139State *s)
{
    uint32_t ret = s->MultiIntr;

    DPRINTF("MultiIntr read(w) val=0x%04x\n", ret);

    return ret;
}

static void rtl8139_io_writeb(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    switch (addr)
    {
        case MAC0 ... MAC0+4:
            s->phys[addr - MAC0] = val;
            break;
        case MAC0+5:
            s->phys[addr - MAC0] = val;
            qemu_format_nic_info_str(qemu_get_queue(s->nic), s->phys);
            break;
        case MAC0+6 ... MAC0+7:
            /* reserved */
            break;
        case MAR0 ... MAR0+7:
            s->mult[addr - MAR0] = val;
            break;
        case ChipCmd:
            rtl8139_ChipCmd_write(s, val);
            break;
        case Cfg9346:
            rtl8139_Cfg9346_write(s, val);
            break;
        case TxConfig: /* windows driver sometimes writes using byte-lenth call */
            rtl8139_TxConfig_writeb(s, val);
            break;
        case Config0:
            rtl8139_Config0_write(s, val);
            break;
        case Config1:
            rtl8139_Config1_write(s, val);
            break;
        case Config3:
            rtl8139_Config3_write(s, val);
            break;
        case Config4:
            rtl8139_Config4_write(s, val);
            break;
        case Config5:
            rtl8139_Config5_write(s, val);
            break;
        case MediaStatus:
            /* ignore */
            DPRINTF("not implemented write(b) to MediaStatus val=0x%02x\n",
                val);
            break;

        case HltClk:
            DPRINTF("HltClk write val=0x%08x\n", val);
            if (val == 'R')
            {
                s->clock_enabled = 1;
            }
            else if (val == 'H')
            {
                s->clock_enabled = 0;
            }
            break;

        case TxThresh:
            DPRINTF("C+ TxThresh write(b) val=0x%02x\n", val);
            s->TxThresh = val;
            break;

        case TxPoll:
            DPRINTF("C+ TxPoll write(b) val=0x%02x\n", val);
            if (val & (1 << 7))
            {
                DPRINTF("C+ TxPoll high priority transmission (not "
                    "implemented)\n");
                //rtl8139_cplus_transmit(s);
            }
            if (val & (1 << 6))
            {
                DPRINTF("C+ TxPoll normal priority transmission\n");
                rtl8139_cplus_transmit(s);
            }

            break;
        case RxConfig:
            DPRINTF("RxConfig write(b) val=0x%02x\n", val);
            rtl8139_RxConfig_write(s,
                (rtl8139_RxConfig_read(s) & 0xFFFFFF00) | val);
            break;
        default:
            DPRINTF("not implemented write(b) addr=0x%x val=0x%02x\n", addr,
                val);
            break;
    }
}

static void rtl8139_io_writew(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    switch (addr)
    {
        case IntrMask:
            rtl8139_IntrMask_write(s, val);
            break;

        case IntrStatus:
            rtl8139_IntrStatus_write(s, val);
            break;

        case MultiIntr:
            rtl8139_MultiIntr_write(s, val);
            break;

        case RxBufPtr:
            rtl8139_RxBufPtr_write(s, val);
            break;

        case BasicModeCtrl:
            rtl8139_BasicModeCtrl_write(s, val);
            break;
        case BasicModeStatus:
            rtl8139_BasicModeStatus_write(s, val);
            break;
        case NWayAdvert:
            DPRINTF("NWayAdvert write(w) val=0x%04x\n", val);
            s->NWayAdvert = val;
            break;
        case NWayLPAR:
            DPRINTF("forbidden NWayLPAR write(w) val=0x%04x\n", val);
            break;
        case NWayExpansion:
            DPRINTF("NWayExpansion write(w) val=0x%04x\n", val);
            s->NWayExpansion = val;
            break;

        case CpCmd:
            rtl8139_CpCmd_write(s, val);
            break;

        case IntrMitigate:
            rtl8139_IntrMitigate_write(s, val);
            break;

        default:
            DPRINTF("ioport write(w) addr=0x%x val=0x%04x via write(b)\n",
                addr, val);

            rtl8139_io_writeb(opaque, addr, val & 0xff);
            rtl8139_io_writeb(opaque, addr + 1, (val >> 8) & 0xff);
            break;
    }
}

static void rtl8139_set_next_tctr_time(RTL8139State *s)
{
    const uint64_t ns_per_period = (uint64_t)PCI_PERIOD << 32;

    DPRINTF("entered rtl8139_set_next_tctr_time\n");

    /* This function is called at least once per period, so it is a good
     * place to update the timer base.
     *
     * After one iteration of this loop the value in the Timer register does
     * not change, but the device model is counting up by 2^32 ticks (approx.
     * 130 seconds).
     */
    while (s->TCTR_base + ns_per_period <= qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
        s->TCTR_base += ns_per_period;
    }

    if (!s->TimerInt) {
        timer_del(s->timer);
    } else {
        uint64_t delta = (uint64_t)s->TimerInt * PCI_PERIOD;
        if (s->TCTR_base + delta <= qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
            delta += ns_per_period;
        }
        timer_mod(s->timer, s->TCTR_base + delta);
    }
}

static void rtl8139_io_writel(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    switch (addr)
    {
        case RxMissed:
            DPRINTF("RxMissed clearing on write\n");
            s->RxMissed = 0;
            break;

        case TxConfig:
            rtl8139_TxConfig_write(s, val);
            break;

        case RxConfig:
            rtl8139_RxConfig_write(s, val);
            break;

        case TxStatus0 ... TxStatus0+4*4-1:
            rtl8139_TxStatus_write(s, addr-TxStatus0, val);
            break;

        case TxAddr0 ... TxAddr0+4*4-1:
            rtl8139_TxAddr_write(s, addr-TxAddr0, val);
            break;

        case RxBuf:
            rtl8139_RxBuf_write(s, val);
            break;

        case RxRingAddrLO:
            DPRINTF("C+ RxRing low bits write val=0x%08x\n", val);
            s->RxRingAddrLO = val;
            break;

        case RxRingAddrHI:
            DPRINTF("C+ RxRing high bits write val=0x%08x\n", val);
            s->RxRingAddrHI = val;
            break;

        case Timer:
            DPRINTF("TCTR Timer reset on write\n");
            s->TCTR_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            rtl8139_set_next_tctr_time(s);
            break;

        case FlashReg:
            DPRINTF("FlashReg TimerInt write val=0x%08x\n", val);
            if (s->TimerInt != val) {
                s->TimerInt = val;
                rtl8139_set_next_tctr_time(s);
            }
            break;

        default:
            DPRINTF("ioport write(l) addr=0x%x val=0x%08x via write(b)\n",
                addr, val);
            rtl8139_io_writeb(opaque, addr, val & 0xff);
            rtl8139_io_writeb(opaque, addr + 1, (val >> 8) & 0xff);
            rtl8139_io_writeb(opaque, addr + 2, (val >> 16) & 0xff);
            rtl8139_io_writeb(opaque, addr + 3, (val >> 24) & 0xff);
            break;
    }
}

static uint32_t rtl8139_io_readb(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    int ret;

    switch (addr)
    {
        case MAC0 ... MAC0+5:
            ret = s->phys[addr - MAC0];
            break;
        case MAC0+6 ... MAC0+7:
            ret = 0;
            break;
        case MAR0 ... MAR0+7:
            ret = s->mult[addr - MAR0];
            break;
        case TxStatus0 ... TxStatus0+4*4-1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxStatus, TxStatus0,
                                               addr, 1);
            break;
        case ChipCmd:
            ret = rtl8139_ChipCmd_read(s);
            break;
        case Cfg9346:
            ret = rtl8139_Cfg9346_read(s);
            break;
        case Config0:
            ret = rtl8139_Config0_read(s);
            break;
        case Config1:
            ret = rtl8139_Config1_read(s);
            break;
        case Config3:
            ret = rtl8139_Config3_read(s);
            break;
        case Config4:
            ret = rtl8139_Config4_read(s);
            break;
        case Config5:
            ret = rtl8139_Config5_read(s);
            break;

        case MediaStatus:
            /* The LinkDown bit of MediaStatus is inverse with link status */
            ret = 0xd0 | (~s->BasicModeStatus & 0x04);
            DPRINTF("MediaStatus read 0x%x\n", ret);
            break;

        case HltClk:
            ret = s->clock_enabled;
            DPRINTF("HltClk read 0x%x\n", ret);
            break;

        case PCIRevisionID:
            ret = RTL8139_PCI_REVID;
            DPRINTF("PCI Revision ID read 0x%x\n", ret);
            break;

        case TxThresh:
            ret = s->TxThresh;
            DPRINTF("C+ TxThresh read(b) val=0x%02x\n", ret);
            break;

        case 0x43: /* Part of TxConfig register. Windows driver tries to read it */
            ret = s->TxConfig >> 24;
            DPRINTF("RTL8139C TxConfig at 0x43 read(b) val=0x%02x\n", ret);
            break;

        default:
            DPRINTF("not implemented read(b) addr=0x%x\n", addr);
            ret = 0;
            break;
    }

    return ret;
}

static uint32_t rtl8139_io_readw(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    uint32_t ret;

    switch (addr)
    {
        case TxAddr0 ... TxAddr0+4*4-1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxAddr, TxAddr0, addr, 2);
            break;
        case IntrMask:
            ret = rtl8139_IntrMask_read(s);
            break;

        case IntrStatus:
            ret = rtl8139_IntrStatus_read(s);
            break;

        case MultiIntr:
            ret = rtl8139_MultiIntr_read(s);
            break;

        case RxBufPtr:
            ret = rtl8139_RxBufPtr_read(s);
            break;

        case RxBufAddr:
            ret = rtl8139_RxBufAddr_read(s);
            break;

        case BasicModeCtrl:
            ret = rtl8139_BasicModeCtrl_read(s);
            break;
        case BasicModeStatus:
            ret = rtl8139_BasicModeStatus_read(s);
            break;
        case NWayAdvert:
            ret = s->NWayAdvert;
            DPRINTF("NWayAdvert read(w) val=0x%04x\n", ret);
            break;
        case NWayLPAR:
            ret = s->NWayLPAR;
            DPRINTF("NWayLPAR read(w) val=0x%04x\n", ret);
            break;
        case NWayExpansion:
            ret = s->NWayExpansion;
            DPRINTF("NWayExpansion read(w) val=0x%04x\n", ret);
            break;

        case CpCmd:
            ret = rtl8139_CpCmd_read(s);
            break;

        case IntrMitigate:
            ret = rtl8139_IntrMitigate_read(s);
            break;

        case TxSummary:
            ret = rtl8139_TSAD_read(s);
            break;

        case CSCR:
            ret = rtl8139_CSCR_read(s);
            break;

        default:
            DPRINTF("ioport read(w) addr=0x%x via read(b)\n", addr);

            ret  = rtl8139_io_readb(opaque, addr);
            ret |= rtl8139_io_readb(opaque, addr + 1) << 8;

            DPRINTF("ioport read(w) addr=0x%x val=0x%04x\n", addr, ret);
            break;
    }

    return ret;
}

static uint32_t rtl8139_io_readl(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    uint32_t ret;

    switch (addr)
    {
        case RxMissed:
            ret = s->RxMissed;

            DPRINTF("RxMissed read val=0x%08x\n", ret);
            break;

        case TxConfig:
            ret = rtl8139_TxConfig_read(s);
            break;

        case RxConfig:
            ret = rtl8139_RxConfig_read(s);
            break;

        case TxStatus0 ... TxStatus0+4*4-1:
            ret = rtl8139_TxStatus_TxAddr_read(s, s->TxStatus, TxStatus0,
                                               addr, 4);
            break;

        case TxAddr0 ... TxAddr0+4*4-1:
            ret = rtl8139_TxAddr_read(s, addr-TxAddr0);
            break;

        case RxBuf:
            ret = rtl8139_RxBuf_read(s);
            break;

        case RxRingAddrLO:
            ret = s->RxRingAddrLO;
            DPRINTF("C+ RxRing low bits read val=0x%08x\n", ret);
            break;

        case RxRingAddrHI:
            ret = s->RxRingAddrHI;
            DPRINTF("C+ RxRing high bits read val=0x%08x\n", ret);
            break;

        case Timer:
            ret = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->TCTR_base) /
                  PCI_PERIOD;
            DPRINTF("TCTR Timer read val=0x%08x\n", ret);
            break;

        case FlashReg:
            ret = s->TimerInt;
            DPRINTF("FlashReg TimerInt read val=0x%08x\n", ret);
            break;

        default:
            DPRINTF("ioport read(l) addr=0x%x via read(b)\n", addr);

            ret  = rtl8139_io_readb(opaque, addr);
            ret |= rtl8139_io_readb(opaque, addr + 1) << 8;
            ret |= rtl8139_io_readb(opaque, addr + 2) << 16;
            ret |= rtl8139_io_readb(opaque, addr + 3) << 24;

            DPRINTF("read(l) addr=0x%x val=%08x\n", addr, ret);
            break;
    }

    return ret;
}

/* */

static int rtl8139_post_load(void *opaque, int version_id)
{
    RTL8139State* s = opaque;
    rtl8139_set_next_tctr_time(s);
    if (version_id < 4) {
        s->cplus_enabled = s->CpCmd != 0;
    }

    /* nc.link_down can't be migrated, so infer link_down according
     * to link status bit in BasicModeStatus */
    qemu_get_queue(s->nic)->link_down = (s->BasicModeStatus & 0x04) == 0;

    return 0;
}

static bool rtl8139_hotplug_ready_needed(void *opaque)
{
    return qdev_machine_modified();
}

static const VMStateDescription vmstate_rtl8139_hotplug_ready ={
    .name = "rtl8139/hotplug_ready",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = rtl8139_hotplug_ready_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static int rtl8139_pre_save(void *opaque)
{
    RTL8139State* s = opaque;
    int64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* for migration to older versions */
    s->TCTR = (current_time - s->TCTR_base) / PCI_PERIOD;
    s->rtl8139_mmio_io_addr_dummy = 0;

    return 0;
}

static const VMStateDescription vmstate_rtl8139 = {
    .name = "rtl8139",
    .version_id = 5,
    .minimum_version_id = 3,
    .post_load = rtl8139_post_load,
    .pre_save  = rtl8139_pre_save,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, RTL8139State),
        VMSTATE_PARTIAL_BUFFER(phys, RTL8139State, 6),
        VMSTATE_BUFFER(mult, RTL8139State),
        VMSTATE_UINT32_ARRAY(TxStatus, RTL8139State, 4),
        VMSTATE_UINT32_ARRAY(TxAddr, RTL8139State, 4),

        VMSTATE_UINT32(RxBuf, RTL8139State),
        VMSTATE_UINT32(RxBufferSize, RTL8139State),
        VMSTATE_UINT32(RxBufPtr, RTL8139State),
        VMSTATE_UINT32(RxBufAddr, RTL8139State),

        VMSTATE_UINT16(IntrStatus, RTL8139State),
        VMSTATE_UINT16(IntrMask, RTL8139State),

        VMSTATE_UINT32(TxConfig, RTL8139State),
        VMSTATE_UINT32(RxConfig, RTL8139State),
        VMSTATE_UINT32(RxMissed, RTL8139State),
        VMSTATE_UINT16(CSCR, RTL8139State),

        VMSTATE_UINT8(Cfg9346, RTL8139State),
        VMSTATE_UINT8(Config0, RTL8139State),
        VMSTATE_UINT8(Config1, RTL8139State),
        VMSTATE_UINT8(Config3, RTL8139State),
        VMSTATE_UINT8(Config4, RTL8139State),
        VMSTATE_UINT8(Config5, RTL8139State),

        VMSTATE_UINT8(clock_enabled, RTL8139State),
        VMSTATE_UINT8(bChipCmdState, RTL8139State),

        VMSTATE_UINT16(MultiIntr, RTL8139State),

        VMSTATE_UINT16(BasicModeCtrl, RTL8139State),
        VMSTATE_UINT16(BasicModeStatus, RTL8139State),
        VMSTATE_UINT16(NWayAdvert, RTL8139State),
        VMSTATE_UINT16(NWayLPAR, RTL8139State),
        VMSTATE_UINT16(NWayExpansion, RTL8139State),

        VMSTATE_UINT16(CpCmd, RTL8139State),
        VMSTATE_UINT8(TxThresh, RTL8139State),

        VMSTATE_UNUSED(4),
        VMSTATE_MACADDR(conf.macaddr, RTL8139State),
        VMSTATE_INT32(rtl8139_mmio_io_addr_dummy, RTL8139State),

        VMSTATE_UINT32(currTxDesc, RTL8139State),
        VMSTATE_UINT32(currCPlusRxDesc, RTL8139State),
        VMSTATE_UINT32(currCPlusTxDesc, RTL8139State),
        VMSTATE_UINT32(RxRingAddrLO, RTL8139State),
        VMSTATE_UINT32(RxRingAddrHI, RTL8139State),

        VMSTATE_UINT16_ARRAY(eeprom.contents, RTL8139State, EEPROM_9346_SIZE),
        VMSTATE_INT32(eeprom.mode, RTL8139State),
        VMSTATE_UINT32(eeprom.tick, RTL8139State),
        VMSTATE_UINT8(eeprom.address, RTL8139State),
        VMSTATE_UINT16(eeprom.input, RTL8139State),
        VMSTATE_UINT16(eeprom.output, RTL8139State),

        VMSTATE_UINT8(eeprom.eecs, RTL8139State),
        VMSTATE_UINT8(eeprom.eesk, RTL8139State),
        VMSTATE_UINT8(eeprom.eedi, RTL8139State),
        VMSTATE_UINT8(eeprom.eedo, RTL8139State),

        VMSTATE_UINT32(TCTR, RTL8139State),
        VMSTATE_UINT32(TimerInt, RTL8139State),
        VMSTATE_INT64(TCTR_base, RTL8139State),

        VMSTATE_UINT64(tally_counters.TxOk, RTL8139State),
        VMSTATE_UINT64(tally_counters.RxOk, RTL8139State),
        VMSTATE_UINT64(tally_counters.TxERR, RTL8139State),
        VMSTATE_UINT32(tally_counters.RxERR, RTL8139State),
        VMSTATE_UINT16(tally_counters.MissPkt, RTL8139State),
        VMSTATE_UINT16(tally_counters.FAE, RTL8139State),
        VMSTATE_UINT32(tally_counters.Tx1Col, RTL8139State),
        VMSTATE_UINT32(tally_counters.TxMCol, RTL8139State),
        VMSTATE_UINT64(tally_counters.RxOkPhy, RTL8139State),
        VMSTATE_UINT64(tally_counters.RxOkBrd, RTL8139State),
        VMSTATE_UINT32_V(tally_counters.RxOkMul, RTL8139State, 5),
        VMSTATE_UINT16(tally_counters.TxAbt, RTL8139State),
        VMSTATE_UINT16(tally_counters.TxUndrn, RTL8139State),

        VMSTATE_UINT32_V(cplus_enabled, RTL8139State, 4),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_rtl8139_hotplug_ready,
        NULL
    }
};

/***********************************************************/
/* PCI RTL8139 definitions */

static void rtl8139_ioport_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    switch (size) {
    case 1:
        rtl8139_io_writeb(opaque, addr, val);
        break;
    case 2:
        rtl8139_io_writew(opaque, addr, val);
        break;
    case 4:
        rtl8139_io_writel(opaque, addr, val);
        break;
    }
}

static uint64_t rtl8139_ioport_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    switch (size) {
    case 1:
        return rtl8139_io_readb(opaque, addr);
    case 2:
        return rtl8139_io_readw(opaque, addr);
    case 4:
        return rtl8139_io_readl(opaque, addr);
    }

    return -1;
}

static const MemoryRegionOps rtl8139_io_ops = {
    .read = rtl8139_ioport_read,
    .write = rtl8139_ioport_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rtl8139_timer(void *opaque)
{
    RTL8139State *s = opaque;

    if (!s->clock_enabled)
    {
        DPRINTF(">>> timer: clock is not running\n");
        return;
    }

    s->IntrStatus |= PCSTimeout;
    rtl8139_update_irq(s);
    rtl8139_set_next_tctr_time(s);
}

static void pci_rtl8139_uninit(PCIDevice *dev)
{
    RTL8139State *s = RTL8139(dev);

    g_free(s->cplus_txbuffer);
    s->cplus_txbuffer = NULL;
    timer_free(s->timer);
    qemu_del_nic(s->nic);
}

static void rtl8139_set_link_status(NetClientState *nc)
{
    RTL8139State *s = qemu_get_nic_opaque(nc);

    if (nc->link_down) {
        s->BasicModeStatus &= ~0x04;
    } else {
        s->BasicModeStatus |= 0x04;
    }

    s->IntrStatus |= RxUnderrun;
    rtl8139_update_irq(s);
}

static NetClientInfo net_rtl8139_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = rtl8139_can_receive,
    .receive = rtl8139_receive,
    .link_status_changed = rtl8139_set_link_status,
};

static void pci_rtl8139_realize(PCIDevice *dev, Error **errp)
{
    RTL8139State *s = RTL8139(dev);
    DeviceState *d = DEVICE(dev);
    uint8_t *pci_conf;

    pci_conf = dev->config;
    pci_conf[PCI_INTERRUPT_PIN] = 1;    /* interrupt pin A */
    /* TODO: start of capability list, but no capability
     * list bit in status register, and offset 0xdc seems unused. */
    pci_conf[PCI_CAPABILITY_LIST] = 0xdc;

    memory_region_init_io(&s->bar_io, OBJECT(s), &rtl8139_io_ops, s,
                          "rtl8139", 0x100);
    memory_region_init_alias(&s->bar_mem, OBJECT(s), "rtl8139-mem", &s->bar_io,
                             0, 0x100);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_io);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_mem);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    /* prepare eeprom */
    s->eeprom.contents[0] = 0x8129;
#if 1
    /* PCI vendor and device ID should be mirrored here */
    s->eeprom.contents[1] = PCI_VENDOR_ID_REALTEK;
    s->eeprom.contents[2] = PCI_DEVICE_ID_REALTEK_8139;
#endif
    s->eeprom.contents[7] = s->conf.macaddr.a[0] | s->conf.macaddr.a[1] << 8;
    s->eeprom.contents[8] = s->conf.macaddr.a[2] | s->conf.macaddr.a[3] << 8;
    s->eeprom.contents[9] = s->conf.macaddr.a[4] | s->conf.macaddr.a[5] << 8;

    s->nic = qemu_new_nic(&net_rtl8139_info, &s->conf,
                          object_get_typename(OBJECT(dev)), d->id,
                          &d->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    s->cplus_txbuffer = NULL;
    s->cplus_txbuffer_len = 0;
    s->cplus_txbuffer_offset = 0;

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, rtl8139_timer, s);
}

static void rtl8139_instance_init(Object *obj)
{
    RTL8139State *s = RTL8139(obj);

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static Property rtl8139_properties[] = {
    DEFINE_NIC_PROPERTIES(RTL8139State, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void rtl8139_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_rtl8139_realize;
    k->exit = pci_rtl8139_uninit;
    k->romfile = "efi-rtl8139.rom";
    k->vendor_id = PCI_VENDOR_ID_REALTEK;
    k->device_id = PCI_DEVICE_ID_REALTEK_8139;
    k->revision = RTL8139_PCI_REVID; /* >=0x20 is for 8139C+ */
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->reset = rtl8139_reset;
    dc->vmsd = &vmstate_rtl8139;
    device_class_set_props(dc, rtl8139_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo rtl8139_info = {
    .name          = TYPE_RTL8139,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RTL8139State),
    .class_init    = rtl8139_class_init,
    .instance_init = rtl8139_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void rtl8139_register_types(void)
{
    type_register_static(&rtl8139_info);
}

type_init(rtl8139_register_types)
