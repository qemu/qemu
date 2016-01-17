/*
 * QEMU nForce Ethernet Controller implementation
 *
 * Copyright (c) 2013 espes
 * Copyright (c) 2015 Matt Borgerson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "net/net.h"
#include "qemu/iov.h"

#define IOPORT_SIZE 0x8
#define MMIO_SIZE   0x400

#ifdef DEBUG
#   define NVNET_DPRINTF(format, ...) printf(format, ## __VA_ARGS__)
#   define NVNET_DUMP_PACKETS_TO_SCREEN
#else
#   define NVNET_DPRINTF(format, ...) do { } while (0)
#endif

static NetClientInfo net_nvnet_info;
static Property nvnet_properties[];

/*******************************************************************************
 * Various Device Register Definitions (Derived from forcedeth.c)
 ******************************************************************************/

#define DEV_NEED_LASTPACKET1 0x0001
#define DEV_IRQMASK_1        0x0002
#define DEV_IRQMASK_2        0x0004
#define DEV_NEED_TIMERIRQ    0x0008

enum {
    NvRegIrqStatus = 0x000,
#       define NVREG_IRQSTAT_BIT1     0x002
#       define NVREG_IRQSTAT_BIT4     0x010
#       define NVREG_IRQSTAT_MIIEVENT 0x040
#       define NVREG_IRQSTAT_MASK     0x1ff
    NvRegIrqMask = 0x004,
#       define NVREG_IRQ_RX           0x0002
#       define NVREG_IRQ_RX_NOBUF     0x0004
#       define NVREG_IRQ_TX_ERR       0x0008
#       define NVREG_IRQ_TX2          0x0010
#       define NVREG_IRQ_TIMER        0x0020
#       define NVREG_IRQ_LINK         0x0040
#       define NVREG_IRQ_TX1          0x0100
#       define NVREG_IRQMASK_WANTED_1 0x005f
#       define NVREG_IRQMASK_WANTED_2 0x0147
#       define NVREG_IRQ_UNKNOWN      (~(NVREG_IRQ_RX|NVREG_IRQ_RX_NOBUF|\
    NVREG_IRQ_TX_ERR|NVREG_IRQ_TX2|NVREG_IRQ_TIMER|NVREG_IRQ_LINK|\
    NVREG_IRQ_TX1))
    NvRegUnknownSetupReg6 = 0x008,
#       define NVREG_UNKSETUP6_VAL 3
/*
 * NVREG_POLL_DEFAULT is the interval length of the timer source on the nic
 * NVREG_POLL_DEFAULT=97 would result in an interval length of 1 ms
 */
    NvRegPollingInterval = 0x00c,
#       define NVREG_POLL_DEFAULT 970
    NvRegMisc1 = 0x080,
#       define NVREG_MISC1_HD    0x02
#       define NVREG_MISC1_FORCE 0x3b0f3c
    NvRegTransmitterControl = 0x084,
#       define NVREG_XMITCTL_START 0x01
    NvRegTransmitterStatus = 0x088,
#       define NVREG_XMITSTAT_BUSY 0x01
    NvRegPacketFilterFlags = 0x8c,
#       define NVREG_PFF_ALWAYS  0x7F0008
#       define NVREG_PFF_PROMISC 0x80
#       define NVREG_PFF_MYADDR  0x20
    NvRegOffloadConfig = 0x90,
#       define NVREG_OFFLOAD_HOMEPHY 0x601
#       define NVREG_OFFLOAD_NORMAL  0x5ee
    NvRegReceiverControl = 0x094,
#       define NVREG_RCVCTL_START 0x01
    NvRegReceiverStatus = 0x98,
#       define NVREG_RCVSTAT_BUSY  0x01
    NvRegRandomSeed = 0x9c,
#       define NVREG_RNDSEED_MASK  0x00ff
#       define NVREG_RNDSEED_FORCE 0x7f00
    NvRegUnknownSetupReg1 = 0xA0,
#       define NVREG_UNKSETUP1_VAL 0x16070f
    NvRegUnknownSetupReg2 = 0xA4,
#       define NVREG_UNKSETUP2_VAL 0x16
    NvRegMacAddrA = 0xA8,
    NvRegMacAddrB = 0xAC,
    NvRegMulticastAddrA = 0xB0,
#       define NVREG_MCASTADDRA_FORCE  0x01
    NvRegMulticastAddrB = 0xB4,
    NvRegMulticastMaskA = 0xB8,
    NvRegMulticastMaskB = 0xBC,
    NvRegTxRingPhysAddr = 0x100,
    NvRegRxRingPhysAddr = 0x104,
    NvRegRingSizes = 0x108,
#       define NVREG_RINGSZ_TXSHIFT 0
#       define NVREG_RINGSZ_RXSHIFT 16
    NvRegUnknownTransmitterReg = 0x10c,
    NvRegLinkSpeed = 0x110,
