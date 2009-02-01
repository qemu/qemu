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
 */

#include "hw.h"
#include "pci.h"
#include "qemu-timer.h"
#include "net.h"

/* debug RTL8139 card */
//#define DEBUG_RTL8139 1

#define PCI_FREQUENCY 33000000L

/* debug RTL8139 card C+ mode only */
//#define DEBUG_RTL8139CP 1

/* Calculate CRCs properly on Rx packets */
#define RTL8139_CALCULATE_RXCRC 1

/* Uncomment to enable on-board timer interrupts */
//#define RTL8139_ONBOARD_TIMER 1

#if defined(RTL8139_CALCULATE_RXCRC)
/* For crc32 */
#include <zlib.h>
#endif

#define SET_MASKED(input, mask, curr) \
    ( ( (input) & ~(mask) ) | ( (curr) & (mask) ) )

/* arg % size for size which is a power of 2 */
#define MOD2(input, size) \
    ( ( input ) & ( size - 1 )  )

#if defined (DEBUG_RTL8139)
#  define DEBUG_PRINT(x) do { printf x ; } while (0)
#else
#  define DEBUG_PRINT(x)
#endif

/* Symbolic offsets to registers. */
enum RTL8139_registers {
    MAC0 = 0,        /* Ethernet hardware address. */
    MAR0 = 8,        /* Multicast filter. */
    TxStatus0 = 0x10,/* Transmit status (Four 32bit registers). C mode only */
                     /* Dump Tally Conter control register(64bit). C+ mode only */
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
    RxUnderrun = 0x20,
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
    Cfg9346_Lock = 0x00,
    Cfg9346_Unlock = 0xC0,
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

/* Writes tally counters to specified physical memory address */
static void RTL8139TallyCounters_physical_memory_write(target_phys_addr_t tc_addr, RTL8139TallyCounters* counters);

/* Loads values of tally counters from VM state file */
static void RTL8139TallyCounters_load(QEMUFile* f, RTL8139TallyCounters *tally_counters);

/* Saves values of tally counters to VM state file */
static void RTL8139TallyCounters_save(QEMUFile* f, RTL8139TallyCounters *tally_counters);

typedef struct RTL8139State {
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

    PCIDevice *pci_dev;
    VLANClientState *vc;
    uint8_t macaddr[6];
    int rtl8139_mmio_io_addr;

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

} RTL8139State;

static void prom9346_decode_command(EEprom9346 *eeprom, uint8_t command)
{
    DEBUG_PRINT(("RTL8139: eeprom command 0x%02x\n", command));

    switch (command & Chip9346_op_mask)
    {
        case Chip9346_op_read:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->output = eeprom->contents[eeprom->address];
            eeprom->eedo = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_data_read;
            DEBUG_PRINT(("RTL8139: eeprom read from address 0x%02x data=0x%04x\n",
                   eeprom->address, eeprom->output));
        }
        break;

        case Chip9346_op_write:
        {
            eeprom->address = command & EEPROM_9346_ADDR_MASK;
            eeprom->input = 0;
            eeprom->tick = 0;
            eeprom->mode = Chip9346_none; /* Chip9346_data_write */
            DEBUG_PRINT(("RTL8139: eeprom begin write to address 0x%02x\n",
                   eeprom->address));
        }
        break;
        default:
            eeprom->mode = Chip9346_none;
            switch (command & Chip9346_op_ext_mask)
            {
                case Chip9346_op_write_enable:
                    DEBUG_PRINT(("RTL8139: eeprom write enabled\n"));
                    break;
                case Chip9346_op_write_all:
                    DEBUG_PRINT(("RTL8139: eeprom begin write all\n"));
                    break;
                case Chip9346_op_write_disable:
                    DEBUG_PRINT(("RTL8139: eeprom write disabled\n"));
                    break;
            }
            break;
    }
}

static void prom9346_shift_clock(EEprom9346 *eeprom)
{
    int bit = eeprom->eedi?1:0;

    ++ eeprom->tick;

    DEBUG_PRINT(("eeprom: tick %d eedi=%d eedo=%d\n", eeprom->tick, eeprom->eedi, eeprom->eedo));

    switch (eeprom->mode)
    {
        case Chip9346_enter_command_mode:
            if (bit)
            {
                eeprom->mode = Chip9346_read_command;
                eeprom->tick = 0;
                eeprom->input = 0;
                DEBUG_PRINT(("eeprom: +++ synchronized, begin command read\n"));
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

                DEBUG_PRINT(("eeprom: +++ end of read, awaiting next command\n"));
#else
        // original behaviour
                ++eeprom->address;
                eeprom->address &= EEPROM_9346_ADDR_MASK;
                eeprom->output = eeprom->contents[eeprom->address];
                eeprom->tick = 0;

                DEBUG_PRINT(("eeprom: +++ read next address 0x%02x data=0x%04x\n",
                       eeprom->address, eeprom->output));
#endif
            }
            break;

        case Chip9346_data_write:
            eeprom->input = (eeprom->input << 1) | (bit & 1);
            if (eeprom->tick == 16)
            {
                DEBUG_PRINT(("RTL8139: eeprom write to address 0x%02x data=0x%04x\n",
                       eeprom->address, eeprom->input));

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
                DEBUG_PRINT(("RTL8139: eeprom filled with data=0x%04x\n",
                       eeprom->input));

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

    DEBUG_PRINT(("eeprom: +++ wires CS=%d SK=%d DI=%d DO=%d\n",
                 eeprom->eecs, eeprom->eesk, eeprom->eedi, eeprom->eedo));

    if (!old_eecs && eecs)
    {
        /* Synchronize start */
        eeprom->tick = 0;
        eeprom->input = 0;
        eeprom->output = 0;
        eeprom->mode = Chip9346_enter_command_mode;

        DEBUG_PRINT(("=== eeprom: begin access, enter command mode\n"));
    }

    if (!eecs)
    {
        DEBUG_PRINT(("=== eeprom: end access\n"));
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
    int isr;
    isr = (s->IntrStatus & s->IntrMask) & 0xffff;

    DEBUG_PRINT(("RTL8139: Set IRQ to %d (%04x %04x)\n",
       isr ? 1 : 0, s->IntrStatus, s->IntrMask));

    qemu_set_irq(s->pci_dev->irq[0], (isr != 0));
}

#define POLYNOMIAL 0x04c11db6

/* From FreeBSD */
/* XXX: optimize */
static int compute_mcast_idx(const uint8_t *ep)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < 6; i++) {
        b = *ep++;
        for (j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry)
                crc = ((crc ^ POLYNOMIAL) | carry);
        }
    }
    return (crc >> 26);
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
    if (s->RxBufAddr + size > s->RxBufferSize)
    {
        int wrapped = MOD2(s->RxBufAddr + size, s->RxBufferSize);

        /* write packet data */
        if (wrapped && !(s->RxBufferSize < 65536 && rtl8139_RxWrap(s)))
        {
            DEBUG_PRINT((">>> RTL8139: rx packet wrapped in buffer at %d\n", size-wrapped));

            if (size > wrapped)
            {
                cpu_physical_memory_write( s->RxBuf + s->RxBufAddr,
                                           buf, size-wrapped );
            }

            /* reset buffer pointer */
            s->RxBufAddr = 0;

            cpu_physical_memory_write( s->RxBuf + s->RxBufAddr,
                                       buf + (size-wrapped), wrapped );

            s->RxBufAddr = wrapped;

            return;
        }
    }

    /* non-wrapping path or overwrapping enabled */
    cpu_physical_memory_write( s->RxBuf + s->RxBufAddr, buf, size );

    s->RxBufAddr += size;
}

#define MIN_BUF_SIZE 60
static inline target_phys_addr_t rtl8139_addr64(uint32_t low, uint32_t high)
{
#if TARGET_PHYS_ADDR_BITS > 32
    return low | ((target_phys_addr_t)high << 32);
#else
    return low;
#endif
}

static int rtl8139_can_receive(void *opaque)
{
    RTL8139State *s = opaque;
    int avail;

    /* Receive (drop) packets if card is disabled.  */
    if (!s->clock_enabled)
      return 1;
    if (!rtl8139_receiver_enabled(s))
      return 1;

    if (rtl8139_cp_receiver_enabled(s)) {
        /* ??? Flow control not implemented in c+ mode.
           This is a hack to work around slirp deficiencies anyway.  */
        return 1;
    } else {
        avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr,
                     s->RxBufferSize);
        return (avail == 0 || avail >= 1514);
    }
}

