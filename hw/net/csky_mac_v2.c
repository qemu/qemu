/*
 * CSKY GMAC IP emulation.
 *
 * Author: wanghb
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include <zlib.h>
#include "qemu/log.h"
#include "cpu.h"
#include "hw/csky/cskydev.h"
#include "hw/ptimer.h"
#include "sysemu/sysemu.h"

#define CSKY_BUS_WIDTH                  32    /* 32 bit ahb bus */

#define TYPE_CSKY_MAC_V2 "csky_mac_v2"
#define CSKY_MAC_V2(obj) \
    OBJECT_CHECK(csky_mac_v2_state, (obj), TYPE_CSKY_MAC_V2)
#define CSKY_MAC_V2_FREQ                40000000ll

/* buffer descriptor*/
typedef struct {
    uint32_t status1;
    uint32_t status2;
    uint32_t buffer1;
    uint32_t buffer2;
} csky_mac_v2_bd;

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    ptimer_state *timer;
    /* MAC Reg */
    uint32_t config;
    uint32_t frame_filter;
    uint32_t hash_tab_high;
    uint32_t hash_tab_low;
    uint32_t mii_addr;
    uint32_t mii_data;
    uint32_t debug;
    uint32_t int_status;
    uint32_t int_mask;
    uint32_t watchdog_timeout;
    /* MAC MDA Reg */
    uint32_t bus_mode;
    uint32_t tx_poll_demand;
    uint32_t rx_poll_demand;
    uint32_t rx_des_list_addr;
    uint32_t tx_des_list_addr;
    uint32_t status;
    uint32_t operation_mode;
    uint32_t int_en;
    uint32_t missed_frame_buf_flow_ctrl;
    uint32_t rx_int_watchdog_timer;
    uint32_t ahb_axi_status;
    uint32_t cur_tx_des_addr;
    uint32_t cur_rx_des_addr;
    uint32_t cur_tx_buf_addr;
    uint32_t cur_rx_buf_addr;
} csky_mac_v2_state;

static const VMStateDescription vmstate_csky_mac_v2 = {
    .name = "csky_mac_v2",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(config, csky_mac_v2_state),
        VMSTATE_UINT32(frame_filter, csky_mac_v2_state),
        VMSTATE_UINT32(hash_tab_high, csky_mac_v2_state),
        VMSTATE_UINT32(hash_tab_low, csky_mac_v2_state),
        VMSTATE_UINT32(mii_addr, csky_mac_v2_state),
        VMSTATE_UINT32(mii_data, csky_mac_v2_state),
        VMSTATE_UINT32(debug, csky_mac_v2_state),
        VMSTATE_UINT32(int_status, csky_mac_v2_state),
        VMSTATE_UINT32(int_mask, csky_mac_v2_state),
        VMSTATE_UINT32(watchdog_timeout, csky_mac_v2_state),
        VMSTATE_UINT32(bus_mode, csky_mac_v2_state),
        VMSTATE_UINT32(tx_poll_demand, csky_mac_v2_state),
        VMSTATE_UINT32(rx_poll_demand, csky_mac_v2_state),
        VMSTATE_UINT32(rx_des_list_addr, csky_mac_v2_state),
        VMSTATE_UINT32(tx_des_list_addr, csky_mac_v2_state),
        VMSTATE_UINT32(status, csky_mac_v2_state),
        VMSTATE_UINT32(operation_mode, csky_mac_v2_state),
        VMSTATE_UINT32(int_en, csky_mac_v2_state),
        VMSTATE_UINT32(missed_frame_buf_flow_ctrl, csky_mac_v2_state),
        VMSTATE_UINT32(rx_int_watchdog_timer, csky_mac_v2_state),
        VMSTATE_UINT32(ahb_axi_status, csky_mac_v2_state),
        VMSTATE_UINT32(cur_tx_des_addr, csky_mac_v2_state),
        VMSTATE_UINT32(cur_rx_des_addr, csky_mac_v2_state),
        VMSTATE_UINT32(cur_tx_buf_addr, csky_mac_v2_state),
        VMSTATE_UINT32(cur_rx_buf_addr, csky_mac_v2_state),
        VMSTATE_END_OF_LIST()
    }
};

