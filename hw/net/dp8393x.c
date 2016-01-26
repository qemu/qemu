/*
 * QEMU NS SONIC DP8393x netcard
 *
 * Copyright (c) 2008-2009 Herve Poussineau
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/devices.h"
#include "net/net.h"
#include "qemu/timer.h"
#include <zlib.h>

//#define DEBUG_SONIC

#define SONIC_PROM_SIZE 0x1000

#ifdef DEBUG_SONIC
#define DPRINTF(fmt, ...) \
do { printf("sonic: " fmt , ##  __VA_ARGS__); } while (0)
static const char* reg_names[] = {
    "CR", "DCR", "RCR", "TCR", "IMR", "ISR", "UTDA", "CTDA",
    "TPS", "TFC", "TSA0", "TSA1", "TFS", "URDA", "CRDA", "CRBA0",
    "CRBA1", "RBWC0", "RBWC1", "EOBC", "URRA", "RSA", "REA", "RRP",
    "RWP", "TRBA0", "TRBA1", "0x1b", "0x1c", "0x1d", "0x1e", "LLFA",
    "TTDA", "CEP", "CAP2", "CAP1", "CAP0", "CE", "CDP", "CDC",
    "SR", "WT0", "WT1", "RSC", "CRCT", "FAET", "MPT", "MDT",
    "0x30", "0x31", "0x32", "0x33", "0x34", "0x35", "0x36", "0x37",
    "0x38", "0x39", "0x3a", "0x3b", "0x3c", "0x3d", "0x3e", "DCR2" };
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define SONIC_ERROR(fmt, ...) \
do { printf("sonic ERROR: %s: " fmt, __func__ , ## __VA_ARGS__); } while (0)

#define SONIC_CR     0x00
#define SONIC_DCR    0x01
#define SONIC_RCR    0x02
#define SONIC_TCR    0x03
#define SONIC_IMR    0x04
#define SONIC_ISR    0x05
#define SONIC_UTDA   0x06
#define SONIC_CTDA   0x07
#define SONIC_TPS    0x08
#define SONIC_TFC    0x09
#define SONIC_TSA0   0x0a
#define SONIC_TSA1   0x0b
#define SONIC_TFS    0x0c
#define SONIC_URDA   0x0d
#define SONIC_CRDA   0x0e
#define SONIC_CRBA0  0x0f
#define SONIC_CRBA1  0x10
#define SONIC_RBWC0  0x11
#define SONIC_RBWC1  0x12
#define SONIC_EOBC   0x13
#define SONIC_URRA   0x14
#define SONIC_RSA    0x15
#define SONIC_REA    0x16
#define SONIC_RRP    0x17
#define SONIC_RWP    0x18
#define SONIC_TRBA0  0x19
#define SONIC_TRBA1  0x1a
#define SONIC_LLFA   0x1f
#define SONIC_TTDA   0x20
#define SONIC_CEP    0x21
#define SONIC_CAP2   0x22
#define SONIC_CAP1   0x23
#define SONIC_CAP0   0x24
#define SONIC_CE     0x25
#define SONIC_CDP    0x26
#define SONIC_CDC    0x27
#define SONIC_SR     0x28
#define SONIC_WT0    0x29
#define SONIC_WT1    0x2a
#define SONIC_RSC    0x2b
#define SONIC_CRCT   0x2c
#define SONIC_FAET   0x2d
#define SONIC_MPT    0x2e
#define SONIC_MDT    0x2f
#define SONIC_DCR2   0x3f

#define SONIC_CR_HTX     0x0001
#define SONIC_CR_TXP     0x0002
#define SONIC_CR_RXDIS   0x0004
#define SONIC_CR_RXEN    0x0008
#define SONIC_CR_STP     0x0010
#define SONIC_CR_ST      0x0020
#define SONIC_CR_RST     0x0080
#define SONIC_CR_RRRA    0x0100
#define SONIC_CR_LCAM    0x0200
#define SONIC_CR_MASK    0x03bf

#define SONIC_DCR_DW     0x0020
#define SONIC_DCR_LBR    0x2000
#define SONIC_DCR_EXBUS  0x8000

#define SONIC_RCR_PRX    0x0001
#define SONIC_RCR_LBK    0x0002
#define SONIC_RCR_FAER   0x0004
#define SONIC_RCR_CRCR   0x0008
#define SONIC_RCR_CRS    0x0020
#define SONIC_RCR_LPKT   0x0040
#define SONIC_RCR_BC     0x0080
#define SONIC_RCR_MC     0x0100
#define SONIC_RCR_LB0    0x0200
#define SONIC_RCR_LB1    0x0400
#define SONIC_RCR_AMC    0x0800
#define SONIC_RCR_PRO    0x1000
#define SONIC_RCR_BRD    0x2000
#define SONIC_RCR_RNT    0x4000

#define SONIC_TCR_PTX    0x0001
#define SONIC_TCR_BCM    0x0002
#define SONIC_TCR_FU     0x0004
#define SONIC_TCR_EXC    0x0040
#define SONIC_TCR_CRSL   0x0080
#define SONIC_TCR_NCRS   0x0100
#define SONIC_TCR_EXD    0x0400
#define SONIC_TCR_CRCI   0x2000
#define SONIC_TCR_PINT   0x8000

#define SONIC_ISR_RBE    0x0020
#define SONIC_ISR_RDE    0x0040
#define SONIC_ISR_TC     0x0080
#define SONIC_ISR_TXDN   0x0200
#define SONIC_ISR_PKTRX  0x0400
#define SONIC_ISR_PINT   0x0800
#define SONIC_ISR_LCD    0x1000

#define TYPE_DP8393X "dp8393x"
#define DP8393X(obj) OBJECT_CHECK(dp8393xState, (obj), TYPE_DP8393X)

typedef struct dp8393xState {
    SysBusDevice parent_obj;

    /* Hardware */
    uint8_t it_shift;
    qemu_irq irq;