static void rtl8139_do_receive(void *opaque, const uint8_t *buf, int size, int do_interrupt)
{
    RTL8139State *s = opaque;

    uint32_t packet_header = 0;

    uint8_t buf1[60];
    static const uint8_t broadcast_macaddr[6] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    DEBUG_PRINT((">>> RTL8139: received len=%d\n", size));

    /* test if board clock is stopped */
    if (!s->clock_enabled)
    {
        DEBUG_PRINT(("RTL8139: stopped ==========================\n"));
        return;
    }

    /* first check if receiver is enabled */

    if (!rtl8139_receiver_enabled(s))
    {
        DEBUG_PRINT(("RTL8139: receiver disabled ================\n"));
        return;
    }

    /* XXX: check this */
    if (s->RxConfig & AcceptAllPhys) {
        /* promiscuous: receive all */
        DEBUG_PRINT((">>> RTL8139: packet received in promiscuous mode\n"));

    } else {
        if (!memcmp(buf,  broadcast_macaddr, 6)) {
            /* broadcast address */
            if (!(s->RxConfig & AcceptBroadcast))
            {
                DEBUG_PRINT((">>> RTL8139: broadcast packet rejected\n"));

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return;
            }

            packet_header |= RxBroadcast;

            DEBUG_PRINT((">>> RTL8139: broadcast packet received\n"));

            /* update tally counter */
            ++s->tally_counters.RxOkBrd;

        } else if (buf[0] & 0x01) {
            /* multicast */
            if (!(s->RxConfig & AcceptMulticast))
            {
                DEBUG_PRINT((">>> RTL8139: multicast packet rejected\n"));

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return;
            }

            int mcast_idx = compute_mcast_idx(buf);

            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7))))
            {
                DEBUG_PRINT((">>> RTL8139: multicast address mismatch\n"));

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return;
            }

            packet_header |= RxMulticast;

            DEBUG_PRINT((">>> RTL8139: multicast packet received\n"));

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
                DEBUG_PRINT((">>> RTL8139: rejecting physical address matching packet\n"));

                /* update tally counter */
                ++s->tally_counters.RxERR;

                return;
            }

            packet_header |= RxPhysical;

            DEBUG_PRINT((">>> RTL8139: physical address matching packet received\n"));

            /* update tally counter */
            ++s->tally_counters.RxOkPhy;

        } else {

            DEBUG_PRINT((">>> RTL8139: unknown packet\n"));

            /* update tally counter */
            ++s->tally_counters.RxERR;

            return;
        }
    }

    /* if too small buffer, then expand it */
    if (size < MIN_BUF_SIZE) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, MIN_BUF_SIZE - size);
        buf = buf1;
        size = MIN_BUF_SIZE;
    }

    if (rtl8139_cp_receiver_enabled(s))
    {
        DEBUG_PRINT(("RTL8139: in C+ Rx mode ================\n"));

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
        target_phys_addr_t cplus_rx_ring_desc;

        cplus_rx_ring_desc = rtl8139_addr64(s->RxRingAddrLO, s->RxRingAddrHI);
        cplus_rx_ring_desc += 16 * descriptor;

        DEBUG_PRINT(("RTL8139: +++ C+ mode reading RX descriptor %d from host memory at %08x %08x = %016" PRIx64 "\n",
               descriptor, s->RxRingAddrHI, s->RxRingAddrLO, (uint64_t)cplus_rx_ring_desc));

        uint32_t val, rxdw0,rxdw1,rxbufLO,rxbufHI;

        cpu_physical_memory_read(cplus_rx_ring_desc,    (uint8_t *)&val, 4);
        rxdw0 = le32_to_cpu(val);
        cpu_physical_memory_read(cplus_rx_ring_desc+4,  (uint8_t *)&val, 4);
        rxdw1 = le32_to_cpu(val);
        cpu_physical_memory_read(cplus_rx_ring_desc+8,  (uint8_t *)&val, 4);
        rxbufLO = le32_to_cpu(val);
        cpu_physical_memory_read(cplus_rx_ring_desc+12, (uint8_t *)&val, 4);
        rxbufHI = le32_to_cpu(val);

        DEBUG_PRINT(("RTL8139: +++ C+ mode RX descriptor %d %08x %08x %08x %08x\n",
               descriptor,
               rxdw0, rxdw1, rxbufLO, rxbufHI));

        if (!(rxdw0 & CP_RX_OWN))
        {
            DEBUG_PRINT(("RTL8139: C+ Rx mode : descriptor %d is owned by host\n", descriptor));

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            /* update tally counter */
            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return;
        }

        uint32_t rx_space = rxdw0 & CP_RX_BUFFER_SIZE_MASK;

        /* TODO: scatter the packet over available receive ring descriptors space */

        if (size+4 > rx_space)
        {
            DEBUG_PRINT(("RTL8139: C+ Rx mode : descriptor %d size %d received %d + 4\n",
                   descriptor, rx_space, size));

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;

            /* update tally counter */
            ++s->tally_counters.RxERR;
            ++s->tally_counters.MissPkt;

            rtl8139_update_irq(s);
            return;
        }

        target_phys_addr_t rx_addr = rtl8139_addr64(rxbufLO, rxbufHI);

        /* receive/copy to target memory */
        cpu_physical_memory_write( rx_addr, buf, size );

        if (s->CpCmd & CPlusRxChkSum)
        {
            /* do some packet checksumming */
        }

        /* write checksum */
#if defined (RTL8139_CALCULATE_RXCRC)
        val = cpu_to_le32(crc32(0, buf, size));
#else
        val = 0;
#endif
        cpu_physical_memory_write( rx_addr+size, (uint8_t *)&val, 4);

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

        /* reset VLAN tag flag */
        rxdw1 &= ~CP_RX_TAVA;

        /* update ring data */
        val = cpu_to_le32(rxdw0);
        cpu_physical_memory_write(cplus_rx_ring_desc,    (uint8_t *)&val, 4);
        val = cpu_to_le32(rxdw1);
        cpu_physical_memory_write(cplus_rx_ring_desc+4,  (uint8_t *)&val, 4);

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

        DEBUG_PRINT(("RTL8139: done C+ Rx mode ----------------\n"));

    }
    else
    {
        DEBUG_PRINT(("RTL8139: in ring Rx mode ================\n"));

        /* begin ring receiver mode */
        int avail = MOD2(s->RxBufferSize + s->RxBufPtr - s->RxBufAddr, s->RxBufferSize);

        /* if receiver buffer is empty then avail == 0 */

        if (avail != 0 && size + 8 >= avail)
        {
            DEBUG_PRINT(("rx overflow: rx buffer length %d head 0x%04x read 0x%04x === available 0x%04x need 0x%04x\n",
                   s->RxBufferSize, s->RxBufAddr, s->RxBufPtr, avail, size + 8));

            s->IntrStatus |= RxOverflow;
            ++s->RxMissed;
            rtl8139_update_irq(s);
            return;
        }

        packet_header |= RxStatusOK;

        packet_header |= (((size+4) << 16) & 0xffff0000);

        /* write header */
        uint32_t val = cpu_to_le32(packet_header);

        rtl8139_write_buffer(s, (uint8_t *)&val, 4);

        rtl8139_write_buffer(s, buf, size);

        /* write checksum */
#if defined (RTL8139_CALCULATE_RXCRC)
        val = cpu_to_le32(crc32(0, buf, size));
#else
        val = 0;
#endif

        rtl8139_write_buffer(s, (uint8_t *)&val, 4);

        /* correct buffer write pointer */
        s->RxBufAddr = MOD2((s->RxBufAddr + 3) & ~0x3, s->RxBufferSize);

        /* now we can signal we have received something */

        DEBUG_PRINT(("   received: rx buffer length %d head 0x%04x read 0x%04x\n",
               s->RxBufferSize, s->RxBufAddr, s->RxBufPtr));
    }

    s->IntrStatus |= RxOK;

    if (do_interrupt)
    {
        rtl8139_update_irq(s);
    }
}

static void rtl8139_receive(void *opaque, const uint8_t *buf, int size)
{
    rtl8139_do_receive(opaque, buf, size, 1);
}

static void rtl8139_reset_rxring(RTL8139State *s, uint32_t bufferSize)
{
    s->RxBufferSize = bufferSize;
    s->RxBufPtr  = 0;
    s->RxBufAddr = 0;
}