#       define NVREG_LINKSPEED_FORCE 0x10000
#       define NVREG_LINKSPEED_10    10
#       define NVREG_LINKSPEED_100   100
#       define NVREG_LINKSPEED_1000  1000
    NvRegUnknownSetupReg5 = 0x130,
#       define NVREG_UNKSETUP5_BIT31 (1<<31)
    NvRegUnknownSetupReg3 = 0x134,
#       define NVREG_UNKSETUP3_VAL1 0x200010
    NvRegUnknownSetupReg8 = 0x13C,
#       define NVREG_UNKSETUP8_VAL1 0x300010
    NvRegUnknownSetupReg7 = 0x140,
#       define NVREG_UNKSETUP7_VAL 0x300010
    NvRegTxRxControl = 0x144,
#       define NVREG_TXRXCTL_KICK  0x0001
#       define NVREG_TXRXCTL_BIT1  0x0002
#       define NVREG_TXRXCTL_BIT2  0x0004
#       define NVREG_TXRXCTL_IDLE  0x0008
#       define NVREG_TXRXCTL_RESET 0x0010
    NvRegMIIStatus = 0x180,
#       define NVREG_MIISTAT_ERROR      0x0001
#       define NVREG_MIISTAT_LINKCHANGE 0x0008
#       define NVREG_MIISTAT_MASK       0x000f
#       define NVREG_MIISTAT_MASK2      0x000f
    NvRegUnknownSetupReg4 = 0x184,
#       define NVREG_UNKSETUP4_VAL 8
    NvRegAdapterControl = 0x188,
#       define NVREG_ADAPTCTL_START    0x02
#       define NVREG_ADAPTCTL_LINKUP   0x04
#       define NVREG_ADAPTCTL_PHYVALID 0x4000
#       define NVREG_ADAPTCTL_RUNNING  0x100000
#       define NVREG_ADAPTCTL_PHYSHIFT 24
    NvRegMIISpeed = 0x18c,
#       define NVREG_MIISPEED_BIT8 (1<<8)
#       define NVREG_MIIDELAY  5
    NvRegMIIControl = 0x190,
#       define NVREG_MIICTL_INUSE 0x10000
#       define NVREG_MIICTL_WRITE 0x08000
#       define NVREG_MIICTL_ADDRSHIFT  5
    NvRegMIIData = 0x194,
    NvRegWakeUpFlags = 0x200,
#       define NVREG_WAKEUPFLAGS_VAL               0x7770
#       define NVREG_WAKEUPFLAGS_BUSYSHIFT         24
#       define NVREG_WAKEUPFLAGS_ENABLESHIFT       16
#       define NVREG_WAKEUPFLAGS_D3SHIFT           12
#       define NVREG_WAKEUPFLAGS_D2SHIFT           8
#       define NVREG_WAKEUPFLAGS_D1SHIFT           4
#       define NVREG_WAKEUPFLAGS_D0SHIFT           0
#       define NVREG_WAKEUPFLAGS_ACCEPT_MAGPAT     0x01
#       define NVREG_WAKEUPFLAGS_ACCEPT_WAKEUPPAT  0x02
#       define NVREG_WAKEUPFLAGS_ACCEPT_LINKCHANGE 0x04
    NvRegPatternCRC = 0x204,
    NvRegPatternMask = 0x208,
    NvRegPowerCap = 0x268,
#       define NVREG_POWERCAP_D3SUPP (1<<30)
#       define NVREG_POWERCAP_D2SUPP (1<<26)
#       define NVREG_POWERCAP_D1SUPP (1<<25)
    NvRegPowerState = 0x26c,
#       define NVREG_POWERSTATE_POWEREDUP 0x8000
#       define NVREG_POWERSTATE_VALID     0x0100
#       define NVREG_POWERSTATE_MASK      0x0003
#       define NVREG_POWERSTATE_D0        0x0000
#       define NVREG_POWERSTATE_D1        0x0001
#       define NVREG_POWERSTATE_D2        0x0002
#       define NVREG_POWERSTATE_D3        0x0003
};

#define NV_TX_LASTPACKET      (1<<0)
#define NV_TX_RETRYERROR      (1<<3)
#define NV_TX_LASTPACKET1     (1<<8)
#define NV_TX_DEFERRED        (1<<10)
#define NV_TX_CARRIERLOST     (1<<11)
#define NV_TX_LATECOLLISION   (1<<12)
#define NV_TX_UNDERFLOW       (1<<13)
#define NV_TX_ERROR           (1<<14)
#define NV_TX_VALID           (1<<15)
#define NV_RX_DESCRIPTORVALID (1<<0)
#define NV_RX_MISSEDFRAME     (1<<1)
#define NV_RX_SUBSTRACT1      (1<<3)
#define NV_RX_BIT4            (1<<4)
#define NV_RX_ERROR1          (1<<7)
#define NV_RX_ERROR2          (1<<8)
#define NV_RX_ERROR3          (1<<9)
#define NV_RX_ERROR4          (1<<10)
#define NV_RX_CRCERR          (1<<11)
#define NV_RX_OVERFLOW        (1<<12)
#define NV_RX_FRAMINGERR      (1<<13)
#define NV_RX_ERROR           (1<<14)
#define NV_RX_AVAIL           (1<<15)