#ifdef DEBUG_SONIC
    int irq_level;
#endif
    QEMUTimer *watchdog;
    int64_t wt_last_update;
    NICConf conf;
    NICState *nic;
    MemoryRegion mmio;
    MemoryRegion prom;

    /* Registers */
    uint8_t cam[16][6];
    uint16_t regs[0x40];

    /* Temporaries */
    uint8_t tx_buffer[0x10000];
    int loopback_packet;

    /* Memory access */
    void *dma_mr;
    AddressSpace as;
} dp8393xState;

static void dp8393x_update_irq(dp8393xState *s)
{
    int level = (s->regs[SONIC_IMR] & s->regs[SONIC_ISR]) ? 1 : 0;

#ifdef DEBUG_SONIC
    if (level != s->irq_level) {
        s->irq_level = level;
        if (level) {
            DPRINTF("raise irq, isr is 0x%04x\n", s->regs[SONIC_ISR]);
        } else {
            DPRINTF("lower irq\n");
        }
    }
#endif

    qemu_set_irq(s->irq, level);
}

static void dp8393x_do_load_cam(dp8393xState *s)
{
    uint16_t data[8];
    int width, size;
    uint16_t index = 0;

    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;
    size = sizeof(uint16_t) * 4 * width;

    while (s->regs[SONIC_CDC] & 0x1f) {
        /* Fill current entry */
        address_space_rw(&s->as,
            (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_CDP],
            MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 0);
        s->cam[index][0] = data[1 * width] & 0xff;
        s->cam[index][1] = data[1 * width] >> 8;
        s->cam[index][2] = data[2 * width] & 0xff;
        s->cam[index][3] = data[2 * width] >> 8;
        s->cam[index][4] = data[3 * width] & 0xff;
        s->cam[index][5] = data[3 * width] >> 8;
        DPRINTF("load cam[%d] with %02x%02x%02x%02x%02x%02x\n", index,
            s->cam[index][0], s->cam[index][1], s->cam[index][2],
            s->cam[index][3], s->cam[index][4], s->cam[index][5]);
        /* Move to next entry */
        s->regs[SONIC_CDC]--;
        s->regs[SONIC_CDP] += size;
        index++;
    }

    /* Read CAM enable */
    address_space_rw(&s->as,
        (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_CDP],
        MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 0);
    s->regs[SONIC_CE] = data[0 * width];
    DPRINTF("load cam done. cam enable mask 0x%04x\n", s->regs[SONIC_CE]);

    /* Done */
    s->regs[SONIC_CR] &= ~SONIC_CR_LCAM;
    s->regs[SONIC_ISR] |= SONIC_ISR_LCD;
    dp8393x_update_irq(s);
}