/* configuration register */
#define CONFIG_2kPACK                   (0x1 << 27)
#define CONFIG_CRC_STRIP                (0x1 << 25)
#define CONFIG_JUMBO_FRAME              (0x1 << 20)
#define CONFIG_LOOPBACK                 (0x1 << 12)
#define CONFIG_COE                      (0x1 << 10)  /* checksum offload */
#define CONFIG_PAD_CRC_STRIP            (0x1 << 07)
#define CONFIG_TXEN                     (0x1 << 03)
#define CONFIG_RXEN                     (0x1 << 02)

/* frame filter reg */
#define FILTER_REC_ALL                  (0x1 << 31)
#define FILTER_PROMISCUOUS              (0x1 << 00)
#define FILTER_BROADCAST_DIS            (0x1 << 05)
#define FILTER_MULTICAST_EN             (0x1 << 04)

/* mii addr reg */
#define MII_PHY_ADDR                    (0x1f << 11)
#define MII_REG_NUM                     (0x1f << 6)
#define MII_WRITE                       (0x1 << 01)
#define MII_BUSY                        (0x1 << 00)

/* debug reg */
#define DEBUG_TXFIFO_FULL               (0x1 << 25)
#define DEBUG_TXFIFO_NOT_EMPTY          (0x1 << 24)
#define DEBUG_RXFIFO_STATUS             (0x3 << 8)
#define DEBUG_RXFIFO_FULL               (0x3 << 8)

/* interrupt status reg */
#define INT_STATUS_MII_CHANGE           (0x1 << 0)

/* mac address high reg */
#define MACADDR_ENABLE                  (0x1 << 31)

/* bus mode reg */
#define BUSMODE_DSL                     (0x1f << 2) /* descriptor skip length */
#define BUSMODE_RESET                   (0x1 << 00)

/* status reg */
#define STATUS_LINK_CHANGE              (0x1 << 26)
#define STATUS_TX_STATE_STOPPED         (0x0 << 20)
#define STATUS_TX_STATE_RUNNING         (0x3 << 20)
#define STATUS_TX_STATE_SUSPEND         (0x6 << 20)
#define STATUS_RX_STATE_STOPPED         (0x0 << 17)
#define STATUS_RX_STATE_RUNNING         (0x3 << 17)
#define STATUS_RX_STATE_SUSPEND         (0x4 << 17)
#define STATUS_NORMAL_INT               (0x1 << 16)
#define STATUS_ABNORMAL_INT             (0x1 << 15)
#define STATUS_RX_BUF_UNAVAILABLE       (0x1 << 07)
#define STATUS_RX_INT                   (0x1 << 06)
#define STATUS_TX_UNDERFLOW             (0x1 << 05)
#define STATUS_TX_BUF_UNAVAILABLE       (0x1 << 02)
#define STATUS_TX_INT                   (0x1 << 00)

/* operation mode reg */
#define OPMODE_START_TX                 (0x1 << 13)
#define OPMODE_FW_ERR_FRAME             (0x1 << 07)
#define OPMODE_FW_SMALL_FRAME           (0x1 << 06)
#define OPMODE_START_RX                 (0x1 << 01)

/* interrupt enable reg */
#define INT_NORMAL_EN                   (0x1 << 16)
#define INT_ABNORMAL_EN                 (0x1 << 15)
#define INT_RX_BUF_UNAVAILABLE_EN       (0x1 << 07)
#define INT_RX_EN                       (0x1 << 06)
#define INT_TX_BUF_UNAVAILABLE_EN       (0x1 << 02)
#define INT_TX_EN                       (0x1 << 00)

/* transmit descriptor 1 */
#define TXBD_OWN                        (0x1 << 31)
#define TXBD_IPHEADER_ERR               (0x1 << 16)
#define TXBD_ERR_SUMMARY                (0x1 << 15)
#define TXBD_CHECKSUM_ERR               (0x1 << 12)
#define TXBD_UNDERFLOW_ERR              (0x1 << 01)