/* Miscelaneous hardware related defines: */
#define NV_PCI_REGSZ          0x270

/* various timeout delays: all in usec */
#define NV_TXRX_RESET_DELAY   4
#define NV_TXSTOP_DELAY1      10
#define NV_TXSTOP_DELAY1MAX   500000
#define NV_TXSTOP_DELAY2      100
#define NV_RXSTOP_DELAY1      10
#define NV_RXSTOP_DELAY1MAX   500000
#define NV_RXSTOP_DELAY2      100
#define NV_SETUP5_DELAY       5
#define NV_SETUP5_DELAYMAX    50000
#define NV_POWERUP_DELAY      5
#define NV_POWERUP_DELAYMAX   5000
#define NV_MIIBUSY_DELAY      50
#define NV_MIIPHY_DELAY       10
#define NV_MIIPHY_DELAYMAX    10000
#define NV_WAKEUPPATTERNS     5
#define NV_WAKEUPMASKENTRIES  4

/* General driver defaults */
#define NV_WATCHDOG_TIMEO     (2*HZ)
#define DEFAULT_MTU           1500

#define RX_RING               4
#define TX_RING               2
/* limited to 1 packet until we understand NV_TX_LASTPACKET */
#define TX_LIMIT_STOP         10
#define TX_LIMIT_START        5

/* rx/tx mac addr + type + vlan + align + slack*/
#define RX_NIC_BUFSIZE        (DEFAULT_MTU + 64)
/* even more slack */
#define RX_ALLOC_BUFSIZE      (DEFAULT_MTU + 128)

#define OOM_REFILL            (1+HZ/20)
#define POLL_WAIT             (1+HZ/100)

#define MII_READ      (-1)
#define MII_PHYSID1   0x02    /* PHYS ID 1                   */
#define MII_PHYSID2   0x03    /* PHYS ID 2                   */
#define MII_BMCR      0x00    /* Basic mode control register */
#define MII_BMSR      0x01    /* Basic mode status register  */
#define MII_ADVERTISE 0x04    /* Advertisement control reg   */
#define MII_LPA       0x05    /* Link partner ability reg    */

#define BMSR_ANEGCOMPLETE 0x0020 /* Auto-negotiation complete   */
#define BMSR_BIT2         0x0004 /* Unknown... */

/* Link partner ability register. */
#define LPA_SLCT     0x001f  /* Same as advertise selector  */
#define LPA_10HALF   0x0020  /* Can do 10mbps half-duplex   */
#define LPA_10FULL   0x0040  /* Can do 10mbps full-duplex   */
#define LPA_100HALF  0x0080  /* Can do 100mbps half-duplex  */
#define LPA_100FULL  0x0100  /* Can do 100mbps full-duplex  */
#define LPA_100BASE4 0x0200  /* Can do 100mbps 4k packets   */
#define LPA_RESV     0x1c00  /* Unused...                   */
#define LPA_RFAULT   0x2000  /* Link partner faulted        */
#define LPA_LPACK    0x4000  /* Link partner acked us       */
#define LPA_NPAGE    0x8000  /* Next page bit               */

/*******************************************************************************
 * Primary State Structure
 ******************************************************************************/

typedef struct NvNetState {
    PCIDevice    dev;
    NICState     *nic;
    NICConf      conf;
    MemoryRegion mmio, io;
    uint8_t      regs[MMIO_SIZE/4];
    uint32_t     phy_regs[6];
    uint8_t      tx_ring_index;
    uint8_t      tx_ring_size;
    uint8_t      rx_ring_index;
    uint8_t      rx_ring_size;
    uint8_t      txrx_dma_buf[RX_ALLOC_BUFSIZE];
    FILE         *packet_dump_file;
    char         *packet_dump_path;
} NvNetState;

struct RingDesc {
    uint32_t packet_buffer;
    uint16_t length;
    uint16_t flags;
};

/*******************************************************************************
 * Helper Macros
 ******************************************************************************/

#define NVNET_DEVICE(obj) \
    OBJECT_CHECK(NvNetState, (obj), "nvnet")

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/* Init */
static int nvnet_initfn(PCIDevice *dev);
static void nvnet_uninit(PCIDevice *dev);
static void nvnet_class_init(ObjectClass *klass, void *data);
static void nvnet_cleanup(NetClientState *nc);
static void nvnet_reset(void *opaque);
static void qdev_nvnet_reset(DeviceState *dev);
static void nvnet_class_init(ObjectClass *klass, void *data);
static void nvnet_register(void);

/* MMIO / IO / Phy / Device Register Access */
static uint64_t nvnet_mmio_read(void *opaque,
    hwaddr addr, unsigned int size);
static void nvnet_mmio_write(void *opaque,
    hwaddr addr, uint64_t val, unsigned int size);
static uint32_t nvnet_get_reg(NvNetState *s,
    hwaddr addr, unsigned int size);