static void dp8393x_do_read_rra(dp8393xState *s)
{
    uint16_t data[8];
    int width, size;

    /* Read memory */
    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;
    size = sizeof(uint16_t) * 4 * width;
    address_space_rw(&s->as,
        (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_RRP],
        MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 0);

    /* Update SONIC registers */
    s->regs[SONIC_CRBA0] = data[0 * width];
    s->regs[SONIC_CRBA1] = data[1 * width];
    s->regs[SONIC_RBWC0] = data[2 * width];
    s->regs[SONIC_RBWC1] = data[3 * width];
    DPRINTF("CRBA0/1: 0x%04x/0x%04x, RBWC0/1: 0x%04x/0x%04x\n",
        s->regs[SONIC_CRBA0], s->regs[SONIC_CRBA1],
        s->regs[SONIC_RBWC0], s->regs[SONIC_RBWC1]);

    /* Go to next entry */
    s->regs[SONIC_RRP] += size;

    /* Handle wrap */
    if (s->regs[SONIC_RRP] == s->regs[SONIC_REA]) {
        s->regs[SONIC_RRP] = s->regs[SONIC_RSA];
    }

    /* Check resource exhaustion */
    if (s->regs[SONIC_RRP] == s->regs[SONIC_RWP])
    {
        s->regs[SONIC_ISR] |= SONIC_ISR_RBE;
        dp8393x_update_irq(s);
    }

    /* Done */
    s->regs[SONIC_CR] &= ~SONIC_CR_RRRA;
}

static void dp8393x_do_software_reset(dp8393xState *s)
{
    timer_del(s->watchdog);

    s->regs[SONIC_CR] &= ~(SONIC_CR_LCAM | SONIC_CR_RRRA | SONIC_CR_TXP | SONIC_CR_HTX);
    s->regs[SONIC_CR] |= SONIC_CR_RST | SONIC_CR_RXDIS;
}

static void dp8393x_set_next_tick(dp8393xState *s)
{
    uint32_t ticks;
    int64_t delay;

    if (s->regs[SONIC_CR] & SONIC_CR_STP) {
        timer_del(s->watchdog);
        return;
    }

    ticks = s->regs[SONIC_WT1] << 16 | s->regs[SONIC_WT0];
    s->wt_last_update = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    delay = get_ticks_per_sec() * ticks / 5000000;
    timer_mod(s->watchdog, s->wt_last_update + delay);
}

static void dp8393x_update_wt_regs(dp8393xState *s)
{
    int64_t elapsed;
    uint32_t val;

    if (s->regs[SONIC_CR] & SONIC_CR_STP) {
        timer_del(s->watchdog);
        return;
    }

    elapsed = s->wt_last_update - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    val = s->regs[SONIC_WT1] << 16 | s->regs[SONIC_WT0];
    val -= elapsed / 5000000;
    s->regs[SONIC_WT1] = (val >> 16) & 0xffff;
    s->regs[SONIC_WT0] = (val >> 0)  & 0xffff;
    dp8393x_set_next_tick(s);

}

static void dp8393x_do_start_timer(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_STP;
    dp8393x_set_next_tick(s);
}

static void dp8393x_do_stop_timer(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_ST;
    dp8393x_update_wt_regs(s);
}

static int dp8393x_can_receive(NetClientState *nc);

static void dp8393x_do_receiver_enable(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_RXDIS;
    if (dp8393x_can_receive(s->nic->ncs)) {
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
    }
}

static void dp8393x_do_receiver_disable(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_RXEN;
}