/* transmit descriptor 2 */
#define TXBD_IC              (0x1 << 31)   /* interrupt on completion */
#define TXBD_LS              (0x1 << 30)   /* last segment */
#define TXBD_FS              (0x1 << 29)   /* first segment */
#define TXBD_CIC             (0x3 << 27)   /* checksum insertion control */
#define TXBD_CRC_DIS         (0x1 << 26)
#define TXBD_TER             (0x1 << 25)   /* transmit end of ring */
#define TXBD_TCH             (0x1 << 24)   /* second addr chained */
#define TXBD_DP              (0x1 << 23)   /* disable padding */
#define TXBD_BUF2_SIZE       (0x7ff << 11) /* buf2 size */
#define TXBD_BUF1_SIZE       (0x7ff << 0)  /* buf1 size */

/* receive descriptor 1 */
#define RXBD_OWN             (0x1 << 31)
#define RXBD_DAFF            (0x1 << 30) /* dest addr filter fail */
#define RXBD_ES              (0x1 << 15) /* error summary */
#define RXBD_DE              (0x1 << 14) /* descriptor error */
#define RXBD_LE              (0x1 << 12) /* length error */
#define RXBD_FS              (0x1 << 9)  /* first segment */
#define RXBD_LS              (0x1 << 8)  /* last segment */
#define RXBD_CE_GF           (0x1 << 7)  /* IPC checksum error or giant frame */
#define RXBD_FT              (0x1 << 5)  /* frame type */
#define RXBD_ERR             (0x1 << 3)  /* receive error */
#define RXBD_DBE             (0x1 << 2)  /* dribble bit error */
#define RXBD_CRC_ERR         (0x1 << 1)
#define RXBD_MAC_ADDR_ERR    (0x1 << 0)  /* rx mac addr or payload
                                            checksum error */

/*receive descriptor 2 */
#define RXBD_IC_DIS       (0x1 << 31)   /* disable interrupt on completion */
#define RXBD_RER          (0x1 << 25)   /* receive end of ring */
#define RXBD_RCH          (0x1 << 24)   /* second addr chained */
#define RXBD_BUF2_SIZE    (0x7ff << 11) /* buf2 size */
#define RXBD_BUF1_SIZE    (0x7ff << 0)  /* buf1 size */

static ssize_t csky_mac_v2_receive(NetClientState *s, const uint8_t *buf,
                                   size_t size);

static inline void csky_mac_v2_reset(csky_mac_v2_state *s);


/**************************************************************************
 * Description:
 *     Update the interrupt flag according the MAC state and
 *     give the flag to interrupt controller.
 * Argument:
 *     s  --- the pointer to the MAC state
 * Return:
 *     void
 **************************************************************************/
static void csky_mac_v2_update(csky_mac_v2_state *s)
{
    int level;

    level = !!(s->status & s->int_en);
    qemu_set_irq(s->irq, level);
}

/**************************************************************************
 * Description:
 *     Read a MAC register according to the offset.
 * Argument:
 *     opaque  --- the pointer to the MAC state
 *     offset  --- the offset from the base address
 * Return:
 *     the data which is read from the corrsponding register
 **************************************************************************/