static void rtl8139_reset(RTL8139State *s)
{
    int i;

    /* restore MAC address */
    memcpy(s->phys, s->macaddr, 6);

    /* reset interrupt mask */
    s->IntrStatus = 0;
    s->IntrMask = 0;

    rtl8139_update_irq(s);

    /* prepare eeprom */
    s->eeprom.contents[0] = 0x8129;
#if 1
    // PCI vendor and device ID should be mirrored here
    s->eeprom.contents[1] = PCI_VENDOR_ID_REALTEK;
    s->eeprom.contents[2] = PCI_DEVICE_ID_REALTEK_8139;
#endif

    s->eeprom.contents[7] = s->macaddr[0] | s->macaddr[1] << 8;
    s->eeprom.contents[8] = s->macaddr[2] | s->macaddr[3] << 8;
    s->eeprom.contents[9] = s->macaddr[4] | s->macaddr[5] << 8;

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

    s->CSCR = CSCR_F_LINK_100 | CSCR_HEART_BIT | CSCR_LD;

    s->CpCmd   = 0x0; /* reset C+ mode */
    s->cplus_enabled = 0;


//    s->BasicModeCtrl = 0x3100; // 100Mbps, full duplex, autonegotiation
//    s->BasicModeCtrl = 0x2100; // 100Mbps, full duplex
    s->BasicModeCtrl = 0x1000; // autonegotiation

    s->BasicModeStatus  = 0x7809;
    //s->BasicModeStatus |= 0x0040; /* UTP medium */
    s->BasicModeStatus |= 0x0020; /* autonegotiation completed */
    s->BasicModeStatus |= 0x0004; /* link is up */

    s->NWayAdvert    = 0x05e1; /* all modes, full duplex */
    s->NWayLPAR      = 0x05e1; /* all modes, full duplex */
    s->NWayExpansion = 0x0001; /* autonegotiation supported */

    /* also reset timer and disable timer interrupt */
    s->TCTR = 0;
    s->TimerInt = 0;
    s->TCTR_base = 0;

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

static void RTL8139TallyCounters_physical_memory_write(target_phys_addr_t tc_addr, RTL8139TallyCounters* tally_counters)
{
    uint16_t val16;
    uint32_t val32;
    uint64_t val64;

    val64 = cpu_to_le64(tally_counters->TxOk);
    cpu_physical_memory_write(tc_addr + 0,    (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->RxOk);
    cpu_physical_memory_write(tc_addr + 8,    (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->TxERR);
    cpu_physical_memory_write(tc_addr + 16,    (uint8_t *)&val64, 8);

    val32 = cpu_to_le32(tally_counters->RxERR);
    cpu_physical_memory_write(tc_addr + 24,    (uint8_t *)&val32, 4);

    val16 = cpu_to_le16(tally_counters->MissPkt);
    cpu_physical_memory_write(tc_addr + 28,    (uint8_t *)&val16, 2);

    val16 = cpu_to_le16(tally_counters->FAE);
    cpu_physical_memory_write(tc_addr + 30,    (uint8_t *)&val16, 2);

    val32 = cpu_to_le32(tally_counters->Tx1Col);
    cpu_physical_memory_write(tc_addr + 32,    (uint8_t *)&val32, 4);

    val32 = cpu_to_le32(tally_counters->TxMCol);
    cpu_physical_memory_write(tc_addr + 36,    (uint8_t *)&val32, 4);

    val64 = cpu_to_le64(tally_counters->RxOkPhy);
    cpu_physical_memory_write(tc_addr + 40,    (uint8_t *)&val64, 8);

    val64 = cpu_to_le64(tally_counters->RxOkBrd);
    cpu_physical_memory_write(tc_addr + 48,    (uint8_t *)&val64, 8);

    val32 = cpu_to_le32(tally_counters->RxOkMul);
    cpu_physical_memory_write(tc_addr + 56,    (uint8_t *)&val32, 4);

    val16 = cpu_to_le16(tally_counters->TxAbt);
    cpu_physical_memory_write(tc_addr + 60,    (uint8_t *)&val16, 2);

    val16 = cpu_to_le16(tally_counters->TxUndrn);
    cpu_physical_memory_write(tc_addr + 62,    (uint8_t *)&val16, 2);
}

/* Loads values of tally counters from VM state file */
static void RTL8139TallyCounters_load(QEMUFile* f, RTL8139TallyCounters *tally_counters)
{
    qemu_get_be64s(f, &tally_counters->TxOk);
    qemu_get_be64s(f, &tally_counters->RxOk);
    qemu_get_be64s(f, &tally_counters->TxERR);
    qemu_get_be32s(f, &tally_counters->RxERR);
    qemu_get_be16s(f, &tally_counters->MissPkt);
    qemu_get_be16s(f, &tally_counters->FAE);
    qemu_get_be32s(f, &tally_counters->Tx1Col);
    qemu_get_be32s(f, &tally_counters->TxMCol);
    qemu_get_be64s(f, &tally_counters->RxOkPhy);
    qemu_get_be64s(f, &tally_counters->RxOkBrd);
    qemu_get_be32s(f, &tally_counters->RxOkMul);
    qemu_get_be16s(f, &tally_counters->TxAbt);
    qemu_get_be16s(f, &tally_counters->TxUndrn);
}

/* Saves values of tally counters to VM state file */
static void RTL8139TallyCounters_save(QEMUFile* f, RTL8139TallyCounters *tally_counters)
{
    qemu_put_be64s(f, &tally_counters->TxOk);
    qemu_put_be64s(f, &tally_counters->RxOk);
    qemu_put_be64s(f, &tally_counters->TxERR);
    qemu_put_be32s(f, &tally_counters->RxERR);
    qemu_put_be16s(f, &tally_counters->MissPkt);
    qemu_put_be16s(f, &tally_counters->FAE);
    qemu_put_be32s(f, &tally_counters->Tx1Col);
    qemu_put_be32s(f, &tally_counters->TxMCol);
    qemu_put_be64s(f, &tally_counters->RxOkPhy);
    qemu_put_be64s(f, &tally_counters->RxOkBrd);
    qemu_put_be32s(f, &tally_counters->RxOkMul);
    qemu_put_be16s(f, &tally_counters->TxAbt);
    qemu_put_be16s(f, &tally_counters->TxUndrn);
}

static void rtl8139_ChipCmd_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DEBUG_PRINT(("RTL8139: ChipCmd write val=0x%08x\n", val));

    if (val & CmdReset)
    {
        DEBUG_PRINT(("RTL8139: ChipCmd reset\n"));
        rtl8139_reset(s);
    }
    if (val & CmdRxEnb)
    {
        DEBUG_PRINT(("RTL8139: ChipCmd enable receiver\n"));

        s->currCPlusRxDesc = 0;
    }
    if (val & CmdTxEnb)
    {
        DEBUG_PRINT(("RTL8139: ChipCmd enable transmitter\n"));

        s->currCPlusTxDesc = 0;
    }

    /* mask unwriteable bits */
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
        DEBUG_PRINT(("RTL8139: receiver buffer data available 0x%04x\n", unread));
        return 0;
    }

    DEBUG_PRINT(("RTL8139: receiver buffer is empty\n"));

    return 1;
}

static uint32_t rtl8139_ChipCmd_read(RTL8139State *s)
{
    uint32_t ret = s->bChipCmdState;

    if (rtl8139_RxBufferEmpty(s))
        ret |= RxBufEmpty;

    DEBUG_PRINT(("RTL8139: ChipCmd read val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_CpCmd_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DEBUG_PRINT(("RTL8139C+ command register write(w) val=0x%04x\n", val));

    s->cplus_enabled = 1;

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0xff84, s->CpCmd);

    s->CpCmd = val;
}

static uint32_t rtl8139_CpCmd_read(RTL8139State *s)
{
    uint32_t ret = s->CpCmd;

    DEBUG_PRINT(("RTL8139C+ command register read(w) val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_IntrMitigate_write(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139C+ IntrMitigate register write(w) val=0x%04x\n", val));
}

static uint32_t rtl8139_IntrMitigate_read(RTL8139State *s)
{
    uint32_t ret = 0;

    DEBUG_PRINT(("RTL8139C+ IntrMitigate register read(w) val=0x%04x\n", ret));

    return ret;
}

static int rtl8139_config_writeable(RTL8139State *s)
{
    if (s->Cfg9346 & Cfg9346_Unlock)
    {
        return 1;
    }

    DEBUG_PRINT(("RTL8139: Configuration registers are write-protected\n"));

    return 0;
}

static void rtl8139_BasicModeCtrl_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DEBUG_PRINT(("RTL8139: BasicModeCtrl register write(w) val=0x%04x\n", val));

    /* mask unwriteable bits */
    uint32_t mask = 0x4cff;

    if (1 || !rtl8139_config_writeable(s))
    {
        /* Speed setting and autonegotiation enable bits are read-only */
        mask |= 0x3000;
        /* Duplex mode setting is read-only */
        mask |= 0x0100;
    }

    val = SET_MASKED(val, mask, s->BasicModeCtrl);

    s->BasicModeCtrl = val;
}

static uint32_t rtl8139_BasicModeCtrl_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeCtrl;

    DEBUG_PRINT(("RTL8139: BasicModeCtrl register read(w) val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_BasicModeStatus_write(RTL8139State *s, uint32_t val)
{
    val &= 0xffff;

    DEBUG_PRINT(("RTL8139: BasicModeStatus register write(w) val=0x%04x\n", val));

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0xff3f, s->BasicModeStatus);

    s->BasicModeStatus = val;
}

static uint32_t rtl8139_BasicModeStatus_read(RTL8139State *s)
{
    uint32_t ret = s->BasicModeStatus;

    DEBUG_PRINT(("RTL8139: BasicModeStatus register read(w) val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_Cfg9346_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DEBUG_PRINT(("RTL8139: Cfg9346 write val=0x%02x\n", val));

    /* mask unwriteable bits */
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
        rtl8139_reset(s);
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

    DEBUG_PRINT(("RTL8139: Cfg9346 read val=0x%02x\n", ret));

    return ret;
}

static void rtl8139_Config0_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DEBUG_PRINT(("RTL8139: Config0 write val=0x%02x\n", val));

    if (!rtl8139_config_writeable(s))
        return;

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0xf8, s->Config0);

    s->Config0 = val;
}

static uint32_t rtl8139_Config0_read(RTL8139State *s)
{
    uint32_t ret = s->Config0;

    DEBUG_PRINT(("RTL8139: Config0 read val=0x%02x\n", ret));

    return ret;
}

static void rtl8139_Config1_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DEBUG_PRINT(("RTL8139: Config1 write val=0x%02x\n", val));

    if (!rtl8139_config_writeable(s))
        return;

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0xC, s->Config1);

    s->Config1 = val;
}

static uint32_t rtl8139_Config1_read(RTL8139State *s)
{
    uint32_t ret = s->Config1;

    DEBUG_PRINT(("RTL8139: Config1 read val=0x%02x\n", ret));

    return ret;
}

static void rtl8139_Config3_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DEBUG_PRINT(("RTL8139: Config3 write val=0x%02x\n", val));

    if (!rtl8139_config_writeable(s))
        return;

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0x8F, s->Config3);

    s->Config3 = val;
}

static uint32_t rtl8139_Config3_read(RTL8139State *s)
{
    uint32_t ret = s->Config3;

    DEBUG_PRINT(("RTL8139: Config3 read val=0x%02x\n", ret));

    return ret;
}

static void rtl8139_Config4_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DEBUG_PRINT(("RTL8139: Config4 write val=0x%02x\n", val));

    if (!rtl8139_config_writeable(s))
        return;

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0x0a, s->Config4);

    s->Config4 = val;
}

static uint32_t rtl8139_Config4_read(RTL8139State *s)
{
    uint32_t ret = s->Config4;

    DEBUG_PRINT(("RTL8139: Config4 read val=0x%02x\n", ret));

    return ret;
}

static void rtl8139_Config5_write(RTL8139State *s, uint32_t val)
{
    val &= 0xff;

    DEBUG_PRINT(("RTL8139: Config5 write val=0x%02x\n", val));

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0x80, s->Config5);

    s->Config5 = val;
}

static uint32_t rtl8139_Config5_read(RTL8139State *s)
{
    uint32_t ret = s->Config5;

    DEBUG_PRINT(("RTL8139: Config5 read val=0x%02x\n", ret));

    return ret;
}

static void rtl8139_TxConfig_write(RTL8139State *s, uint32_t val)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DEBUG_PRINT(("RTL8139: transmitter disabled; no TxConfig write val=0x%08x\n", val));
        return;
    }

    DEBUG_PRINT(("RTL8139: TxConfig write val=0x%08x\n", val));

    val = SET_MASKED(val, TxVersionMask | 0x8070f80f, s->TxConfig);

    s->TxConfig = val;
}

static void rtl8139_TxConfig_writeb(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139C TxConfig via write(b) val=0x%02x\n", val));

    uint32_t tc = s->TxConfig;
    tc &= 0xFFFFFF00;
    tc |= (val & 0x000000FF);
    rtl8139_TxConfig_write(s, tc);
}

static uint32_t rtl8139_TxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->TxConfig;

    DEBUG_PRINT(("RTL8139: TxConfig read val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_RxConfig_write(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139: RxConfig write val=0x%08x\n", val));

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0xf0fc0040, s->RxConfig);

    s->RxConfig = val;

    /* reset buffer size and read/write pointers */
    rtl8139_reset_rxring(s, 8192 << ((s->RxConfig >> 11) & 0x3));

    DEBUG_PRINT(("RTL8139: RxConfig write reset buffer size to %d\n", s->RxBufferSize));
}