static void dp8393x_do_transmit_packets(dp8393xState *s)
{
    NetClientState *nc = qemu_get_queue(s->nic);
    uint16_t data[12];
    int width, size;
    int tx_len, len;
    uint16_t i;

    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;

    while (1) {
        /* Read memory */
        DPRINTF("Transmit packet at %08x\n",
                (s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_CTDA]);
        size = sizeof(uint16_t) * 6 * width;
        s->regs[SONIC_TTDA] = s->regs[SONIC_CTDA];
        address_space_rw(&s->as,
            ((s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA]) + sizeof(uint16_t) * width,
            MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 0);
        tx_len = 0;

        /* Update registers */
        s->regs[SONIC_TCR] = data[0 * width] & 0xf000;
        s->regs[SONIC_TPS] = data[1 * width];
        s->regs[SONIC_TFC] = data[2 * width];
        s->regs[SONIC_TSA0] = data[3 * width];
        s->regs[SONIC_TSA1] = data[4 * width];
        s->regs[SONIC_TFS] = data[5 * width];

        /* Handle programmable interrupt */
        if (s->regs[SONIC_TCR] & SONIC_TCR_PINT) {
            s->regs[SONIC_ISR] |= SONIC_ISR_PINT;
        } else {
            s->regs[SONIC_ISR] &= ~SONIC_ISR_PINT;
        }

        for (i = 0; i < s->regs[SONIC_TFC]; ) {
            /* Append fragment */
            len = s->regs[SONIC_TFS];
            if (tx_len + len > sizeof(s->tx_buffer)) {
                len = sizeof(s->tx_buffer) - tx_len;
            }
            address_space_rw(&s->as,
                (s->regs[SONIC_TSA1] << 16) | s->regs[SONIC_TSA0],
                MEMTXATTRS_UNSPECIFIED, &s->tx_buffer[tx_len], len, 0);
            tx_len += len;

            i++;
            if (i != s->regs[SONIC_TFC]) {
                /* Read next fragment details */
                size = sizeof(uint16_t) * 3 * width;
                address_space_rw(&s->as,
                    ((s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA]) + sizeof(uint16_t) * (4 + 3 * i) * width,
                    MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 0);
                s->regs[SONIC_TSA0] = data[0 * width];
                s->regs[SONIC_TSA1] = data[1 * width];
                s->regs[SONIC_TFS] = data[2 * width];
            }
        }

        /* Handle Ethernet checksum */
        if (!(s->regs[SONIC_TCR] & SONIC_TCR_CRCI)) {
            /* Don't append FCS there, to look like slirp packets
             * which don't have one */
        } else {
            /* Remove existing FCS */
            tx_len -= 4;
        }

        if (s->regs[SONIC_RCR] & (SONIC_RCR_LB1 | SONIC_RCR_LB0)) {
            /* Loopback */
            s->regs[SONIC_TCR] |= SONIC_TCR_CRSL;
            if (nc->info->can_receive(nc)) {
                s->loopback_packet = 1;
                nc->info->receive(nc, s->tx_buffer, tx_len);
            }
        } else {
            /* Transmit packet */
            qemu_send_packet(nc, s->tx_buffer, tx_len);
        }
        s->regs[SONIC_TCR] |= SONIC_TCR_PTX;

        /* Write status */
        data[0 * width] = s->regs[SONIC_TCR] & 0x0fff; /* status */
        size = sizeof(uint16_t) * width;
        address_space_rw(&s->as,
            (s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA],
            MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 1);

        if (!(s->regs[SONIC_CR] & SONIC_CR_HTX)) {
            /* Read footer of packet */
            size = sizeof(uint16_t) * width;
            address_space_rw(&s->as,
                ((s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA]) + sizeof(uint16_t) * (4 + 3 * s->regs[SONIC_TFC]) * width,
                MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 0);
            s->regs[SONIC_CTDA] = data[0 * width] & ~0x1;
            if (data[0 * width] & 0x1) {
                /* EOL detected */
                break;
            }
        }
    }

    /* Done */
    s->regs[SONIC_CR] &= ~SONIC_CR_TXP;
    s->regs[SONIC_ISR] |= SONIC_ISR_TXDN;
    dp8393x_update_irq(s);
}