static uint64_t csky_mac_v2_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_mac_v2_state *s = (csky_mac_v2_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mac_v2_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
        /* MAC Reg */
    case 0x0:
        ret = s->config & ~CONFIG_COE;
        break;
    case 0x4:
        ret = s->frame_filter;
        break;
    case 0x8:
        ret = s->hash_tab_high;
        break;
    case 0xc:
        ret = s->hash_tab_low;
        break;
    case 0x10:
        ret = s->mii_addr;
        break;
    case 0x14:
        ret = s->mii_data;
        break;
    case 0x20:
        ret = 0x1037;
        break;
    case 0x24:
        ret = s->debug;
        break;
    case 0x38:
        ret = s->int_status;
        break;
    case 0x3c:
        ret = s->int_mask;
        break;
    case 0x40:
        ret = (MACADDR_ENABLE | s->conf.macaddr.a[0] << 8) |
            (s->conf.macaddr.a[1]);
        break;
    case 0x44:
        ret = (s->conf.macaddr.a[2] << 24) | (s->conf.macaddr.a[3] << 16)
            | (s->conf.macaddr.a[4] << 8) | s->conf.macaddr.a[5];
        break;
    case 0xdc:
        ret = s->watchdog_timeout;
        qemu_log("watchdog timeout 0x%lx\n",ret);
        break;
        /* MAC DMA Reg */
    case 0x1000:
        ret = s->bus_mode;
        break;
    case 0x1004:
        ret = s->tx_poll_demand;
        break;
    case 0x1008:
        ret = s->rx_poll_demand;
        break;
    case 0x100c:
        ret = s->rx_des_list_addr;
        break;
    case 0x1010:
        ret = s->tx_des_list_addr;
        break;
    case 0x1014:
        ret = s->status;
        break;
    case 0x1018:
        ret = s->operation_mode;
        break;
    case 0x101c:
        ret = s->int_en;
        break;
    case 0x1020:   /* missed_frame_buf_flow_ctrl register
                      is cleared when it is read */
        ret = s->missed_frame_buf_flow_ctrl;
        s->missed_frame_buf_flow_ctrl = 0x0;
        break;
    case 0x1024:
        ret = s->rx_int_watchdog_timer;
        break;
    case 0x102c:
        ret = s->ahb_axi_status;
        break;
    case 0x1048:
        ret = s->cur_tx_des_addr;
        break;
    case 0x104c:
        ret = s->cur_rx_des_addr;
        break;
    case 0x1050:
        ret = s->cur_tx_buf_addr;
        break;
    case 0x1054:
        ret = s->cur_tx_buf_addr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mac_v2_read: Bad offset 0x%x\n", (int)offset);
        ret = 0;
        break;
    }

    return ret;
}

/**************************************************************************
 * Description:
 *     Send a frame according to the tx bd.
 * Argument:
 *     s  --- the pointer to the MAC state
 * Return:
 *     void
 **************************************************************************/

static void csky_mac_v2_release_packet(csky_mac_v2_state *s)
{
    int size;
    uint8_t *p;
    uint8_t frame[1518];
    csky_mac_v2_bd cur_tx_bd;
    int end_of_ring;
    NetClientState *nc = qemu_get_queue(s->nic);
    int en_int;

    p = frame;
    int len=0;
    while (1) {

        /* acquire current tx bd, save the important control bits */
        cpu_physical_memory_read(s->cur_tx_des_addr, (uint8_t *)&cur_tx_bd, 16);
        en_int           = cur_tx_bd.status2 & TXBD_IC;
        end_of_ring      = cur_tx_bd.status2 & TXBD_TER;
        size             = cur_tx_bd.status2 & TXBD_BUF1_SIZE;

        assert(size <= 1518 && len <= 1518);
        if ((cur_tx_bd.status1 & TXBD_OWN) == 0) {
            s->status |= STATUS_TX_BUF_UNAVAILABLE | STATUS_NORMAL_INT;
            s->status |= STATUS_TX_STATE_SUSPEND;
            break;
        }
        cpu_physical_memory_read(cur_tx_bd.buffer1, p, size);
        p += size;
        len +=size;
        if (cur_tx_bd.status2 & TXBD_LS) {
            qemu_send_packet(nc, frame, len);
            cur_tx_bd.status1 &= ~TXBD_OWN;
            cpu_physical_memory_write(s->cur_tx_des_addr, (uint8_t *)&cur_tx_bd, 16);
            if (en_int) {
                s->status |= STATUS_TX_INT | STATUS_NORMAL_INT;
                csky_mac_v2_update(s);
            }
            p = frame;
            len = 0;
        }
        else {
            cur_tx_bd.status1 &= ~TXBD_OWN;
            cpu_physical_memory_write(s->cur_tx_des_addr, (uint8_t *)&cur_tx_bd, 16);
        }
        if (end_of_ring) {
            s->cur_tx_des_addr = s->tx_des_list_addr;
        } else {
            s->cur_tx_des_addr += (s->bus_mode & BUSMODE_DSL) + 16;
        }

    }

    csky_mac_v2_update(s);
}


/**************************************************************************
 * Description:
 *     Read the PHY register.
 * Argument:
 *     phy_addr --- the PHY address (0 to 31), for this case, is 1
 *     phy_reg  --- the PHY register number.
 * Return:
 *     the data being read(always 0)
 **************************************************************************/