static void nvnet_set_reg(NvNetState *s,
    hwaddr addr, uint32_t val, unsigned int size);
static uint64_t nvnet_io_read(void *opaque,
    hwaddr addr, unsigned int size);
static void nvnet_io_write(void *opaque,
    hwaddr addr, uint64_t val, unsigned int size);
static int nvnet_mii_rw(NvNetState *s,
    uint64_t val);

/* Link State */
static void nvnet_link_down(NvNetState *s);
static void nvnet_link_up(NvNetState *s);
static void nvnet_set_link_status(NetClientState *nc);

/* Interrupts */
static void nvnet_update_irq(NvNetState *s);

/* Packet Tx/Rx */
static void nvnet_send_packet(NvNetState *s,
    const uint8_t *buf, int size);
static ssize_t nvnet_dma_packet_to_guest(NvNetState *s,
    const uint8_t *buf, size_t size);
static ssize_t nvnet_dma_packet_from_guest(NvNetState *s);
static int nvnet_can_receive(NetClientState *nc);
static ssize_t nvnet_receive(NetClientState *nc,
    const uint8_t *buf, size_t size);
static ssize_t nvnet_receive_iov(NetClientState *nc,
    const struct iovec *iov, int iovcnt);

/* Utility Functions */
static void nvnet_hex_dump(NvNetState *s, const uint8_t *buf, int size);

#ifdef DEBUG
static const char *nvnet_get_reg_name(hwaddr addr);
static const char *nvnet_get_mii_reg_name(uint8_t reg);
#endif

/*******************************************************************************
 * IRQ
 ******************************************************************************/

/*
 * Update IRQ status
 */
static void nvnet_update_irq(NvNetState *s)
{
    if (nvnet_get_reg(s, NvRegIrqMask,   4) &&
        nvnet_get_reg(s, NvRegIrqStatus, 4)) {
        NVNET_DPRINTF("Asserting IRQ\n");
        pci_irq_assert(&s->dev);
    } else {
        pci_irq_deassert(&s->dev);
    }
}

/*******************************************************************************
 * Register Control
 ******************************************************************************/

/*
 * Read backing store for a device register.
 */
static uint32_t nvnet_get_reg(NvNetState *s, hwaddr addr, unsigned int size)
{
    switch (size) {
    case 4:
        assert((addr & 3) == 0); /* Unaligned register access. */
        return ((uint32_t *)s->regs)[addr>>2];

    case 2:
        assert((addr & 1) == 0); /* Unaligned register access. */
        return ((uint16_t *)s->regs)[addr>>1];

    case 1:
        return s->regs[addr];

    default:
        assert(0); /* Unsupported register access. */
    }
}

/*
 * Write backing store for a device register.
 */
static void nvnet_set_reg(NvNetState *s,
                          hwaddr addr, uint32_t val, unsigned int size)
{
    switch (size) {
    case 4:
        assert((addr & 3) == 0); /* Unaligned register access. */
        ((uint32_t *)s->regs)[addr>>2] = val;
        break;

    case 2:
        assert((addr & 1) == 0); /* Unaligned register access. */
        ((uint16_t *)s->regs)[addr>>1] = (uint16_t)val;
        break;

    case 1:
        s->regs[addr] = (uint8_t)val;
        break;

    default:
        assert(0); /* Unsupported register access. */
    }
}

/*******************************************************************************
 * PHY Control
 ******************************************************************************/

/*
 * Read from PHY.
 */
static int nvnet_mii_rw(NvNetState *s, uint64_t val)
{
    uint32_t mii_ctl;
    int write, retval, phy_addr, reg;

    retval   = 0;
    mii_ctl  = nvnet_get_reg(s, NvRegMIIControl, 4);
    phy_addr = (mii_ctl >> NVREG_MIICTL_ADDRSHIFT) & 0x1f;
    reg      = mii_ctl & ((1 << NVREG_MIICTL_ADDRSHIFT) - 1);
    write    = mii_ctl & NVREG_MIICTL_WRITE;

    NVNET_DPRINTF("nvnet mii %s: phy 0x%x %s [0x%x]\n",
        write ? "write" : "read", phy_addr, nvnet_get_mii_reg_name(reg), reg);

    if (phy_addr != 1) {
        return -1;
    }

    if (write) {
        return retval;
    }

    switch (reg) {
    case MII_BMSR:
        /* Phy initialization code waits for BIT2 to be set.. If not set,
         * software may report controller as not running */
        retval = BMSR_ANEGCOMPLETE | BMSR_BIT2;
        break;

    case MII_ADVERTISE:
        /* Fall through... */

    case MII_LPA:
        retval = LPA_10HALF | LPA_10FULL;
        retval |= LPA_100HALF | LPA_100FULL | LPA_100BASE4;
        break;

    default:
        break;
    }

    return retval;
}

/*******************************************************************************
 * MMIO Read/Write
 ******************************************************************************/

/*
 * Handler for guest reads from MMIO ranges owned by this device.
 */