static void dp8393x_do_halt_transmission(dp8393xState *s)
{
    /* Nothing to do */
}

static void dp8393x_do_command(dp8393xState *s, uint16_t command)
{
    if ((s->regs[SONIC_CR] & SONIC_CR_RST) && !(command & SONIC_CR_RST)) {
        s->regs[SONIC_CR] &= ~SONIC_CR_RST;
        return;
    }

    s->regs[SONIC_CR] |= (command & SONIC_CR_MASK);

    if (command & SONIC_CR_HTX)
        dp8393x_do_halt_transmission(s);
    if (command & SONIC_CR_TXP)
        dp8393x_do_transmit_packets(s);
    if (command & SONIC_CR_RXDIS)
        dp8393x_do_receiver_disable(s);
    if (command & SONIC_CR_RXEN)
        dp8393x_do_receiver_enable(s);
    if (command & SONIC_CR_STP)
        dp8393x_do_stop_timer(s);
    if (command & SONIC_CR_ST)
        dp8393x_do_start_timer(s);
    if (command & SONIC_CR_RST)
        dp8393x_do_software_reset(s);
    if (command & SONIC_CR_RRRA)
        dp8393x_do_read_rra(s);
    if (command & SONIC_CR_LCAM)
        dp8393x_do_load_cam(s);
}

static uint64_t dp8393x_read(void *opaque, hwaddr addr, unsigned int size)
{
    dp8393xState *s = opaque;
    int reg = addr >> s->it_shift;
    uint16_t val = 0;

    switch (reg) {
        /* Update data before reading it */
        case SONIC_WT0:
        case SONIC_WT1:
            dp8393x_update_wt_regs(s);
            val = s->regs[reg];
            break;
        /* Accept read to some registers only when in reset mode */
        case SONIC_CAP2:
        case SONIC_CAP1:
        case SONIC_CAP0:
            if (s->regs[SONIC_CR] & SONIC_CR_RST) {
                val = s->cam[s->regs[SONIC_CEP] & 0xf][2* (SONIC_CAP0 - reg) + 1] << 8;
                val |= s->cam[s->regs[SONIC_CEP] & 0xf][2* (SONIC_CAP0 - reg)];
            }
            break;
        /* All other registers have no special contrainst */
        default:
            val = s->regs[reg];
    }

    DPRINTF("read 0x%04x from reg %s\n", val, reg_names[reg]);

    return val;
}

static void dp8393x_write(void *opaque, hwaddr addr, uint64_t data,
                          unsigned int size)
{
    dp8393xState *s = opaque;
    int reg = addr >> s->it_shift;

    DPRINTF("write 0x%04x to reg %s\n", (uint16_t)data, reg_names[reg]);

    switch (reg) {
        /* Command register */
        case SONIC_CR:
            dp8393x_do_command(s, data);
            break;
        /* Prevent write to read-only registers */
        case SONIC_CAP2:
        case SONIC_CAP1:
        case SONIC_CAP0:
        case SONIC_SR:
        case SONIC_MDT:
            DPRINTF("writing to reg %d invalid\n", reg);
            break;
        /* Accept write to some registers only when in reset mode */
        case SONIC_DCR:
            if (s->regs[SONIC_CR] & SONIC_CR_RST) {
                s->regs[reg] = data & 0xbfff;
            } else {
                DPRINTF("writing to DCR invalid\n");
            }
            break;
        case SONIC_DCR2:
            if (s->regs[SONIC_CR] & SONIC_CR_RST) {
                s->regs[reg] = data & 0xf017;
            } else {
                DPRINTF("writing to DCR2 invalid\n");
            }
            break;
        /* 12 lower bytes are Read Only */
        case SONIC_TCR:
            s->regs[reg] = data & 0xf000;
            break;
        /* 9 lower bytes are Read Only */
        case SONIC_RCR:
            s->regs[reg] = data & 0xffe0;
            break;
        /* Ignore most significant bit */
        case SONIC_IMR:
            s->regs[reg] = data & 0x7fff;
            dp8393x_update_irq(s);
            break;
        /* Clear bits by writing 1 to them */
        case SONIC_ISR:
            data &= s->regs[reg];
            s->regs[reg] &= ~data;
            if (data & SONIC_ISR_RBE) {
                dp8393x_do_read_rra(s);
            }
            dp8393x_update_irq(s);
            if (dp8393x_can_receive(s->nic->ncs)) {
                qemu_flush_queued_packets(qemu_get_queue(s->nic));
            }
            break;
        /* Ignore least significant bit */
        case SONIC_RSA:
        case SONIC_REA:
        case SONIC_RRP:
        case SONIC_RWP:
            s->regs[reg] = data & 0xfffe;
            break;
        /* Invert written value for some registers */
        case SONIC_CRCT:
        case SONIC_FAET:
        case SONIC_MPT:
            s->regs[reg] = data ^ 0xffff;
            break;
        /* All other registers have no special contrainst */
        default:
            s->regs[reg] = data;
    }

    if (reg == SONIC_WT0 || reg == SONIC_WT1) {
        dp8393x_set_next_tick(s);
    }
}