static uint32_t rtl8139_RxConfig_read(RTL8139State *s)
{
    uint32_t ret = s->RxConfig;

    DEBUG_PRINT(("RTL8139: RxConfig read val=0x%08x\n", ret));

    return ret;
}

static void rtl8139_transfer_frame(RTL8139State *s, const uint8_t *buf, int size, int do_interrupt)
{
    if (!size)
    {
        DEBUG_PRINT(("RTL8139: +++ empty ethernet frame\n"));
        return;
    }

    if (TxLoopBack == (s->TxConfig & TxLoopBack))
    {
        DEBUG_PRINT(("RTL8139: +++ transmit loopback mode\n"));
        rtl8139_do_receive(s, buf, size, do_interrupt);
    }
    else
    {
        qemu_send_packet(s->vc, buf, size);
    }
}

static int rtl8139_transmit_one(RTL8139State *s, int descriptor)
{
    if (!rtl8139_transmitter_enabled(s))
    {
        DEBUG_PRINT(("RTL8139: +++ cannot transmit from descriptor %d: transmitter disabled\n",
                     descriptor));
        return 0;
    }

    if (s->TxStatus[descriptor] & TxHostOwns)
    {
        DEBUG_PRINT(("RTL8139: +++ cannot transmit from descriptor %d: owned by host (%08x)\n",
                     descriptor, s->TxStatus[descriptor]));
        return 0;
    }

    DEBUG_PRINT(("RTL8139: +++ transmitting from descriptor %d\n", descriptor));

    int txsize = s->TxStatus[descriptor] & 0x1fff;
    uint8_t txbuffer[0x2000];

    DEBUG_PRINT(("RTL8139: +++ transmit reading %d bytes from host memory at 0x%08x\n",
                 txsize, s->TxAddr[descriptor]));

    cpu_physical_memory_read(s->TxAddr[descriptor], txbuffer, txsize);

    /* Mark descriptor as transferred */
    s->TxStatus[descriptor] |= TxHostOwns;
    s->TxStatus[descriptor] |= TxStatOK;

    rtl8139_transfer_frame(s, txbuffer, txsize, 0);

    DEBUG_PRINT(("RTL8139: +++ transmitted %d bytes from descriptor %d\n", txsize, descriptor));

    /* update interrupt */
    s->IntrStatus |= TxOK;
    rtl8139_update_irq(s);

    return 1;
}

/* structures and macros for task offloading */
typedef struct ip_header
{
    uint8_t  ip_ver_len;    /* version and header length */
    uint8_t  ip_tos;        /* type of service */
    uint16_t ip_len;        /* total length */
    uint16_t ip_id;         /* identification */
    uint16_t ip_off;        /* fragment offset field */
    uint8_t  ip_ttl;        /* time to live */
    uint8_t  ip_p;          /* protocol */
    uint16_t ip_sum;        /* checksum */
    uint32_t ip_src,ip_dst; /* source and dest address */
} ip_header;

#define IP_HEADER_VERSION_4 4
#define IP_HEADER_VERSION(ip) ((ip->ip_ver_len >> 4)&0xf)
#define IP_HEADER_LENGTH(ip) (((ip->ip_ver_len)&0xf) << 2)

typedef struct tcp_header
{
    uint16_t th_sport;		/* source port */
    uint16_t th_dport;		/* destination port */
    uint32_t th_seq;			/* sequence number */
    uint32_t th_ack;			/* acknowledgement number */
    uint16_t th_offset_flags; /* data offset, reserved 6 bits, TCP protocol flags */
    uint16_t th_win;			/* window */
    uint16_t th_sum;			/* checksum */
    uint16_t th_urp;			/* urgent pointer */
} tcp_header;

typedef struct udp_header
{
    uint16_t uh_sport; /* source port */
    uint16_t uh_dport; /* destination port */
    uint16_t uh_ulen;  /* udp length */
    uint16_t uh_sum;   /* udp checksum */
} udp_header;

typedef struct ip_pseudo_header
{
    uint32_t ip_src;
    uint32_t ip_dst;
    uint8_t  zeros;
    uint8_t  ip_proto;
    uint16_t ip_payload;
} ip_pseudo_header;

#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define TCP_HEADER_DATA_OFFSET(tcp) (((be16_to_cpu(tcp->th_offset_flags) >> 12)&0xf) << 2)
#define TCP_FLAGS_ONLY(flags) ((flags)&0x3f)
#define TCP_HEADER_FLAGS(tcp) TCP_FLAGS_ONLY(be16_to_cpu(tcp->th_offset_flags))

#define TCP_HEADER_CLEAR_FLAGS(tcp, off) ((tcp)->th_offset_flags &= cpu_to_be16(~TCP_FLAGS_ONLY(off)))

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_PUSH 0x08

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
        DEBUG_PRINT(("RTL8139: +++ C+ mode: transmitter disabled\n"));
        return 0;
    }

    if (!rtl8139_cp_transmitter_enabled(s))
    {
        DEBUG_PRINT(("RTL8139: +++ C+ mode: C+ transmitter disabled\n"));
        return 0 ;
    }

    int descriptor = s->currCPlusTxDesc;

    target_phys_addr_t cplus_tx_ring_desc =
        rtl8139_addr64(s->TxAddr[0], s->TxAddr[1]);

    /* Normal priority ring */
    cplus_tx_ring_desc += 16 * descriptor;

    DEBUG_PRINT(("RTL8139: +++ C+ mode reading TX descriptor %d from host memory at %08x0x%08x = 0x%8lx\n",
           descriptor, s->TxAddr[1], s->TxAddr[0], cplus_tx_ring_desc));

    uint32_t val, txdw0,txdw1,txbufLO,txbufHI;

    cpu_physical_memory_read(cplus_tx_ring_desc,    (uint8_t *)&val, 4);
    txdw0 = le32_to_cpu(val);
    cpu_physical_memory_read(cplus_tx_ring_desc+4,  (uint8_t *)&val, 4);
    txdw1 = le32_to_cpu(val);
    cpu_physical_memory_read(cplus_tx_ring_desc+8,  (uint8_t *)&val, 4);
    txbufLO = le32_to_cpu(val);
    cpu_physical_memory_read(cplus_tx_ring_desc+12, (uint8_t *)&val, 4);
    txbufHI = le32_to_cpu(val);

    DEBUG_PRINT(("RTL8139: +++ C+ mode TX descriptor %d %08x %08x %08x %08x\n",
           descriptor,
           txdw0, txdw1, txbufLO, txbufHI));

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
/* large send MSS mask, bits 16...25 */
#define CP_TC_LGSEN_MSS_MASK ((1 << 12) - 1)

/* IP checksum offload flag */
#define CP_TX_IPCS (1<<18)
/* UDP checksum offload flag */
#define CP_TX_UDPCS (1<<17)
/* TCP checksum offload flag */
#define CP_TX_TCPCS (1<<16)

/* w0 bits 0...15 : buffer size */
#define CP_TX_BUFFER_SIZE (1<<16)
#define CP_TX_BUFFER_SIZE_MASK (CP_TX_BUFFER_SIZE - 1)
/* w1 tag available flag */
#define CP_RX_TAGC (1<<17)
/* w1 bits 0...15 : VLAN tag */
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
        DEBUG_PRINT(("RTL8139: C+ Tx mode : descriptor %d is owned by host\n", descriptor));
        return 0 ;
    }

    DEBUG_PRINT(("RTL8139: +++ C+ Tx mode : transmitting from descriptor %d\n", descriptor));

    if (txdw0 & CP_TX_FS)
    {
        DEBUG_PRINT(("RTL8139: +++ C+ Tx mode : descriptor %d is first segment descriptor\n", descriptor));

        /* reset internal buffer offset */
        s->cplus_txbuffer_offset = 0;
    }

    int txsize = txdw0 & CP_TX_BUFFER_SIZE_MASK;
    target_phys_addr_t tx_addr = rtl8139_addr64(txbufLO, txbufHI);

    /* make sure we have enough space to assemble the packet */
    if (!s->cplus_txbuffer)
    {
        s->cplus_txbuffer_len = CP_TX_BUFFER_SIZE;
        s->cplus_txbuffer = malloc(s->cplus_txbuffer_len);
        s->cplus_txbuffer_offset = 0;

        DEBUG_PRINT(("RTL8139: +++ C+ mode transmission buffer allocated space %d\n", s->cplus_txbuffer_len));
    }

    while (s->cplus_txbuffer && s->cplus_txbuffer_offset + txsize >= s->cplus_txbuffer_len)
    {
        s->cplus_txbuffer_len += CP_TX_BUFFER_SIZE;
        s->cplus_txbuffer = qemu_realloc(s->cplus_txbuffer, s->cplus_txbuffer_len);

        DEBUG_PRINT(("RTL8139: +++ C+ mode transmission buffer space changed to %d\n", s->cplus_txbuffer_len));
    }

    if (!s->cplus_txbuffer)
    {
        /* out of memory */

        DEBUG_PRINT(("RTL8139: +++ C+ mode transmiter failed to reallocate %d bytes\n", s->cplus_txbuffer_len));

        /* update tally counter */
        ++s->tally_counters.TxERR;
        ++s->tally_counters.TxAbt;

        return 0;
    }

    /* append more data to the packet */

    DEBUG_PRINT(("RTL8139: +++ C+ mode transmit reading %d bytes from host memory at %016" PRIx64 " to offset %d\n",
                 txsize, (uint64_t)tx_addr, s->cplus_txbuffer_offset));

    cpu_physical_memory_read(tx_addr, s->cplus_txbuffer + s->cplus_txbuffer_offset, txsize);
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

    /* transfer ownership to target */
    txdw0 &= ~CP_RX_OWN;

    /* reset error indicator bits */
    txdw0 &= ~CP_TX_STATUS_UNF;
    txdw0 &= ~CP_TX_STATUS_TES;
    txdw0 &= ~CP_TX_STATUS_OWC;
    txdw0 &= ~CP_TX_STATUS_LNKF;
    txdw0 &= ~CP_TX_STATUS_EXC;

    /* update ring data */
    val = cpu_to_le32(txdw0);
    cpu_physical_memory_write(cplus_tx_ring_desc,    (uint8_t *)&val, 4);