static uint64_t nvnet_mmio_read(void *opaque, hwaddr addr, unsigned int size)
{
    NvNetState *s;
    uint64_t retval;

    s = NVNET_DEVICE(opaque);

    switch (addr) {
    case NvRegMIIData:
        assert(size == 4);
        retval = nvnet_mii_rw(s, MII_READ);
        break;

    case NvRegMIIControl:
        retval = nvnet_get_reg(s, addr, size);
        retval &= ~NVREG_MIICTL_INUSE;
        break;

    case NvRegMIIStatus:
        retval = 0;
        break;

    default:
        retval = nvnet_get_reg(s, addr, size);
        break;
    }

    NVNET_DPRINTF("nvnet mmio: read %s [0x%llx] <- 0x%llx\n",
        nvnet_get_reg_name(addr & ~3), addr, retval);

    return retval;
}

/*
 * Handler for guest writes to MMIO ranges owned by this device.
 */
static void nvnet_mmio_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    NvNetState *s;
    uint32_t temp;

    s = NVNET_DEVICE(opaque);

    NVNET_DPRINTF("nvnet mmio: write %s [0x%llx] = 0x%llx\n",
        nvnet_get_reg_name(addr & ~3), addr, val);

    switch (addr) {
    case NvRegRingSizes:
        nvnet_set_reg(s, addr, val, size);
        s->rx_ring_size = ((val >> NVREG_RINGSZ_RXSHIFT) & 0xffff)+1;
        s->tx_ring_size = ((val >> NVREG_RINGSZ_TXSHIFT) & 0xffff)+1;
        break;

    case NvRegMIIData:
        nvnet_mii_rw(s, val);
        break;

    case NvRegTxRxControl:
        if (val == NVREG_TXRXCTL_KICK) {
            NVNET_DPRINTF("NvRegTxRxControl = NVREG_TXRXCTL_KICK!\n");
            nvnet_dma_packet_from_guest(s);
        }

        if (val & NVREG_TXRXCTL_BIT2) {
            nvnet_set_reg(s, NvRegTxRxControl, NVREG_TXRXCTL_IDLE, 4);
            break;
        }

        if (val & NVREG_TXRXCTL_BIT1) {
            nvnet_set_reg(s, NvRegIrqStatus, 0, 4);
            break;
        } else if (val == 0) {
            temp = nvnet_get_reg(s, NvRegUnknownSetupReg3, 4);
            if (temp == NVREG_UNKSETUP3_VAL1) {
                /* forcedeth waits for this bit to be set... */
                nvnet_set_reg(s, NvRegUnknownSetupReg5,
                                 NVREG_UNKSETUP5_BIT31, 4);
                break;
            }
        }

        nvnet_set_reg(s, NvRegTxRxControl, val, size);
        break;

    case NvRegIrqMask:
        nvnet_set_reg(s, addr, val, size);
        nvnet_update_irq(s);
        break;

    case NvRegIrqStatus:
        nvnet_set_reg(s, addr, nvnet_get_reg(s, addr, size) & ~val, size);
        nvnet_update_irq(s);
        break;

    default:
        nvnet_set_reg(s, addr, val, size);
        break;
    }
}

static const MemoryRegionOps nvnet_mmio_ops = {
    .read = nvnet_mmio_read,
    .write = nvnet_mmio_write,
};

/*******************************************************************************
 * Packet TX/RX
 ******************************************************************************/

static void nvnet_send_packet(NvNetState *s, const uint8_t *buf, int size)
{
    NetClientState *nc = qemu_get_queue(s->nic);

    NVNET_DPRINTF("nvnet: Sending packet!\n");
    nvnet_hex_dump(s, buf, size);
    qemu_send_packet(nc, buf, size);
}

static int nvnet_can_receive(NetClientState *nc)
{
    NVNET_DPRINTF("nvnet_can_receive called\n");
    return 1;
}

static ssize_t nvnet_receive(NetClientState *nc,
                             const uint8_t *buf, size_t size)
{
    const struct iovec iov = {
        .iov_base = (uint8_t *)buf,
        .iov_len = size
    };

    NVNET_DPRINTF("nvnet_receive called\n");
    return nvnet_receive_iov(nc, &iov, 1);
}

static ssize_t nvnet_receive_iov(NetClientState *nc,
                                 const struct iovec *iov, int iovcnt)
{
    NvNetState *s = qemu_get_nic_opaque(nc);
    size_t size = iov_size(iov, iovcnt);

    NVNET_DPRINTF("nvnet: Packet received!\n");

    if (size > sizeof(s->txrx_dma_buf)) {
        NVNET_DPRINTF("nvnet_receive_iov packet too large!\n");
        assert(0);
        return -1;
    }

    iov_to_buf(iov, iovcnt, 0, s->txrx_dma_buf, size);
    nvnet_hex_dump(s, s->txrx_dma_buf, size);
    return nvnet_dma_packet_to_guest(s, s->txrx_dma_buf, size);
}