static const MemoryRegionOps dp8393x_ops = {
    .read = dp8393x_read,
    .write = dp8393x_write,
    .impl.min_access_size = 2,
    .impl.max_access_size = 2,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void dp8393x_watchdog(void *opaque)
{
    dp8393xState *s = opaque;

    if (s->regs[SONIC_CR] & SONIC_CR_STP) {
        return;
    }

    s->regs[SONIC_WT1] = 0xffff;
    s->regs[SONIC_WT0] = 0xffff;
    dp8393x_set_next_tick(s);

    /* Signal underflow */
    s->regs[SONIC_ISR] |= SONIC_ISR_TC;
    dp8393x_update_irq(s);
}

static int dp8393x_can_receive(NetClientState *nc)
{
    dp8393xState *s = qemu_get_nic_opaque(nc);

    if (!(s->regs[SONIC_CR] & SONIC_CR_RXEN))
        return 0;
    if (s->regs[SONIC_ISR] & SONIC_ISR_RBE)
        return 0;
    return 1;
}

static int dp8393x_receive_filter(dp8393xState *s, const uint8_t * buf,
                                  int size)
{
    static const uint8_t bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    int i;

    /* Check promiscuous mode */
    if ((s->regs[SONIC_RCR] & SONIC_RCR_PRO) && (buf[0] & 1) == 0) {
        return 0;
    }

    /* Check multicast packets */
    if ((s->regs[SONIC_RCR] & SONIC_RCR_AMC) && (buf[0] & 1) == 1) {
        return SONIC_RCR_MC;
    }

    /* Check broadcast */
    if ((s->regs[SONIC_RCR] & SONIC_RCR_BRD) && !memcmp(buf, bcast, sizeof(bcast))) {
        return SONIC_RCR_BC;
    }

    /* Check CAM */
    for (i = 0; i < 16; i++) {
        if (s->regs[SONIC_CE] & (1 << i)) {
             /* Entry enabled */
             if (!memcmp(buf, s->cam[i], sizeof(s->cam[i]))) {
                 return 0;
             }
        }
    }

    return -1;
}

static ssize_t dp8393x_receive(NetClientState *nc, const uint8_t * buf,
                               size_t size)
{
    dp8393xState *s = qemu_get_nic_opaque(nc);
    uint16_t data[10];
    int packet_type;
    uint32_t available, address;
    int width, rx_len = size;
    uint32_t checksum;

    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;

    s->regs[SONIC_RCR] &= ~(SONIC_RCR_PRX | SONIC_RCR_LBK | SONIC_RCR_FAER |
        SONIC_RCR_CRCR | SONIC_RCR_LPKT | SONIC_RCR_BC | SONIC_RCR_MC);

    packet_type = dp8393x_receive_filter(s, buf, size);
    if (packet_type < 0) {
        DPRINTF("packet not for netcard\n");
        return -1;
    }

    /* XXX: Check byte ordering */

    /* Check for EOL */
    if (s->regs[SONIC_LLFA] & 0x1) {
        /* Are we still in resource exhaustion? */
        size = sizeof(uint16_t) * 1 * width;
        address = ((s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA]) + sizeof(uint16_t) * 5 * width;
        address_space_rw(&s->as, address, MEMTXATTRS_UNSPECIFIED,
                         (uint8_t *)data, size, 0);
        if (data[0 * width] & 0x1) {
            /* Still EOL ; stop reception */
            return -1;
        } else {
            s->regs[SONIC_CRDA] = s->regs[SONIC_LLFA];
        }
    }

    /* Save current position */
    s->regs[SONIC_TRBA1] = s->regs[SONIC_CRBA1];
    s->regs[SONIC_TRBA0] = s->regs[SONIC_CRBA0];

    /* Calculate the ethernet checksum */
    checksum = cpu_to_le32(crc32(0, buf, rx_len));

    /* Put packet into RBA */
    DPRINTF("Receive packet at %08x\n", (s->regs[SONIC_CRBA1] << 16) | s->regs[SONIC_CRBA0]);
    address = (s->regs[SONIC_CRBA1] << 16) | s->regs[SONIC_CRBA0];
    address_space_rw(&s->as, address,
        MEMTXATTRS_UNSPECIFIED, (uint8_t *)buf, rx_len, 1);
    address += rx_len;
    address_space_rw(&s->as, address,
        MEMTXATTRS_UNSPECIFIED, (uint8_t *)&checksum, 4, 1);
    rx_len += 4;
    s->regs[SONIC_CRBA1] = address >> 16;
    s->regs[SONIC_CRBA0] = address & 0xffff;
    available = (s->regs[SONIC_RBWC1] << 16) | s->regs[SONIC_RBWC0];
    available -= rx_len / 2;
    s->regs[SONIC_RBWC1] = available >> 16;
    s->regs[SONIC_RBWC0] = available & 0xffff;

    /* Update status */
    if (((s->regs[SONIC_RBWC1] << 16) | s->regs[SONIC_RBWC0]) < s->regs[SONIC_EOBC]) {
        s->regs[SONIC_RCR] |= SONIC_RCR_LPKT;
    }
    s->regs[SONIC_RCR] |= packet_type;
    s->regs[SONIC_RCR] |= SONIC_RCR_PRX;
    if (s->loopback_packet) {
        s->regs[SONIC_RCR] |= SONIC_RCR_LBK;
        s->loopback_packet = 0;
    }

    /* Write status to memory */
    DPRINTF("Write status at %08x\n", (s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA]);
    data[0 * width] = s->regs[SONIC_RCR]; /* status */
    data[1 * width] = rx_len; /* byte count */
    data[2 * width] = s->regs[SONIC_TRBA0]; /* pkt_ptr0 */
    data[3 * width] = s->regs[SONIC_TRBA1]; /* pkt_ptr1 */
    data[4 * width] = s->regs[SONIC_RSC]; /* seq_no */
    size = sizeof(uint16_t) * 5 * width;
    address_space_rw(&s->as, (s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA],
        MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 1);

    /* Move to next descriptor */
    size = sizeof(uint16_t) * width;
    address_space_rw(&s->as,
        ((s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA]) + sizeof(uint16_t) * 5 * width,
        MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, size, 0);
    s->regs[SONIC_LLFA] = data[0 * width];
    if (s->regs[SONIC_LLFA] & 0x1) {
        /* EOL detected */
        s->regs[SONIC_ISR] |= SONIC_ISR_RDE;
    } else {
        data[0 * width] = 0; /* in_use */
        address_space_rw(&s->as,
            ((s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA]) + sizeof(uint16_t) * 6 * width,
            MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, sizeof(uint16_t), 1);
        s->regs[SONIC_CRDA] = s->regs[SONIC_LLFA];
        s->regs[SONIC_ISR] |= SONIC_ISR_PKTRX;
        s->regs[SONIC_RSC] = (s->regs[SONIC_RSC] & 0xff00) | (((s->regs[SONIC_RSC] & 0x00ff) + 1) & 0x00ff);

        if (s->regs[SONIC_RCR] & SONIC_RCR_LPKT) {
            /* Read next RRA */
            dp8393x_do_read_rra(s);
        }
    }

    /* Done */
    dp8393x_update_irq(s);

    return size;
}

static void dp8393x_reset(DeviceState *dev)
{
    dp8393xState *s = DP8393X(dev);
    timer_del(s->watchdog);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[SONIC_CR] = SONIC_CR_RST | SONIC_CR_STP | SONIC_CR_RXDIS;
    s->regs[SONIC_DCR] &= ~(SONIC_DCR_EXBUS | SONIC_DCR_LBR);
    s->regs[SONIC_RCR] &= ~(SONIC_RCR_LB0 | SONIC_RCR_LB1 | SONIC_RCR_BRD | SONIC_RCR_RNT);
    s->regs[SONIC_TCR] |= SONIC_TCR_NCRS | SONIC_TCR_PTX;
    s->regs[SONIC_TCR] &= ~SONIC_TCR_BCM;
    s->regs[SONIC_IMR] = 0;
    s->regs[SONIC_ISR] = 0;
    s->regs[SONIC_DCR2] = 0;
    s->regs[SONIC_EOBC] = 0x02F8;
    s->regs[SONIC_RSC] = 0;
    s->regs[SONIC_CE] = 0;
    s->regs[SONIC_RSC] = 0;

    /* Network cable is connected */
    s->regs[SONIC_RCR] |= SONIC_RCR_CRS;

    dp8393x_update_irq(s);
}

static NetClientInfo net_dp83932_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = dp8393x_can_receive,
    .receive = dp8393x_receive,
};