//    val = cpu_to_le32(txdw1);
//    cpu_physical_memory_write(cplus_tx_ring_desc+4,  &val, 4);

    /* Now decide if descriptor being processed is holding the last segment of packet */
    if (txdw0 & CP_TX_LS)
    {
        DEBUG_PRINT(("RTL8139: +++ C+ Tx mode : descriptor %d is last segment descriptor\n", descriptor));

        /* can transfer fully assembled packet */

        uint8_t *saved_buffer  = s->cplus_txbuffer;
        int      saved_size    = s->cplus_txbuffer_offset;
        int      saved_buffer_len = s->cplus_txbuffer_len;

        /* reset the card space to protect from recursive call */
        s->cplus_txbuffer = NULL;
        s->cplus_txbuffer_offset = 0;
        s->cplus_txbuffer_len = 0;

        if (txdw0 & (CP_TX_IPCS | CP_TX_UDPCS | CP_TX_TCPCS | CP_TX_LGSEN))
        {
            DEBUG_PRINT(("RTL8139: +++ C+ mode offloaded task checksum\n"));

            #define ETH_P_IP	0x0800		/* Internet Protocol packet	*/
            #define ETH_HLEN    14
            #define ETH_MTU     1500

            /* ip packet header */
            ip_header *ip = 0;
            int hlen = 0;
            uint8_t  ip_protocol = 0;
            uint16_t ip_data_len = 0;

            uint8_t *eth_payload_data = 0;
            size_t   eth_payload_len  = 0;

            int proto = be16_to_cpu(*(uint16_t *)(saved_buffer + 12));
            if (proto == ETH_P_IP)
            {
                DEBUG_PRINT(("RTL8139: +++ C+ mode has IP packet\n"));

                /* not aligned */
                eth_payload_data = saved_buffer + ETH_HLEN;
                eth_payload_len  = saved_size   - ETH_HLEN;

                ip = (ip_header*)eth_payload_data;

                if (IP_HEADER_VERSION(ip) != IP_HEADER_VERSION_4) {
                    DEBUG_PRINT(("RTL8139: +++ C+ mode packet has bad IP version %d expected %d\n", IP_HEADER_VERSION(ip), IP_HEADER_VERSION_4));
                    ip = NULL;
                } else {
                    hlen = IP_HEADER_LENGTH(ip);
                    ip_protocol = ip->ip_p;
                    ip_data_len = be16_to_cpu(ip->ip_len) - hlen;
                }
            }

            if (ip)
            {
                if (txdw0 & CP_TX_IPCS)
                {
                    DEBUG_PRINT(("RTL8139: +++ C+ mode need IP checksum\n"));

                    if (hlen<sizeof(ip_header) || hlen>eth_payload_len) {/* min header length */
                        /* bad packet header len */
                        /* or packet too short */
                    }
                    else
                    {
                        ip->ip_sum = 0;
                        ip->ip_sum = ip_checksum(ip, hlen);
                        DEBUG_PRINT(("RTL8139: +++ C+ mode IP header len=%d checksum=%04x\n", hlen, ip->ip_sum));
                    }
                }

                if ((txdw0 & CP_TX_LGSEN) && ip_protocol == IP_PROTO_TCP)
                {
#if defined (DEBUG_RTL8139)
                    int large_send_mss = (txdw0 >> 16) & CP_TC_LGSEN_MSS_MASK;
#endif
                    DEBUG_PRINT(("RTL8139: +++ C+ mode offloaded task TSO MTU=%d IP data %d frame data %d specified MSS=%d\n",
                                 ETH_MTU, ip_data_len, saved_size - ETH_HLEN, large_send_mss));

                    int tcp_send_offset = 0;
                    int send_count = 0;

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

                    /* ETH_MTU = ip header len + tcp header len + payload */
                    int tcp_data_len = ip_data_len - tcp_hlen;
                    int tcp_chunk_size = ETH_MTU - hlen - tcp_hlen;

                    DEBUG_PRINT(("RTL8139: +++ C+ mode TSO IP data len %d TCP hlen %d TCP data len %d TCP chunk size %d\n",
                                 ip_data_len, tcp_hlen, tcp_data_len, tcp_chunk_size));

                    /* note the cycle below overwrites IP header data,
                       but restores it from saved_ip_header before sending packet */

                    int is_last_frame = 0;

                    for (tcp_send_offset = 0; tcp_send_offset < tcp_data_len; tcp_send_offset += tcp_chunk_size)
                    {
                        uint16_t chunk_size = tcp_chunk_size;

                        /* check if this is the last frame */
                        if (tcp_send_offset + tcp_chunk_size >= tcp_data_len)
                        {
                            is_last_frame = 1;
                            chunk_size = tcp_data_len - tcp_send_offset;
                        }

                        DEBUG_PRINT(("RTL8139: +++ C+ mode TSO TCP seqno %08x\n", be32_to_cpu(p_tcp_hdr->th_seq)));

                        /* add 4 TCP pseudoheader fields */
                        /* copy IP source and destination fields */
                        memcpy(data_to_checksum, saved_ip_header + 12, 8);

                        DEBUG_PRINT(("RTL8139: +++ C+ mode TSO calculating TCP checksum for packet with %d bytes data\n", tcp_hlen + chunk_size));

                        if (tcp_send_offset)
                        {
                            memcpy((uint8_t*)p_tcp_hdr + tcp_hlen, (uint8_t*)p_tcp_hdr + tcp_hlen + tcp_send_offset, chunk_size);
                        }

                        /* keep PUSH and FIN flags only for the last frame */
                        if (!is_last_frame)
                        {
                            TCP_HEADER_CLEAR_FLAGS(p_tcp_hdr, TCP_FLAG_PUSH|TCP_FLAG_FIN);
                        }

                        /* recalculate TCP checksum */
                        ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *)data_to_checksum;
                        p_tcpip_hdr->zeros      = 0;
                        p_tcpip_hdr->ip_proto   = IP_PROTO_TCP;
                        p_tcpip_hdr->ip_payload = cpu_to_be16(tcp_hlen + chunk_size);

                        p_tcp_hdr->th_sum = 0;

                        int tcp_checksum = ip_checksum(data_to_checksum, tcp_hlen + chunk_size + 12);
                        DEBUG_PRINT(("RTL8139: +++ C+ mode TSO TCP checksum %04x\n", tcp_checksum));

                        p_tcp_hdr->th_sum = tcp_checksum;

                        /* restore IP header */
                        memcpy(eth_payload_data, saved_ip_header, hlen);

                        /* set IP data length and recalculate IP checksum */
                        ip->ip_len = cpu_to_be16(hlen + tcp_hlen + chunk_size);

                        /* increment IP id for subsequent frames */
                        ip->ip_id = cpu_to_be16(tcp_send_offset/tcp_chunk_size + be16_to_cpu(ip->ip_id));

                        ip->ip_sum = 0;
                        ip->ip_sum = ip_checksum(eth_payload_data, hlen);
                        DEBUG_PRINT(("RTL8139: +++ C+ mode TSO IP header len=%d checksum=%04x\n", hlen, ip->ip_sum));

                        int tso_send_size = ETH_HLEN + hlen + tcp_hlen + chunk_size;
                        DEBUG_PRINT(("RTL8139: +++ C+ mode TSO transferring packet size %d\n", tso_send_size));
                        rtl8139_transfer_frame(s, saved_buffer, tso_send_size, 0);

                        /* add transferred count to TCP sequence number */
                        p_tcp_hdr->th_seq = cpu_to_be32(chunk_size + be32_to_cpu(p_tcp_hdr->th_seq));
                        ++send_count;
                    }

                    /* Stop sending this frame */
                    saved_size = 0;
                }
                else if (txdw0 & (CP_TX_TCPCS|CP_TX_UDPCS))
                {
                    DEBUG_PRINT(("RTL8139: +++ C+ mode need TCP or UDP checksum\n"));

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
                        DEBUG_PRINT(("RTL8139: +++ C+ mode calculating TCP checksum for packet with %d bytes data\n", ip_data_len));

                        ip_pseudo_header *p_tcpip_hdr = (ip_pseudo_header *)data_to_checksum;
                        p_tcpip_hdr->zeros      = 0;
                        p_tcpip_hdr->ip_proto   = IP_PROTO_TCP;
                        p_tcpip_hdr->ip_payload = cpu_to_be16(ip_data_len);

                        tcp_header* p_tcp_hdr = (tcp_header *) (data_to_checksum+12);

                        p_tcp_hdr->th_sum = 0;

                        int tcp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                        DEBUG_PRINT(("RTL8139: +++ C+ mode TCP checksum %04x\n", tcp_checksum));

                        p_tcp_hdr->th_sum = tcp_checksum;
                    }
                    else if ((txdw0 & CP_TX_UDPCS) && ip_protocol == IP_PROTO_UDP)
                    {
                        DEBUG_PRINT(("RTL8139: +++ C+ mode calculating UDP checksum for packet with %d bytes data\n", ip_data_len));

                        ip_pseudo_header *p_udpip_hdr = (ip_pseudo_header *)data_to_checksum;
                        p_udpip_hdr->zeros      = 0;
                        p_udpip_hdr->ip_proto   = IP_PROTO_UDP;
                        p_udpip_hdr->ip_payload = cpu_to_be16(ip_data_len);

                        udp_header *p_udp_hdr = (udp_header *) (data_to_checksum+12);

                        p_udp_hdr->uh_sum = 0;

                        int udp_checksum = ip_checksum(data_to_checksum, ip_data_len + 12);
                        DEBUG_PRINT(("RTL8139: +++ C+ mode UDP checksum %04x\n", udp_checksum));

                        p_udp_hdr->uh_sum = udp_checksum;
                    }

                    /* restore IP header */
                    memcpy(eth_payload_data, saved_ip_header, hlen);
                }
            }
        }

        /* update tally counter */
        ++s->tally_counters.TxOk;

        DEBUG_PRINT(("RTL8139: +++ C+ mode transmitting %d bytes packet\n", saved_size));

        rtl8139_transfer_frame(s, saved_buffer, saved_size, 1);

        /* restore card space if there was no recursion and reset offset */
        if (!s->cplus_txbuffer)
        {
            s->cplus_txbuffer        = saved_buffer;
            s->cplus_txbuffer_len    = saved_buffer_len;
            s->cplus_txbuffer_offset = 0;
        }
        else
        {
            free(saved_buffer);
        }
    }
    else
    {
        DEBUG_PRINT(("RTL8139: +++ C+ mode transmission continue to next descriptor\n"));
    }

    return 1;
}