static ssize_t nvnet_dma_packet_to_guest(NvNetState *s,
                                         const uint8_t *buf, size_t size)
{
    struct RingDesc desc;
    int i;

    for (i = 0; i < s->rx_ring_size; i++) {
        /* Read current ring descriptor */
        s->rx_ring_index %= s->rx_ring_size;
        dma_addr_t rx_ring_addr = nvnet_get_reg(s, NvRegRxRingPhysAddr, 4);
        rx_ring_addr += s->rx_ring_index*sizeof(desc);
        pci_dma_read(&s->dev, rx_ring_addr, &desc, sizeof(desc));
        NVNET_DPRINTF("Looking at ring descriptor %d (0x%llx): ",
                      s->rx_ring_index, rx_ring_addr);
        NVNET_DPRINTF("Buffer: 0x%x, ", desc.packet_buffer);
        NVNET_DPRINTF("Length: 0x%x, ", desc.length);
        NVNET_DPRINTF("Flags: 0x%x\n", desc.flags);

        s->rx_ring_index += 1;

        if (!(desc.flags & NV_RX_AVAIL) || !(desc.length >= size)) {
            continue;
        }

        /* Transfer packet from device to memory */
        NVNET_DPRINTF("Transferring packet, size 0x%zx, to memory at 0x%x\n",
                      size, desc.packet_buffer);
        pci_dma_write(&s->dev, desc.packet_buffer, buf, size);

        /* Update descriptor indicating the packet is waiting */
        desc.length = size;
        desc.flags  = NV_RX_BIT4 | NV_RX_DESCRIPTORVALID;
        pci_dma_write(&s->dev, rx_ring_addr, &desc, sizeof(desc));
        NVNET_DPRINTF("Updated ring descriptor: ");
        NVNET_DPRINTF("Length: 0x%x, ", desc.length);
        NVNET_DPRINTF("Flags: 0x%x\n", desc.flags);

        /* Trigger interrupt */
        NVNET_DPRINTF("Triggering interrupt\n");
        nvnet_set_reg(s, NvRegIrqStatus, NVREG_IRQSTAT_BIT1, 4);
        nvnet_update_irq(s);
        return size;
    }

    /* Could not find free buffer, or packet too large. */
    NVNET_DPRINTF("Could not find free buffer!\n");
    return -1;
}

static ssize_t nvnet_dma_packet_from_guest(NvNetState *s)
{
    struct RingDesc desc;
    bool is_last_packet;
    int i;

    for (i = 0; i < s->tx_ring_size; i++) {
        /* Read ring descriptor */
        s->tx_ring_index %= s->tx_ring_size;
        dma_addr_t tx_ring_addr = nvnet_get_reg(s, NvRegTxRingPhysAddr, 4);
        tx_ring_addr += s->tx_ring_index * sizeof(desc);
        pci_dma_read(&s->dev, tx_ring_addr, &desc, sizeof(desc));
        NVNET_DPRINTF("Looking at ring desc %d (%llx): ",
                      s->tx_ring_index, tx_ring_addr);
        NVNET_DPRINTF("Buffer: 0x%x, ", desc.packet_buffer);
        NVNET_DPRINTF("Length: 0x%x, ", desc.length);
        NVNET_DPRINTF("Flags: 0x%x\n", desc.flags);

        s->tx_ring_index += 1;

        if (!(desc.flags & NV_TX_VALID)) {
            continue;
        }

        /* Transfer packet from guest memory */
        NVNET_DPRINTF("Sending packet...\n");
        pci_dma_read(&s->dev, desc.packet_buffer,
                              s->txrx_dma_buf, desc.length+1);
        nvnet_send_packet(s, s->txrx_dma_buf, desc.length+1);

        /* Update descriptor */
        is_last_packet = desc.flags & NV_TX_LASTPACKET;
        desc.flags &= ~(NV_TX_VALID | NV_TX_RETRYERROR | NV_TX_DEFERRED |
            NV_TX_CARRIERLOST | NV_TX_LATECOLLISION | NV_TX_UNDERFLOW |
            NV_TX_ERROR);
        desc.length = desc.length+5;
        pci_dma_write(&s->dev, tx_ring_addr, &desc, sizeof(desc));

        if (is_last_packet) {
            NVNET_DPRINTF("  -- Last packet\n");
            break;
        }
    }

    /* Trigger interrupt */
    NVNET_DPRINTF("Triggering interrupt\n");
    nvnet_set_reg(s, NvRegIrqStatus, NVREG_IRQSTAT_BIT4, 4);
    nvnet_update_irq(s);

    return 0;
}

/*******************************************************************************
 * Link Status Control
 ******************************************************************************/

static void nvnet_link_down(NvNetState *s)
{
    NVNET_DPRINTF("nvnet_link_down called\n");
}

static void nvnet_link_up(NvNetState *s)
{
    NVNET_DPRINTF("nvnet_link_up called\n");
}