static uint32_t csky_phy_read(uint32_t phy_addr, uint32_t phy_reg)
{
    if (phy_addr != 0x1) {
        return 0xffff;
    }
    switch (phy_reg) {
    case 0:
        return 0x2100;
    case 1:
        return 0x786d;
    case 3:
        return 0x8201;
    case 4:
        return 0x0100;
    case 5:
        return 0x0100;
    case 17:
        return 0x0080;
    case 19:
        return 0x0023;
    case 25:
        return 0x0101;
    default:
        return 0x0;
    }

}

/**************************************************************************
 * Description:
 *     Write to the PHY register.
 * Argument:
 *     phy_addr --- the PHY address (0 to 31), for this case, is 1
 *     phy_reg  --- the PHY register number.
 *     value    --- the value which will be written to PHY
 * Return:
 *     void
 **************************************************************************/
static inline void csky_phy_write(uint32_t phy_addr, uint32_t phy_reg,
                                  uint32_t value)
{
    return;
}

/**************************************************************************
 * Description:
 *     Write to the MAC register.
 * Argument:
 *     opaque --- the pointer to to MAC state
 *     offset --- the offset from the base address
 *     value  --- the value which will be written to the register
 * Return:
 *     void
 **************************************************************************/
static void csky_mac_v2_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    csky_mac_v2_state *s = (csky_mac_v2_state *)opaque;
    csky_mac_v2_bd cur_bd;
    uint8_t *p_cur_bd = (uint8_t *)&cur_bd;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mac_v2_write: 0x%x must word align write\n",
                      (int)offset);
    }
    switch (offset) {
        /* MAC Reg */
    case 0x0:
        if (!(s->config & CONFIG_RXEN) && (value & CONFIG_RXEN)) {
            s->cur_rx_des_addr = s->rx_des_list_addr;
        }
        if (!(s->config & CONFIG_TXEN) && (value & CONFIG_TXEN)) {
            s->cur_tx_des_addr = s->tx_des_list_addr;
        }
        s->config = value;
        return;
    case 0x4:
        s->frame_filter = value;
        return;
    case 0x8:
        s->hash_tab_high = value;
        return;
    case 0xc:
        s->hash_tab_low = value;
        return;
    case 0x10:
        s->mii_addr = value;
        /* try to write phy reg */
        if (s->mii_addr & MII_WRITE) {
            csky_phy_write((s->mii_addr & MII_PHY_ADDR) >> 11,
                           (s->mii_addr & MII_REG_NUM) >> 6, s->mii_data);
        /* try to read phy reg, update the mii_data value */
        } else {
            s->mii_data = csky_phy_read((s->mii_addr & MII_PHY_ADDR) >> 11,
                                        (s->mii_addr & MII_REG_NUM) >> 6);
        }
        s->mii_addr &= ~MII_BUSY;   /* MII returns to idle state after
                                       read/write process. */
        return;
    case 0x14:
        s->mii_data = value;
        return;
    case 0x24: /* debug register read only */
        return;
    case 0x38: /* int_status register read only */
        return;
    case 0x3c: /* the corresponding interrupt source is
                  not realized in hardware configuration. */
        s->int_mask = value;
        return;
    case 0x40:
        s->conf.macaddr.a[2] = value >> 24;
        s->conf.macaddr.a[3] = value >> 16;
        s->conf.macaddr.a[4] = value >> 8;
        s->conf.macaddr.a[5] = value;
        return;
    case 0x44:
        s->conf.macaddr.a[0] = value >> 8;
        s->conf.macaddr.a[1] = value;
        return;
    case 0xdc:
        s->watchdog_timeout = value;
        return;
    case 0x100:
        return;
    case 0x10c:
        return;
    case 0x110:
        return;
    case 0x1000:
        s->bus_mode = value;
        if (s->bus_mode & BUSMODE_RESET) {
            csky_mac_v2_reset(s);
        }
        return;
    case 0x1004:  /* tx_poll_demand register read only */
        if ( s->operation_mode & OPMODE_START_TX && s->config & CONFIG_TXEN){
             s->status |= STATUS_TX_STATE_RUNNING;
             csky_mac_v2_release_packet(s);
        }
        return;
    case 0x1008:  /* rx_poll_demand register read only */
        cpu_physical_memory_read(s->cur_rx_des_addr, p_cur_bd, 16);
        if (cur_bd.status1 & RXBD_OWN) {
            s->status &= ~STATUS_RX_BUF_UNAVAILABLE;
            if (!(s->status & STATUS_TX_UNDERFLOW)) {
                s->status &= ~STATUS_ABNORMAL_INT;
            }
        } else {
            s->status |= STATUS_RX_BUF_UNAVAILABLE | STATUS_ABNORMAL_INT;
        }
        csky_mac_v2_update(s);
        return;
    case 0x100c:
        /* the lowest two bits are always 0 for 32-bit bus width. */
        s->rx_des_list_addr = value & (~0x3);
        if (!(s->operation_mode & OPMODE_START_RX)) {
            s->cur_rx_des_addr = s->rx_des_list_addr;
        }
        return;
    case 0x1010:
        /* the lowest two bits are always 0 for 32-bit bus width. */
        s->tx_des_list_addr = value & (~0x3);
        if (!(s->operation_mode & OPMODE_START_TX)) {
            s->cur_tx_des_addr = s->tx_des_list_addr;
        }
        return;
    case 0x1014: /* status register */
        s->status = ((s->status & 0x7fff) & (~value)) |
            (s->status & 0xfffe0000);
        if (s->status & (STATUS_TX_BUF_UNAVAILABLE | STATUS_TX_INT |
                         STATUS_RX_INT)) {
            s->status |= STATUS_NORMAL_INT;
        } else {
            s->status &= ~STATUS_NORMAL_INT;
        }

        if (s->status & (STATUS_RX_BUF_UNAVAILABLE | STATUS_TX_UNDERFLOW)) {
            s->status |= STATUS_ABNORMAL_INT;
        } else {
            s->status &= ~STATUS_ABNORMAL_INT;
        }
        csky_mac_v2_update(s);
        return;
    case 0x1018:  /* operation mode register */
        if (!(s->operation_mode & OPMODE_START_TX) &&
            (value & OPMODE_START_TX)) {        
           
            s->operation_mode = value & OPMODE_START_TX; 
            s->status |= STATUS_TX_STATE_RUNNING;
        } else if ((s->operation_mode & OPMODE_START_TX) &&
                   !(value & OPMODE_START_TX)) {
            s->status |= STATUS_TX_STATE_STOPPED;
        }

        if (!(s->operation_mode & OPMODE_START_RX) &&
            (value & OPMODE_START_RX)) {
            cpu_physical_memory_read(s->cur_rx_des_addr, p_cur_bd, 16);
            if (cur_bd.status1 & RXBD_OWN) {
                s->status &= ~STATUS_RX_BUF_UNAVAILABLE;
                if ((!(s->status & STATUS_TX_UNDERFLOW))) {
                    s->status &= ~STATUS_ABNORMAL_INT;
                }
                s->status |= STATUS_RX_STATE_RUNNING;
            } else {
                s->status |= STATUS_RX_BUF_UNAVAILABLE | STATUS_ABNORMAL_INT;
                s->status |= STATUS_RX_STATE_SUSPEND;
            }
            csky_mac_v2_update(s);
        } else if ((s->operation_mode & OPMODE_START_RX) &&
                   !(value & OPMODE_START_RX)) {
            s->status |= STATUS_RX_STATE_STOPPED;
        }

        s->operation_mode = value;
        return;
    case 0x101c:
        s->int_en = value;
        csky_mac_v2_update(s);
        return;
    case 0x1020: /* missed_frame_buf_flow_ctrl register bit is set internal */
        return;
    case 0x1024:
        s->rx_int_watchdog_timer = value & 0xff;
        return;
    case 0x102c:   /* ahb_axi_status read only */
        return;
    case 0x1048:   /* cur_tx_des_addr read only */
        return;
    case 0x104c:   /* cur_rx_des_addr read only */
        return;
    case 0x1050:   /* cur_tx_buf_addr read only */
        return;
    case 0x1054:   /* cur_rx_buf_addr read only */
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_mac_v2_write: Bad offset 0x%x\n", (int)offset);
        return;
    }
}