static void rtl8139_cplus_transmit(RTL8139State *s)
{
    int txcount = 0;

    while (rtl8139_cplus_transmit_one(s))
    {
        ++txcount;
    }

    /* Mark transfer completed */
    if (!txcount)
    {
        DEBUG_PRINT(("RTL8139: C+ mode : transmitter queue stalled, current TxDesc = %d\n",
                     s->currCPlusTxDesc));
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
        DEBUG_PRINT(("RTL8139: transmitter queue stalled, current TxDesc = %d\n", s->currTxDesc));
    }
}

static void rtl8139_TxStatus_write(RTL8139State *s, uint32_t txRegOffset, uint32_t val)
{

    int descriptor = txRegOffset/4;

    /* handle C+ transmit mode register configuration */

    if (s->cplus_enabled)
    {
        DEBUG_PRINT(("RTL8139C+ DTCCR write offset=0x%x val=0x%08x descriptor=%d\n", txRegOffset, val, descriptor));

        /* handle Dump Tally Counters command */
        s->TxStatus[descriptor] = val;

        if (descriptor == 0 && (val & 0x8))
        {
            target_phys_addr_t tc_addr = rtl8139_addr64(s->TxStatus[0] & ~0x3f, s->TxStatus[1]);

            /* dump tally counters to specified memory location */
            RTL8139TallyCounters_physical_memory_write( tc_addr, &s->tally_counters);

            /* mark dump completed */
            s->TxStatus[0] &= ~0x8;
        }

        return;
    }

    DEBUG_PRINT(("RTL8139: TxStatus write offset=0x%x val=0x%08x descriptor=%d\n", txRegOffset, val, descriptor));

    /* mask only reserved bits */
    val &= ~0xff00c000; /* these bits are reset on write */
    val = SET_MASKED(val, 0x00c00000, s->TxStatus[descriptor]);

    s->TxStatus[descriptor] = val;

    /* attempt to start transmission */
    rtl8139_transmit(s);
}

static uint32_t rtl8139_TxStatus_read(RTL8139State *s, uint32_t txRegOffset)
{
    uint32_t ret = s->TxStatus[txRegOffset/4];

    DEBUG_PRINT(("RTL8139: TxStatus read offset=0x%x val=0x%08x\n", txRegOffset, ret));

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


    DEBUG_PRINT(("RTL8139: TSAD read val=0x%04x\n", ret));

    return ret;
}

static uint16_t rtl8139_CSCR_read(RTL8139State *s)
{
    uint16_t ret = s->CSCR;

    DEBUG_PRINT(("RTL8139: CSCR read val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_TxAddr_write(RTL8139State *s, uint32_t txAddrOffset, uint32_t val)
{
    DEBUG_PRINT(("RTL8139: TxAddr write offset=0x%x val=0x%08x\n", txAddrOffset, val));

    s->TxAddr[txAddrOffset/4] = val;
}

static uint32_t rtl8139_TxAddr_read(RTL8139State *s, uint32_t txAddrOffset)
{
    uint32_t ret = s->TxAddr[txAddrOffset/4];

    DEBUG_PRINT(("RTL8139: TxAddr read offset=0x%x val=0x%08x\n", txAddrOffset, ret));

    return ret;
}

static void rtl8139_RxBufPtr_write(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139: RxBufPtr write val=0x%04x\n", val));

    /* this value is off by 16 */
    s->RxBufPtr = MOD2(val + 0x10, s->RxBufferSize);

    DEBUG_PRINT((" CAPR write: rx buffer length %d head 0x%04x read 0x%04x\n",
           s->RxBufferSize, s->RxBufAddr, s->RxBufPtr));
}

static uint32_t rtl8139_RxBufPtr_read(RTL8139State *s)
{
    /* this value is off by 16 */
    uint32_t ret = s->RxBufPtr - 0x10;

    DEBUG_PRINT(("RTL8139: RxBufPtr read val=0x%04x\n", ret));

    return ret;
}

static uint32_t rtl8139_RxBufAddr_read(RTL8139State *s)
{
    /* this value is NOT off by 16 */
    uint32_t ret = s->RxBufAddr;

    DEBUG_PRINT(("RTL8139: RxBufAddr read val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_RxBuf_write(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139: RxBuf write val=0x%08x\n", val));

    s->RxBuf = val;

    /* may need to reset rxring here */
}

static uint32_t rtl8139_RxBuf_read(RTL8139State *s)
{
    uint32_t ret = s->RxBuf;

    DEBUG_PRINT(("RTL8139: RxBuf read val=0x%08x\n", ret));

    return ret;
}

static void rtl8139_IntrMask_write(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139: IntrMask write(w) val=0x%04x\n", val));

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0x1e00, s->IntrMask);

    s->IntrMask = val;

    rtl8139_update_irq(s);
}

static uint32_t rtl8139_IntrMask_read(RTL8139State *s)
{
    uint32_t ret = s->IntrMask;

    DEBUG_PRINT(("RTL8139: IntrMask read(w) val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_IntrStatus_write(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139: IntrStatus write(w) val=0x%04x\n", val));

#if 0

    /* writing to ISR has no effect */

    return;

#else
    uint16_t newStatus = s->IntrStatus & ~val;

    /* mask unwriteable bits */
    newStatus = SET_MASKED(newStatus, 0x1e00, s->IntrStatus);

    /* writing 1 to interrupt status register bit clears it */
    s->IntrStatus = 0;
    rtl8139_update_irq(s);

    s->IntrStatus = newStatus;
    rtl8139_update_irq(s);
#endif
}

static uint32_t rtl8139_IntrStatus_read(RTL8139State *s)
{
    uint32_t ret = s->IntrStatus;

    DEBUG_PRINT(("RTL8139: IntrStatus read(w) val=0x%04x\n", ret));

#if 0

    /* reading ISR clears all interrupts */
    s->IntrStatus = 0;

    rtl8139_update_irq(s);

#endif

    return ret;
}

static void rtl8139_MultiIntr_write(RTL8139State *s, uint32_t val)
{
    DEBUG_PRINT(("RTL8139: MultiIntr write(w) val=0x%04x\n", val));

    /* mask unwriteable bits */
    val = SET_MASKED(val, 0xf000, s->MultiIntr);

    s->MultiIntr = val;
}

static uint32_t rtl8139_MultiIntr_read(RTL8139State *s)
{
    uint32_t ret = s->MultiIntr;

    DEBUG_PRINT(("RTL8139: MultiIntr read(w) val=0x%04x\n", ret));

    return ret;
}

static void rtl8139_io_writeb(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    addr &= 0xff;

    switch (addr)
    {
        case MAC0 ... MAC0+5:
            s->phys[addr - MAC0] = val;
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
            DEBUG_PRINT(("RTL8139: not implemented write(b) to MediaStatus val=0x%02x\n", val));
            break;

        case HltClk:
            DEBUG_PRINT(("RTL8139: HltClk write val=0x%08x\n", val));
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
            DEBUG_PRINT(("RTL8139C+ TxThresh write(b) val=0x%02x\n", val));
            s->TxThresh = val;
            break;

        case TxPoll:
            DEBUG_PRINT(("RTL8139C+ TxPoll write(b) val=0x%02x\n", val));
            if (val & (1 << 7))
            {
                DEBUG_PRINT(("RTL8139C+ TxPoll high priority transmission (not implemented)\n"));
                //rtl8139_cplus_transmit(s);
            }
            if (val & (1 << 6))
            {
                DEBUG_PRINT(("RTL8139C+ TxPoll normal priority transmission\n"));
                rtl8139_cplus_transmit(s);
            }

            break;

        default:
            DEBUG_PRINT(("RTL8139: not implemented write(b) addr=0x%x val=0x%02x\n", addr, val));
            break;
    }
}

static void rtl8139_io_writew(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    addr &= 0xfe;

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
            DEBUG_PRINT(("RTL8139: NWayAdvert write(w) val=0x%04x\n", val));
            s->NWayAdvert = val;
            break;
        case NWayLPAR:
            DEBUG_PRINT(("RTL8139: forbidden NWayLPAR write(w) val=0x%04x\n", val));
            break;
        case NWayExpansion:
            DEBUG_PRINT(("RTL8139: NWayExpansion write(w) val=0x%04x\n", val));
            s->NWayExpansion = val;
            break;

        case CpCmd:
            rtl8139_CpCmd_write(s, val);
            break;

        case IntrMitigate:
            rtl8139_IntrMitigate_write(s, val);
            break;

        default:
            DEBUG_PRINT(("RTL8139: ioport write(w) addr=0x%x val=0x%04x via write(b)\n", addr, val));

            rtl8139_io_writeb(opaque, addr, val & 0xff);
            rtl8139_io_writeb(opaque, addr + 1, (val >> 8) & 0xff);
            break;
    }
}

static void rtl8139_io_writel(void *opaque, uint8_t addr, uint32_t val)
{
    RTL8139State *s = opaque;

    addr &= 0xfc;

    switch (addr)
    {
        case RxMissed:
            DEBUG_PRINT(("RTL8139: RxMissed clearing on write\n"));
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
            DEBUG_PRINT(("RTL8139: C+ RxRing low bits write val=0x%08x\n", val));
            s->RxRingAddrLO = val;
            break;

        case RxRingAddrHI:
            DEBUG_PRINT(("RTL8139: C+ RxRing high bits write val=0x%08x\n", val));
            s->RxRingAddrHI = val;
            break;

        case Timer:
            DEBUG_PRINT(("RTL8139: TCTR Timer reset on write\n"));
            s->TCTR = 0;
            s->TCTR_base = qemu_get_clock(vm_clock);
            break;

        case FlashReg:
            DEBUG_PRINT(("RTL8139: FlashReg TimerInt write val=0x%08x\n", val));
            s->TimerInt = val;
            break;

        default:
            DEBUG_PRINT(("RTL8139: ioport write(l) addr=0x%x val=0x%08x via write(b)\n", addr, val));
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

    addr &= 0xff;

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
            ret = 0xd0;
            DEBUG_PRINT(("RTL8139: MediaStatus read 0x%x\n", ret));
            break;

        case HltClk:
            ret = s->clock_enabled;
            DEBUG_PRINT(("RTL8139: HltClk read 0x%x\n", ret));
            break;

        case PCIRevisionID:
            ret = RTL8139_PCI_REVID;
            DEBUG_PRINT(("RTL8139: PCI Revision ID read 0x%x\n", ret));
            break;

        case TxThresh:
            ret = s->TxThresh;
            DEBUG_PRINT(("RTL8139C+ TxThresh read(b) val=0x%02x\n", ret));
            break;

        case 0x43: /* Part of TxConfig register. Windows driver tries to read it */
            ret = s->TxConfig >> 24;
            DEBUG_PRINT(("RTL8139C TxConfig at 0x43 read(b) val=0x%02x\n", ret));
            break;

        default:
            DEBUG_PRINT(("RTL8139: not implemented read(b) addr=0x%x\n", addr));
            ret = 0;
            break;
    }

    return ret;
}

static uint32_t rtl8139_io_readw(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    uint32_t ret;

    addr &= 0xfe; /* mask lower bit */

    switch (addr)
    {
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
            DEBUG_PRINT(("RTL8139: NWayAdvert read(w) val=0x%04x\n", ret));
            break;
        case NWayLPAR:
            ret = s->NWayLPAR;
            DEBUG_PRINT(("RTL8139: NWayLPAR read(w) val=0x%04x\n", ret));
            break;
        case NWayExpansion:
            ret = s->NWayExpansion;
            DEBUG_PRINT(("RTL8139: NWayExpansion read(w) val=0x%04x\n", ret));
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
            DEBUG_PRINT(("RTL8139: ioport read(w) addr=0x%x via read(b)\n", addr));

            ret  = rtl8139_io_readb(opaque, addr);
            ret |= rtl8139_io_readb(opaque, addr + 1) << 8;

            DEBUG_PRINT(("RTL8139: ioport read(w) addr=0x%x val=0x%04x\n", addr, ret));
            break;
    }

    return ret;
}

static uint32_t rtl8139_io_readl(void *opaque, uint8_t addr)
{
    RTL8139State *s = opaque;
    uint32_t ret;

    addr &= 0xfc; /* also mask low 2 bits */

    switch (addr)
    {
        case RxMissed:
            ret = s->RxMissed;

            DEBUG_PRINT(("RTL8139: RxMissed read val=0x%08x\n", ret));
            break;

        case TxConfig:
            ret = rtl8139_TxConfig_read(s);
            break;

        case RxConfig:
            ret = rtl8139_RxConfig_read(s);
            break;

        case TxStatus0 ... TxStatus0+4*4-1:
            ret = rtl8139_TxStatus_read(s, addr-TxStatus0);
            break;

        case TxAddr0 ... TxAddr0+4*4-1:
            ret = rtl8139_TxAddr_read(s, addr-TxAddr0);
            break;

        case RxBuf:
            ret = rtl8139_RxBuf_read(s);
            break;

        case RxRingAddrLO:
            ret = s->RxRingAddrLO;
            DEBUG_PRINT(("RTL8139: C+ RxRing low bits read val=0x%08x\n", ret));
            break;

        case RxRingAddrHI:
            ret = s->RxRingAddrHI;
            DEBUG_PRINT(("RTL8139: C+ RxRing high bits read val=0x%08x\n", ret));
            break;

        case Timer:
            ret = s->TCTR;
            DEBUG_PRINT(("RTL8139: TCTR Timer read val=0x%08x\n", ret));
            break;

        case FlashReg:
            ret = s->TimerInt;
            DEBUG_PRINT(("RTL8139: FlashReg TimerInt read val=0x%08x\n", ret));
            break;

        default:
            DEBUG_PRINT(("RTL8139: ioport read(l) addr=0x%x via read(b)\n", addr));

            ret  = rtl8139_io_readb(opaque, addr);
            ret |= rtl8139_io_readb(opaque, addr + 1) << 8;
            ret |= rtl8139_io_readb(opaque, addr + 2) << 16;
            ret |= rtl8139_io_readb(opaque, addr + 3) << 24;

            DEBUG_PRINT(("RTL8139: read(l) addr=0x%x val=%08x\n", addr, ret));
            break;
    }

    return ret;
}

/* */

static void rtl8139_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    rtl8139_io_writeb(opaque, addr & 0xFF, val);
}