static void nvnet_set_link_status(NetClientState *nc)
{
    NvNetState *s = qemu_get_nic_opaque(nc);
    if (nc->link_down) {
        nvnet_link_down(s);
    } else {
        nvnet_link_up(s);
    }
}

/*******************************************************************************
 * IO Read/Write
 ******************************************************************************/

static uint64_t nvnet_io_read(void *opaque, hwaddr addr, unsigned int size)
{
    NVNET_DPRINTF("nvnet io: read [0x%llx]\n", addr);
    return 0;
}

static void nvnet_io_write(void *opaque,
                           hwaddr addr, uint64_t val, unsigned int size)
{
    NVNET_DPRINTF("nvnet io: [0x%llx] = 0x%llx\n", addr, val);
}

static const MemoryRegionOps nvnet_io_ops = {
    .read  = nvnet_io_read,
    .write = nvnet_io_write,
};

/*******************************************************************************
 * Init
 ******************************************************************************/

static int nvnet_initfn(PCIDevice *pci_dev)
{
    DeviceState *dev = DEVICE(pci_dev);
    NvNetState *s = NVNET_DEVICE(pci_dev);

    pci_dev->config[PCI_INTERRUPT_PIN] = 0x01;

    s->packet_dump_file = NULL;
    if (s->packet_dump_path && *s->packet_dump_path != '\x00') {
        s->packet_dump_file = fopen(s->packet_dump_path, "wb");
        if (!s->packet_dump_file) {
            fprintf(stderr, "Failed to open %s for writing!\n",
                            s->packet_dump_path);
            return -1;
        }
    }

    memset(s->regs, 0, sizeof(s->regs));

    s->rx_ring_index = 0;
    s->rx_ring_size  = 0;
    s->tx_ring_index = 0;
    s->tx_ring_size  = 0;

    memory_region_init_io(&s->mmio, OBJECT(dev), &nvnet_mmio_ops, s,
        "nvnet-mmio", MMIO_SIZE);
    pci_register_bar(&s->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    memory_region_init_io(&s->io, OBJECT(dev), &nvnet_io_ops, s,
        "nvnet-io", IOPORT_SIZE);
    pci_register_bar(&s->dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->io);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_nvnet_info, &s->conf,
        object_get_typename(OBJECT(s)), dev->id, s);
    assert(s->nic);

    s->regs[NvRegMacAddrA+0x00] = s->conf.macaddr.a[0];
    s->regs[NvRegMacAddrA+0x01] = s->conf.macaddr.a[1];
    s->regs[NvRegMacAddrA+0x02] = s->conf.macaddr.a[2];
    s->regs[NvRegMacAddrA+0x03] = s->conf.macaddr.a[3];
    s->regs[NvRegMacAddrB+0x00] = s->conf.macaddr.a[4];
    s->regs[NvRegMacAddrB+0x01] = s->conf.macaddr.a[5];

    return 0;
}

static void nvnet_uninit(PCIDevice *dev)
{
    NvNetState *s = NVNET_DEVICE(dev);

    if (s->packet_dump_file) {
        fclose(s->packet_dump_file);
    }

    memory_region_destroy(&s->mmio);
    memory_region_destroy(&s->io);
    qemu_del_nic(s->nic);
}

void nvnet_cleanup(NetClientState *nc)
{
}

static void nvnet_reset(void *opaque)
{
    NvNetState *s = opaque;

    if (qemu_get_queue(s->nic)->link_down) {
        nvnet_link_down(s);
    }
}

static void qdev_nvnet_reset(DeviceState *dev)
{
    NvNetState *s = NVNET_DEVICE(dev);
    nvnet_reset(s);
}

/*******************************************************************************
 * Utility Functions
 ******************************************************************************/

static void hex_dump(FILE *f, const uint8_t *buf, int size)
{
    int len, i, j, c;

    for (i = 0; i < size; i += 16) {
        len = size - i;
        if (len > 16) {
            len = 16;
        }
        fprintf(f, "%08x ", i);
        for (j = 0; j < 16; j++) {
            if (j < len) {
                fprintf(f, " %02x", buf[i+j]);
            } else {
                fprintf(f, "   ");
            }
        }
        fprintf(f, " ");
        for (j = 0; j < len; j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~') {
                c = '.';
            }
            fprintf(f, "%c", c);
        }
        fprintf(f, "\n");
    }
}

static void nvnet_hex_dump(NvNetState *s, const uint8_t *buf, int size)
{
#ifdef NVNET_DUMP_PACKETS_TO_SCREEN
    hex_dump(stdout, buf, size);
#endif
    if (s->packet_dump_file) {
        hex_dump(s->packet_dump_file, buf, size);
    }
}

#ifdef DEBUG
/*
 * Return register name given the offset of the device register.
 */