static const MemoryRegionOps csky_mac_v2_ops = {
    .read = csky_mac_v2_read,
    .write = csky_mac_v2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/**************************************************************************
 * Description:
 *     Decide whether MAC can receive packet.
 * Argument:
 *     nc --- the pointer to the VLAN Client state
 * Return:
 *     1  --- can receive
 *     0  --- can not receive
 **************************************************************************/
/*
static int csky_mac_v2_can_receive(NetClientState *nc)
{
    csky_mac_v2_state *s = qemu_get_nic_opaque(nc);

    if ((s->operation_mode & OPMODE_START_RX)
        && !(s->status & STATUS_RX_BUF_UNAVAILABLE)
        && (s->config & CONFIG_RXEN)) {
        return 1;
    }
    return 0;
}
*/
/**************************************************************************
 * Description:
 *     Drop some unqualified packets according to control bits
 * Argument:
 *     s  --- the pointer to the MAC state
 *     buf -- the pointer to the packet
 *     size -- the packet size
 * Return:
 *     -1  --- unqualified
 *     0   --- qualified
 **************************************************************************/
/*
static int csky_mac_v2_receive_filter(csky_mac_v2_state *s, const uint8_t *buf,
                                      size_t size)
{
    if (s->frame_filter & FILTER_REC_ALL) return 0;
    if (s->frame_filter & FILTER_PROMISCUOUS) return 0;
    if (s->frame_filter & FILTER_BROADCAST_DIS) {
        if ((buf[0] == 0xff)
            && (buf[1] == 0xff)
            && (buf[2] == 0xff)
            && (buf[3] == 0xff)
            && (buf[4] == 0xff)
            && (buf[5] == 0xff))
            return -1;
        return 0;
    }
    if (s->frame_filter & ~FILTER_MULTICAST_EN) {
        if ((buf[0] & 0x1) == 1) return -1;
        return 0;
    }
}
*/
/**************************************************************************
 * Description:
 *     Receive a packet.
 * Argument:
 *     nc  --- the pointer to the VLAN Client state.
 *     buf -- the pointer to the packet
 *     size -- the packet size
 * Return:
 *     the total size of the packet
 **************************************************************************/
static ssize_t csky_mac_v2_receive(NetClientState *nc, const uint8_t *buf,
                                   size_t size)
{
    csky_mac_v2_state *s = qemu_get_nic_opaque(nc);
    csky_mac_v2_bd cur_rx_bd;
    int dis_int, end_of_ring;

    if (!((s->operation_mode & OPMODE_START_RX)
        && !(s->status & STATUS_RX_BUF_UNAVAILABLE)
        && (s->config & CONFIG_RXEN))) {
        return -1;
    }
    /* acquire current rx bd, save the important bits */
    assert( size <= 1518 );
    cpu_physical_memory_read(s->cur_rx_des_addr, (uint8_t *)&cur_rx_bd, 16);
    dis_int        = cur_rx_bd.status2 & RXBD_IC_DIS;
    end_of_ring    = cur_rx_bd.status2 & RXBD_RER;

    if ((cur_rx_bd.status1 & RXBD_OWN) == 0){
        if (end_of_ring) {
            s->cur_rx_des_addr = s->rx_des_list_addr;
        } else {
            s->cur_rx_des_addr += (s->bus_mode & BUSMODE_DSL) + 16;
        }
        return -1;
    }
    cpu_physical_memory_write(cur_rx_bd.buffer1, buf, size);
    cur_rx_bd.status1 &= ~0x3fff0000;
    cur_rx_bd.status1 |= (size + 4) << 16;
    cur_rx_bd.status1 |= RXBD_FS | RXBD_LS;
    cur_rx_bd.status1 &= ~RXBD_OWN;
    cpu_physical_memory_write(s->cur_rx_des_addr, (uint8_t *)&cur_rx_bd, 16);
    if (!dis_int) {
        s->status |= STATUS_RX_INT | STATUS_NORMAL_INT;
        csky_mac_v2_update(s);
        /* disabled timer before it runs out */
        ptimer_stop(s->timer);
    }
    /* if non-zero,ritw get activated */
    if (s->rx_int_watchdog_timer != 0 && ptimer_get_count(s->timer) == 0){
        ptimer_set_limit(s->timer, s->rx_int_watchdog_timer*256 ,0);
        ptimer_set_freq(s->timer, CSKY_MAC_V2_FREQ);
        ptimer_run(s->timer,1);
    }
    /*
    s->status |= STATUS_RX_INT | STATUS_NORMAL_INT;
    csky_mac_v2_update(s);
    */
    if (end_of_ring) {
        s->cur_rx_des_addr = s->rx_des_list_addr;
    } else {
        s->cur_rx_des_addr += (s->bus_mode & BUSMODE_DSL) + 16;
    }

    return size;
}

/**************************************************************************
 * Description
 *     Asserting RI when riwt runs out.
 * Argument:
 *     opaque  --- the pointer to the csky_mac_v2_state.
 * Return:
 *     void
 ***************************************************************************/
static void csky_mac_v2_timer_tick(void *opaque){
    csky_mac_v2_state *s = (csky_mac_v2_state*)opaque;
    s->status |= STATUS_RX_INT | STATUS_NORMAL_INT;
    csky_mac_v2_update(s);
}

/**************************************************************************
 * Description:
 *     Clean up the created MAC.
 * Argument:
 *     nc  --- the pointer to the VLAN Client state.
 * Return:
 *     void
 ***************************************************************************/
static void csky_mac_v2_cleanup(NetClientState *nc)
{
    csky_mac_v2_state *s = qemu_get_nic_opaque(nc);

    s->nic = NULL;
}

static NetClientInfo net_csky_mac_v2_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
/*    .can_receive = csky_mac_v2_can_receive,*/
    .receive = csky_mac_v2_receive,
    .cleanup = csky_mac_v2_cleanup,
};