static void dp8393x_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    dp8393xState *s = DP8393X(obj);

    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_mmio(sbd, &s->prom);
    sysbus_init_irq(sbd, &s->irq);
}

static void dp8393x_realize(DeviceState *dev, Error **errp)
{
    dp8393xState *s = DP8393X(dev);
    int i, checksum;
    uint8_t *prom;
    Error *local_err = NULL;

    address_space_init(&s->as, s->dma_mr, "dp8393x");
    memory_region_init_io(&s->mmio, OBJECT(dev), &dp8393x_ops, s,
                          "dp8393x-regs", 0x40 << s->it_shift);

    s->nic = qemu_new_nic(&net_dp83932_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    s->watchdog = timer_new_ns(QEMU_CLOCK_VIRTUAL, dp8393x_watchdog, s);
    s->regs[SONIC_SR] = 0x0004; /* only revision recognized by Linux */

    memory_region_init_ram(&s->prom, OBJECT(dev),
                           "dp8393x-prom", SONIC_PROM_SIZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_set_readonly(&s->prom, true);
    prom = memory_region_get_ram_ptr(&s->prom);
    checksum = 0;
    for (i = 0; i < 6; i++) {
        prom[i] = s->conf.macaddr.a[i];
        checksum += prom[i];
        if (checksum > 0xff) {
            checksum = (checksum + 1) & 0xff;
        }
    }
    prom[7] = 0xff - checksum;
}

static const VMStateDescription vmstate_dp8393x = {
    .name = "dp8393x",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField []) {
        VMSTATE_BUFFER_UNSAFE(cam, dp8393xState, 0, 16 * 6),
        VMSTATE_UINT16_ARRAY(regs, dp8393xState, 0x40),
        VMSTATE_END_OF_LIST()
    }
};

static Property dp8393x_properties[] = {
    DEFINE_NIC_PROPERTIES(dp8393xState, conf),
    DEFINE_PROP_PTR("dma_mr", dp8393xState, dma_mr),
    DEFINE_PROP_UINT8("it_shift", dp8393xState, it_shift, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void dp8393x_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->realize = dp8393x_realize;
    dc->reset = dp8393x_reset;
    dc->vmsd = &vmstate_dp8393x;
    dc->props = dp8393x_properties;
    /* Reason: dma_mr property can't be set */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo dp8393x_info = {
    .name          = TYPE_DP8393X,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(dp8393xState),
    .instance_init = dp8393x_instance_init,
    .class_init    = dp8393x_class_init,
};

static void dp8393x_register_types(void)
{
    type_register_static(&dp8393x_info);
}

type_init(dp8393x_register_types)