static void rtl8139_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    rtl8139_io_writew(opaque, addr & 0xFF, val);
}

static void rtl8139_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    rtl8139_io_writel(opaque, addr & 0xFF, val);
}

static uint32_t rtl8139_ioport_readb(void *opaque, uint32_t addr)
{
    return rtl8139_io_readb(opaque, addr & 0xFF);
}

static uint32_t rtl8139_ioport_readw(void *opaque, uint32_t addr)
{
    return rtl8139_io_readw(opaque, addr & 0xFF);
}

static uint32_t rtl8139_ioport_readl(void *opaque, uint32_t addr)
{
    return rtl8139_io_readl(opaque, addr & 0xFF);
}

/* */

static void rtl8139_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    rtl8139_io_writeb(opaque, addr & 0xFF, val);
}

static void rtl8139_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    rtl8139_io_writew(opaque, addr & 0xFF, val);
}

static void rtl8139_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    rtl8139_io_writel(opaque, addr & 0xFF, val);
}

static uint32_t rtl8139_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    return rtl8139_io_readb(opaque, addr & 0xFF);
}

static uint32_t rtl8139_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t val = rtl8139_io_readw(opaque, addr & 0xFF);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

static uint32_t rtl8139_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t val = rtl8139_io_readl(opaque, addr & 0xFF);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

/* */

static void rtl8139_save(QEMUFile* f,void* opaque)
{
    RTL8139State* s=(RTL8139State*)opaque;
    unsigned int i;

    pci_device_save(s->pci_dev, f);

    qemu_put_buffer(f, s->phys, 6);
    qemu_put_buffer(f, s->mult, 8);

    for (i=0; i<4; ++i)
    {
        qemu_put_be32s(f, &s->TxStatus[i]); /* TxStatus0 */
    }
    for (i=0; i<4; ++i)
    {
        qemu_put_be32s(f, &s->TxAddr[i]); /* TxAddr0 */
    }

    qemu_put_be32s(f, &s->RxBuf); /* Receive buffer */
    qemu_put_be32s(f, &s->RxBufferSize);/* internal variable, receive ring buffer size in C mode */
    qemu_put_be32s(f, &s->RxBufPtr);
    qemu_put_be32s(f, &s->RxBufAddr);

    qemu_put_be16s(f, &s->IntrStatus);
    qemu_put_be16s(f, &s->IntrMask);

    qemu_put_be32s(f, &s->TxConfig);
    qemu_put_be32s(f, &s->RxConfig);
    qemu_put_be32s(f, &s->RxMissed);
    qemu_put_be16s(f, &s->CSCR);

    qemu_put_8s(f, &s->Cfg9346);
    qemu_put_8s(f, &s->Config0);
    qemu_put_8s(f, &s->Config1);
    qemu_put_8s(f, &s->Config3);
    qemu_put_8s(f, &s->Config4);
    qemu_put_8s(f, &s->Config5);

    qemu_put_8s(f, &s->clock_enabled);
    qemu_put_8s(f, &s->bChipCmdState);

    qemu_put_be16s(f, &s->MultiIntr);

    qemu_put_be16s(f, &s->BasicModeCtrl);
    qemu_put_be16s(f, &s->BasicModeStatus);
    qemu_put_be16s(f, &s->NWayAdvert);
    qemu_put_be16s(f, &s->NWayLPAR);
    qemu_put_be16s(f, &s->NWayExpansion);

    qemu_put_be16s(f, &s->CpCmd);
    qemu_put_8s(f, &s->TxThresh);

    i = 0;
    qemu_put_be32s(f, &i); /* unused.  */
    qemu_put_buffer(f, s->macaddr, 6);
    qemu_put_be32(f, s->rtl8139_mmio_io_addr);

    qemu_put_be32s(f, &s->currTxDesc);
    qemu_put_be32s(f, &s->currCPlusRxDesc);
    qemu_put_be32s(f, &s->currCPlusTxDesc);
    qemu_put_be32s(f, &s->RxRingAddrLO);
    qemu_put_be32s(f, &s->RxRingAddrHI);

    for (i=0; i<EEPROM_9346_SIZE; ++i)
    {
        qemu_put_be16s(f, &s->eeprom.contents[i]);
    }
    qemu_put_be32(f, s->eeprom.mode);
    qemu_put_be32s(f, &s->eeprom.tick);
    qemu_put_8s(f, &s->eeprom.address);
    qemu_put_be16s(f, &s->eeprom.input);
    qemu_put_be16s(f, &s->eeprom.output);

    qemu_put_8s(f, &s->eeprom.eecs);
    qemu_put_8s(f, &s->eeprom.eesk);
    qemu_put_8s(f, &s->eeprom.eedi);
    qemu_put_8s(f, &s->eeprom.eedo);

    qemu_put_be32s(f, &s->TCTR);
    qemu_put_be32s(f, &s->TimerInt);
    qemu_put_be64(f, s->TCTR_base);

    RTL8139TallyCounters_save(f, &s->tally_counters);

    qemu_put_be32s(f, &s->cplus_enabled);
}