/**************************************************************************
 * Description:
 *     Reset the MAC controller.
 * Argument:
 *     s  --- the pointer to the MAC state
 * Return:
 *     void
 ***************************************************************************/
static inline void csky_mac_v2_reset(csky_mac_v2_state *s)
{
    memset(&s->config, 0, 25 * sizeof(uint32_t));
    s->bus_mode = 0x00020100;
}

/**************************************************************************
 * Description:
 *     Initial the MAC controller.
 * Argument:
 *     dev  --- the pointer to the MAC state
 * Return:
 *     SUCCESS or FAILURE
 ***************************************************************************/
static int csky_mac_v2_init(SysBusDevice *sbd)
{
    QEMUBH *bh;
    DeviceState *dev = DEVICE(sbd);
    csky_mac_v2_state *s = CSKY_MAC_V2(dev);

    memory_region_init_io(&s->mmio, OBJECT(s), &csky_mac_v2_ops, s,
                          TYPE_CSKY_MAC_V2, 0x2000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&net_csky_mac_v2_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    csky_mac_v2_reset(s);
    bh = qemu_bh_new(csky_mac_v2_timer_tick, s);
    s->timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);
    return 0;
}

static Property csky_mac_v2_properties[] = {
    DEFINE_NIC_PROPERTIES(csky_mac_v2_state, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void csky_mac_v2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = csky_mac_v2_init;
    dc->props = csky_mac_v2_properties;
    dc->vmsd = &vmstate_csky_mac_v2;
}

static const TypeInfo csky_mac_v2_info = {
    .name          = TYPE_CSKY_MAC_V2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_mac_v2_state),
    .class_init    = csky_mac_v2_class_init,
};

static void csky_mac_v2_register_types(void)
{
    type_register_static(&csky_mac_v2_info);
}

/**************************************************************************
 * Description:
 *     Create the MAC controller.
 * Argument:
 *     nd   --- the pointer to the MAC infomation
 *     base --- the base address of the MAC mmio address
 *     irq  --- the interrupt request signal line
 * Return:
 *     void
 ***************************************************************************/

void csky_mac_v2_create(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    qemu_check_nic_model(nd, "csky_mac_v2");
    dev = qdev_create(NULL, "csky_mac_v2");
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

type_init(csky_mac_v2_register_types)