static const char *nvnet_get_reg_name(hwaddr addr)
{
    switch (addr) {
    case NvRegIrqStatus:             return "NvRegIrqStatus";
    case NvRegIrqMask:               return "NvRegIrqMask";
    case NvRegUnknownSetupReg6:      return "NvRegUnknownSetupReg6";
    case NvRegPollingInterval:       return "NvRegPollingInterval";
    case NvRegMisc1:                 return "NvRegMisc1";
    case NvRegTransmitterControl:    return "NvRegTransmitterControl";
    case NvRegTransmitterStatus:     return "NvRegTransmitterStatus";
    case NvRegPacketFilterFlags:     return "NvRegPacketFilterFlags";
    case NvRegOffloadConfig:         return "NvRegOffloadConfig";
    case NvRegReceiverControl:       return "NvRegReceiverControl";
    case NvRegReceiverStatus:        return "NvRegReceiverStatus";
    case NvRegRandomSeed:            return "NvRegRandomSeed";
    case NvRegUnknownSetupReg1:      return "NvRegUnknownSetupReg1";
    case NvRegUnknownSetupReg2:      return "NvRegUnknownSetupReg2";
    case NvRegMacAddrA:              return "NvRegMacAddrA";
    case NvRegMacAddrB:              return "NvRegMacAddrB";
    case NvRegMulticastAddrA:        return "NvRegMulticastAddrA";
    case NvRegMulticastAddrB:        return "NvRegMulticastAddrB";
    case NvRegMulticastMaskA:        return "NvRegMulticastMaskA";
    case NvRegMulticastMaskB:        return "NvRegMulticastMaskB";
    case NvRegTxRingPhysAddr:        return "NvRegTxRingPhysAddr";
    case NvRegRxRingPhysAddr:        return "NvRegRxRingPhysAddr";
    case NvRegRingSizes:             return "NvRegRingSizes";
    case NvRegUnknownTransmitterReg: return "NvRegUnknownTransmitterReg";
    case NvRegLinkSpeed:             return "NvRegLinkSpeed";
    case NvRegUnknownSetupReg5:      return "NvRegUnknownSetupReg5";
    case NvRegUnknownSetupReg3:      return "NvRegUnknownSetupReg3";
    case NvRegUnknownSetupReg8:      return "NvRegUnknownSetupReg8";
    case NvRegUnknownSetupReg7:      return "NvRegUnknownSetupReg7";
    case NvRegTxRxControl:           return "NvRegTxRxControl";
    case NvRegMIIStatus:             return "NvRegMIIStatus";
    case NvRegUnknownSetupReg4:      return "NvRegUnknownSetupReg4";
    case NvRegAdapterControl:        return "NvRegAdapterControl";
    case NvRegMIISpeed:              return "NvRegMIISpeed";
    case NvRegMIIControl:            return "NvRegMIIControl";
    case NvRegMIIData:               return "NvRegMIIData";
    case NvRegWakeUpFlags:           return "NvRegWakeUpFlags";
    case NvRegPatternCRC:            return "NvRegPatternCRC";
    case NvRegPatternMask:           return "NvRegPatternMask";
    case NvRegPowerCap:              return "NvRegPowerCap";
    case NvRegPowerState:            return "NvRegPowerState";
    default:                         return "Unknown";
    }
}
#endif


#ifdef DEBUG
/*
 * Get PHY register name.
 */
static const char *nvnet_get_mii_reg_name(uint8_t reg)
{
    switch (reg) {
    case MII_PHYSID1:   return "MII_PHYSID1";
    case MII_PHYSID2:   return "MII_PHYSID2";
    case MII_BMCR:      return "MII_BMCR";
    case MII_BMSR:      return "MII_BMSR";
    case MII_ADVERTISE: return "MII_ADVERTISE";
    case MII_LPA:       return "MII_LPA";
    default:            return "Unknown";
    }
}
#endif

/*******************************************************************************
 * Properties
 ******************************************************************************/

static void nvnet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_NVENET_1;
    k->revision  = 210;
    k->class_id  = PCI_CLASS_NETWORK_ETHERNET;
    k->init      = nvnet_initfn;
    k->exit      = nvnet_uninit;

    dc->desc  = "nForce Ethernet Controller";
    dc->reset = qdev_nvnet_reset;
    dc->props = nvnet_properties;
}

static Property nvnet_properties[] = {
    DEFINE_NIC_PROPERTIES(NvNetState, conf),
    DEFINE_PROP_STRING("dump", NvNetState, packet_dump_path),
    DEFINE_PROP_END_OF_LIST(),
};

static NetClientInfo net_nvnet_info = {
    .type                = NET_CLIENT_OPTIONS_KIND_NIC,
    .size                = sizeof(NICState),
    .can_receive         = nvnet_can_receive,
    .receive             = nvnet_receive,
    .receive_iov         = nvnet_receive_iov,
    .cleanup             = nvnet_cleanup,
    .link_status_changed = nvnet_set_link_status,
};

static const TypeInfo nvnet_info = {
    .name                = "nvnet",
    .parent              = TYPE_PCI_DEVICE,
    .instance_size       = sizeof(NvNetState),
    .class_init          = nvnet_class_init,
};

static void nvnet_register(void)
{
    type_register_static(&nvnet_info);
}
type_init(nvnet_register);