static int rtl8139_load(QEMUFile* f,void* opaque,int version_id)
{
    RTL8139State* s=(RTL8139State*)opaque;
    unsigned int i;
    int ret;

    /* just 2 versions for now */
    if (version_id > 4)
            return -EINVAL;

    if (version_id >= 3) {
        ret = pci_device_load(s->pci_dev, f);
        if (ret < 0)
            return ret;
    }

    /* saved since version 1 */
    qemu_get_buffer(f, s->phys, 6);
    qemu_get_buffer(f, s->mult, 8);

    for (i=0; i<4; ++i)
    {
        qemu_get_be32s(f, &s->TxStatus[i]); /* TxStatus0 */
    }
    for (i=0; i<4; ++i)
    {
        qemu_get_be32s(f, &s->TxAddr[i]); /* TxAddr0 */
    }

    qemu_get_be32s(f, &s->RxBuf); /* Receive buffer */
    qemu_get_be32s(f, &s->RxBufferSize);/* internal variable, receive ring buffer size in C mode */
    qemu_get_be32s(f, &s->RxBufPtr);
    qemu_get_be32s(f, &s->RxBufAddr);

    qemu_get_be16s(f, &s->IntrStatus);
    qemu_get_be16s(f, &s->IntrMask);

    qemu_get_be32s(f, &s->TxConfig);
    qemu_get_be32s(f, &s->RxConfig);
    qemu_get_be32s(f, &s->RxMissed);
    qemu_get_be16s(f, &s->CSCR);

    qemu_get_8s(f, &s->Cfg9346);
    qemu_get_8s(f, &s->Config0);
    qemu_get_8s(f, &s->Config1);
    qemu_get_8s(f, &s->Config3);
    qemu_get_8s(f, &s->Config4);
    qemu_get_8s(f, &s->Config5);

    qemu_get_8s(f, &s->clock_enabled);
    qemu_get_8s(f, &s->bChipCmdState);

    qemu_get_be16s(f, &s->MultiIntr);

    qemu_get_be16s(f, &s->BasicModeCtrl);
    qemu_get_be16s(f, &s->BasicModeStatus);
    qemu_get_be16s(f, &s->NWayAdvert);
    qemu_get_be16s(f, &s->NWayLPAR);
    qemu_get_be16s(f, &s->NWayExpansion);

    qemu_get_be16s(f, &s->CpCmd);
    qemu_get_8s(f, &s->TxThresh);

    qemu_get_be32s(f, &i); /* unused.  */
    qemu_get_buffer(f, s->macaddr, 6);
    s->rtl8139_mmio_io_addr=qemu_get_be32(f);

    qemu_get_be32s(f, &s->currTxDesc);
    qemu_get_be32s(f, &s->currCPlusRxDesc);
    qemu_get_be32s(f, &s->currCPlusTxDesc);
    qemu_get_be32s(f, &s->RxRingAddrLO);
    qemu_get_be32s(f, &s->RxRingAddrHI);

    for (i=0; i<EEPROM_9346_SIZE; ++i)
    {
        qemu_get_be16s(f, &s->eeprom.contents[i]);
    }
    s->eeprom.mode=qemu_get_be32(f);
    qemu_get_be32s(f, &s->eeprom.tick);
    qemu_get_8s(f, &s->eeprom.address);
    qemu_get_be16s(f, &s->eeprom.input);
    qemu_get_be16s(f, &s->eeprom.output);

    qemu_get_8s(f, &s->eeprom.eecs);
    qemu_get_8s(f, &s->eeprom.eesk);
    qemu_get_8s(f, &s->eeprom.eedi);
    qemu_get_8s(f, &s->eeprom.eedo);

    /* saved since version 2 */
    if (version_id >= 2)
    {
        qemu_get_be32s(f, &s->TCTR);
        qemu_get_be32s(f, &s->TimerInt);
        s->TCTR_base=qemu_get_be64(f);

        RTL8139TallyCounters_load(f, &s->tally_counters);
    }
    else
    {
        /* not saved, use default */
        s->TCTR = 0;
        s->TimerInt = 0;
        s->TCTR_base = 0;

        RTL8139TallyCounters_clear(&s->tally_counters);
    }

    if (version_id >= 4) {
        qemu_get_be32s(f, &s->cplus_enabled);
    } else {
        s->cplus_enabled = s->CpCmd != 0;
    }

    return 0;
}

/***********************************************************/
/* PCI RTL8139 definitions */

typedef struct PCIRTL8139State {
    PCIDevice dev;
    RTL8139State rtl8139;
} PCIRTL8139State;

static void rtl8139_mmio_map(PCIDevice *pci_dev, int region_num,
                       uint32_t addr, uint32_t size, int type)
{
    PCIRTL8139State *d = (PCIRTL8139State *)pci_dev;
    RTL8139State *s = &d->rtl8139;

    cpu_register_physical_memory(addr + 0, 0x100, s->rtl8139_mmio_io_addr);
}

static void rtl8139_ioport_map(PCIDevice *pci_dev, int region_num,
                       uint32_t addr, uint32_t size, int type)
{
    PCIRTL8139State *d = (PCIRTL8139State *)pci_dev;
    RTL8139State *s = &d->rtl8139;

    register_ioport_write(addr, 0x100, 1, rtl8139_ioport_writeb, s);
    register_ioport_read( addr, 0x100, 1, rtl8139_ioport_readb,  s);

    register_ioport_write(addr, 0x100, 2, rtl8139_ioport_writew, s);
    register_ioport_read( addr, 0x100, 2, rtl8139_ioport_readw,  s);

    register_ioport_write(addr, 0x100, 4, rtl8139_ioport_writel, s);
    register_ioport_read( addr, 0x100, 4, rtl8139_ioport_readl,  s);
}

static CPUReadMemoryFunc *rtl8139_mmio_read[3] = {
    rtl8139_mmio_readb,
    rtl8139_mmio_readw,
    rtl8139_mmio_readl,
};

static CPUWriteMemoryFunc *rtl8139_mmio_write[3] = {
    rtl8139_mmio_writeb,
    rtl8139_mmio_writew,
    rtl8139_mmio_writel,
};

static inline int64_t rtl8139_get_next_tctr_time(RTL8139State *s, int64_t current_time)
{
    int64_t next_time = current_time +
        muldiv64(1, ticks_per_sec, PCI_FREQUENCY);
    if (next_time <= current_time)
        next_time = current_time + 1;
    return next_time;
}

#ifdef RTL8139_ONBOARD_TIMER
static void rtl8139_timer(void *opaque)
{
    RTL8139State *s = opaque;

    int is_timeout = 0;

    int64_t  curr_time;
    uint32_t curr_tick;

    if (!s->clock_enabled)
    {
        DEBUG_PRINT(("RTL8139: >>> timer: clock is not running\n"));
        return;
    }

    curr_time = qemu_get_clock(vm_clock);

    curr_tick = muldiv64(curr_time - s->TCTR_base, PCI_FREQUENCY, ticks_per_sec);

    if (s->TimerInt && curr_tick >= s->TimerInt)
    {
        if (s->TCTR < s->TimerInt || curr_tick < s->TCTR)
        {
            is_timeout = 1;
        }
    }

    s->TCTR = curr_tick;

//  DEBUG_PRINT(("RTL8139: >>> timer: tick=%08u\n", s->TCTR));

    if (is_timeout)
    {
        DEBUG_PRINT(("RTL8139: >>> timer: timeout tick=%08u\n", s->TCTR));
        s->IntrStatus |= PCSTimeout;
        rtl8139_update_irq(s);
    }

    qemu_mod_timer(s->timer,
        rtl8139_get_next_tctr_time(s,curr_time));
}
#endif /* RTL8139_ONBOARD_TIMER */

void pci_rtl8139_init(PCIBus *bus, NICInfo *nd, int devfn)
{
    PCIRTL8139State *d;
    RTL8139State *s;
    uint8_t *pci_conf;

    d = (PCIRTL8139State *)pci_register_device(bus,
                                              "RTL8139", sizeof(PCIRTL8139State),
                                              devfn,
                                              NULL, NULL);
    pci_conf = d->dev.config;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_REALTEK);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_REALTEK_8139);
    pci_conf[0x04] = 0x05; /* command = I/O space, Bus Master */
    pci_conf[0x08] = RTL8139_PCI_REVID; /* PCI revision ID; >=0x20 is for 8139C+ */
    pci_config_set_class(pci_conf, PCI_CLASS_NETWORK_ETHERNET);
    pci_conf[0x0e] = 0x00; /* header_type */
    pci_conf[0x3d] = 1;    /* interrupt pin 0 */
    pci_conf[0x34] = 0xdc;

    s = &d->rtl8139;

    /* I/O handler for memory-mapped I/O */
    s->rtl8139_mmio_io_addr =
    cpu_register_io_memory(0, rtl8139_mmio_read, rtl8139_mmio_write, s);

    pci_register_io_region(&d->dev, 0, 0x100,
                           PCI_ADDRESS_SPACE_IO,  rtl8139_ioport_map);

    pci_register_io_region(&d->dev, 1, 0x100,
                           PCI_ADDRESS_SPACE_MEM, rtl8139_mmio_map);

    s->pci_dev = (PCIDevice *)d;
    memcpy(s->macaddr, nd->macaddr, 6);
    rtl8139_reset(s);
    s->vc = qemu_new_vlan_client(nd->vlan, nd->model, nd->name,
                                 rtl8139_receive, rtl8139_can_receive, s);

    qemu_format_nic_info_str(s->vc, s->macaddr);

    s->cplus_txbuffer = NULL;
    s->cplus_txbuffer_len = 0;
    s->cplus_txbuffer_offset = 0;

    register_savevm("rtl8139", -1, 4, rtl8139_save, rtl8139_load, s);

#ifdef RTL8139_ONBOARD_TIMER
    s->timer = qemu_new_timer(vm_clock, rtl8139_timer, s);

    qemu_mod_timer(s->timer,
        rtl8139_get_next_tctr_time(s,qemu_get_clock(vm_clock)));
#endif /* RTL8139_ONBOARD_TIMER */
}
