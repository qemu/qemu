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
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "hw.h"
#include "qemu-timer.h"
#include "net.h"
#include "mips.h"

//#define DEBUG_SONIC

/* Calculate CRCs properly on Rx packets */
#define SONIC_CALCULATE_RXCRC

#if defined(SONIC_CALCULATE_RXCRC)
/* For crc32 */
#include <zlib.h>
#endif

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

typedef struct dp8393xState {
    /* Hardware */
    int it_shift;
    qemu_irq irq;
#ifdef DEBUG_SONIC
    int irq_level;
#endif
    QEMUTimer *watchdog;
    int64_t wt_last_update;
    VLANClientState *vc;
    int mmio_index;

    /* Registers */
    uint8_t cam[16][6];
    uint16_t regs[0x40];

    /* Temporaries */
    uint8_t tx_buffer[0x10000];
    int loopback_packet;

    /* Memory access */
    void (*memory_rw)(void *opaque, target_phys_addr_t addr, uint8_t *buf, int len, int is_write);
    void* mem_opaque;
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

static void do_load_cam(dp8393xState *s)
{
    uint16_t data[8];
    int width, size;
    uint16_t index = 0;

    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;
    size = sizeof(uint16_t) * 4 * width;

    while (s->regs[SONIC_CDC] & 0x1f) {
        /* Fill current entry */
        s->memory_rw(s->mem_opaque,
            (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_CDP],
            (uint8_t *)data, size, 0);
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
    s->memory_rw(s->mem_opaque,
        (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_CDP],
        (uint8_t *)data, size, 0);
    s->regs[SONIC_CE] = data[0 * width];
    DPRINTF("load cam done. cam enable mask 0x%04x\n", s->regs[SONIC_CE]);

    /* Done */
    s->regs[SONIC_CR] &= ~SONIC_CR_LCAM;
    s->regs[SONIC_ISR] |= SONIC_ISR_LCD;
    dp8393x_update_irq(s);
}

static void do_read_rra(dp8393xState *s)
{
    uint16_t data[8];
    int width, size;

    /* Read memory */
    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;
    size = sizeof(uint16_t) * 4 * width;
    s->memory_rw(s->mem_opaque,
        (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_RRP],
        (uint8_t *)data, size, 0);

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

static void do_software_reset(dp8393xState *s)
{
    qemu_del_timer(s->watchdog);

    s->regs[SONIC_CR] &= ~(SONIC_CR_LCAM | SONIC_CR_RRRA | SONIC_CR_TXP | SONIC_CR_HTX);
    s->regs[SONIC_CR] |= SONIC_CR_RST | SONIC_CR_RXDIS;
}

static void set_next_tick(dp8393xState *s)
{
    uint32_t ticks;
    int64_t delay;

    if (s->regs[SONIC_CR] & SONIC_CR_STP) {
        qemu_del_timer(s->watchdog);
        return;
    }

    ticks = s->regs[SONIC_WT1] << 16 | s->regs[SONIC_WT0];
    s->wt_last_update = qemu_get_clock(vm_clock);
    delay = ticks_per_sec * ticks / 5000000;
    qemu_mod_timer(s->watchdog, s->wt_last_update + delay);
}

static void update_wt_regs(dp8393xState *s)
{
    int64_t elapsed;
    uint32_t val;

    if (s->regs[SONIC_CR] & SONIC_CR_STP) {
        qemu_del_timer(s->watchdog);
        return;
    }

    elapsed = s->wt_last_update - qemu_get_clock(vm_clock);
    val = s->regs[SONIC_WT1] << 16 | s->regs[SONIC_WT0];
    val -= elapsed / 5000000;
    s->regs[SONIC_WT1] = (val >> 16) & 0xffff;
    s->regs[SONIC_WT0] = (val >> 0)  & 0xffff;
    set_next_tick(s);

}

static void do_start_timer(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_STP;
    set_next_tick(s);
}

static void do_stop_timer(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_ST;
    update_wt_regs(s);
}

static void do_receiver_enable(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_RXDIS;
}

static void do_receiver_disable(dp8393xState *s)
{
    s->regs[SONIC_CR] &= ~SONIC_CR_RXEN;
}

static void do_transmit_packets(dp8393xState *s)
{
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
        s->memory_rw(s->mem_opaque,
            ((s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA]) + sizeof(uint16_t) * width,
            (uint8_t *)data, size, 0);
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
            s->memory_rw(s->mem_opaque,
                (s->regs[SONIC_TSA1] << 16) | s->regs[SONIC_TSA0],
                &s->tx_buffer[tx_len], len, 0);
            tx_len += len;

            i++;
            if (i != s->regs[SONIC_TFC]) {
                /* Read next fragment details */
                size = sizeof(uint16_t) * 3 * width;
                s->memory_rw(s->mem_opaque,
                    ((s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA]) + sizeof(uint16_t) * (4 + 3 * i) * width,
                    (uint8_t *)data, size, 0);
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
            if (s->vc->fd_can_read(s)) {
                s->loopback_packet = 1;
                s->vc->fd_read(s, s->tx_buffer, tx_len);
            }
        } else {
            /* Transmit packet */
            qemu_send_packet(s->vc, s->tx_buffer, tx_len);
        }
        s->regs[SONIC_TCR] |= SONIC_TCR_PTX;

        /* Write status */
        data[0 * width] = s->regs[SONIC_TCR] & 0x0fff; /* status */
        size = sizeof(uint16_t) * width;
        s->memory_rw(s->mem_opaque,
            (s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA],
            (uint8_t *)data, size, 1);

        if (!(s->regs[SONIC_CR] & SONIC_CR_HTX)) {
            /* Read footer of packet */
            size = sizeof(uint16_t) * width;
            s->memory_rw(s->mem_opaque,
                ((s->regs[SONIC_UTDA] << 16) | s->regs[SONIC_TTDA]) + sizeof(uint16_t) * (4 + 3 * s->regs[SONIC_TFC]) * width,
                (uint8_t *)data, size, 0);
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

static void do_halt_transmission(dp8393xState *s)
{
    /* Nothing to do */
}

static void do_command(dp8393xState *s, uint16_t command)
{
    if ((s->regs[SONIC_CR] & SONIC_CR_RST) && !(command & SONIC_CR_RST)) {
        s->regs[SONIC_CR] &= ~SONIC_CR_RST;
        return;
    }

    s->regs[SONIC_CR] |= (command & SONIC_CR_MASK);

    if (command & SONIC_CR_HTX)
        do_halt_transmission(s);
    if (command & SONIC_CR_TXP)
        do_transmit_packets(s);
    if (command & SONIC_CR_RXDIS)
        do_receiver_disable(s);
    if (command & SONIC_CR_RXEN)
        do_receiver_enable(s);
    if (command & SONIC_CR_STP)
        do_stop_timer(s);
    if (command & SONIC_CR_ST)
        do_start_timer(s);
    if (command & SONIC_CR_RST)
        do_software_reset(s);
    if (command & SONIC_CR_RRRA)
        do_read_rra(s);
    if (command & SONIC_CR_LCAM)
        do_load_cam(s);
}

static uint16_t read_register(dp8393xState *s, int reg)
{
    uint16_t val = 0;

    switch (reg) {
        /* Update data before reading it */
        case SONIC_WT0:
        case SONIC_WT1:
            update_wt_regs(s);
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

static void write_register(dp8393xState *s, int reg, uint16_t val)
{
    DPRINTF("write 0x%04x to reg %s\n", val, reg_names[reg]);

    switch (reg) {
        /* Command register */
        case SONIC_CR:
            do_command(s, val);;
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
                s->regs[reg] = val & 0xbfff;
            } else {
                DPRINTF("writing to DCR invalid\n");
            }
            break;
        case SONIC_DCR2:
            if (s->regs[SONIC_CR] & SONIC_CR_RST) {
                s->regs[reg] = val & 0xf017;
            } else {
                DPRINTF("writing to DCR2 invalid\n");
            }
            break;
        /* 12 lower bytes are Read Only */
        case SONIC_TCR:
            s->regs[reg] = val & 0xf000;
            break;
        /* 9 lower bytes are Read Only */
        case SONIC_RCR:
            s->regs[reg] = val & 0xffe0;
            break;
        /* Ignore most significant bit */
        case SONIC_IMR:
            s->regs[reg] = val & 0x7fff;
            dp8393x_update_irq(s);
            break;
        /* Clear bits by writing 1 to them */
        case SONIC_ISR:
            val &= s->regs[reg];
            s->regs[reg] &= ~val;
            if (val & SONIC_ISR_RBE) {
                do_read_rra(s);
            }
            dp8393x_update_irq(s);
            break;
        /* Ignore least significant bit */
        case SONIC_RSA:
        case SONIC_REA:
        case SONIC_RRP:
        case SONIC_RWP:
            s->regs[reg] = val & 0xfffe;
            break;
        /* Invert written value for some registers */
        case SONIC_CRCT:
        case SONIC_FAET:
        case SONIC_MPT:
            s->regs[reg] = val ^ 0xffff;
            break;
        /* All other registers have no special contrainst */
        default:
            s->regs[reg] = val;
    }

    if (reg == SONIC_WT0 || reg == SONIC_WT1) {
        set_next_tick(s);
    }
}

static void dp8393x_watchdog(void *opaque)
{
    dp8393xState *s = opaque;

    if (s->regs[SONIC_CR] & SONIC_CR_STP) {
        return;
    }

    s->regs[SONIC_WT1] = 0xffff;
    s->regs[SONIC_WT0] = 0xffff;
    set_next_tick(s);

    /* Signal underflow */
    s->regs[SONIC_ISR] |= SONIC_ISR_TC;
    dp8393x_update_irq(s);
}

static uint32_t dp8393x_readw(void *opaque, target_phys_addr_t addr)
{
    dp8393xState *s = opaque;
    int reg;

    if ((addr & ((1 << s->it_shift) - 1)) != 0) {
        return 0;
    }

    reg = addr >> s->it_shift;
    return read_register(s, reg);
}

static uint32_t dp8393x_readb(void *opaque, target_phys_addr_t addr)
{
    uint16_t v = dp8393x_readw(opaque, addr & ~0x1);
    return (v >> (8 * (addr & 0x1))) & 0xff;
}

static uint32_t dp8393x_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = dp8393x_readw(opaque, addr);
    v |= dp8393x_readw(opaque, addr + 2) << 16;
    return v;
}

static void dp8393x_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    dp8393xState *s = opaque;
    int reg;

    if ((addr & ((1 << s->it_shift) - 1)) != 0) {
        return;
    }

    reg = addr >> s->it_shift;

    write_register(s, reg, (uint16_t)val);
}

static void dp8393x_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    uint16_t old_val = dp8393x_readw(opaque, addr & ~0x1);

    switch (addr & 3) {
    case 0:
        val = val | (old_val & 0xff00);
        break;
    case 1:
        val = (val << 8) | (old_val & 0x00ff);
        break;
    }
    dp8393x_writew(opaque, addr & ~0x1, val);
}

static void dp8393x_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    dp8393x_writew(opaque, addr, val & 0xffff);
    dp8393x_writew(opaque, addr + 2, (val >> 16) & 0xffff);
}

static CPUReadMemoryFunc *dp8393x_read[3] = {
    dp8393x_readb,
    dp8393x_readw,
    dp8393x_readl,
};

static CPUWriteMemoryFunc *dp8393x_write[3] = {
    dp8393x_writeb,
    dp8393x_writew,
    dp8393x_writel,
};

static int nic_can_receive(void *opaque)
{
    dp8393xState *s = opaque;

    if (!(s->regs[SONIC_CR] & SONIC_CR_RXEN))
        return 0;
    if (s->regs[SONIC_ISR] & SONIC_ISR_RBE)
        return 0;
    return 1;
}

static int receive_filter(dp8393xState *s, const uint8_t * buf, int size)
{
    static const uint8_t bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    int i;

    /* Check for runt packet (remember that checksum is not there) */
    if (size < 64 - 4) {
        return (s->regs[SONIC_RCR] & SONIC_RCR_RNT) ? 0 : -1;
    }

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

static void nic_receive(void *opaque, const uint8_t * buf, int size)
{
    uint16_t data[10];
    dp8393xState *s = opaque;
    int packet_type;
    uint32_t available, address;
    int width, rx_len = size;
    uint32_t checksum;

    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;

    s->regs[SONIC_RCR] &= ~(SONIC_RCR_PRX | SONIC_RCR_LBK | SONIC_RCR_FAER |
        SONIC_RCR_CRCR | SONIC_RCR_LPKT | SONIC_RCR_BC | SONIC_RCR_MC);

    packet_type = receive_filter(s, buf, size);
    if (packet_type < 0) {
        DPRINTF("packet not for netcard\n");
        return;
    }

    /* XXX: Check byte ordering */

    /* Check for EOL */
    if (s->regs[SONIC_LLFA] & 0x1) {
        /* Are we still in resource exhaustion? */
        size = sizeof(uint16_t) * 1 * width;
        address = ((s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA]) + sizeof(uint16_t) * 5 * width;
        s->memory_rw(s->mem_opaque, address, (uint8_t*)data, size, 0);
        if (data[0 * width] & 0x1) {
            /* Still EOL ; stop reception */
            return;
        } else {
            s->regs[SONIC_CRDA] = s->regs[SONIC_LLFA];
        }
    }

    /* Save current position */
    s->regs[SONIC_TRBA1] = s->regs[SONIC_CRBA1];
    s->regs[SONIC_TRBA0] = s->regs[SONIC_CRBA0];

    /* Calculate the ethernet checksum */
#ifdef SONIC_CALCULATE_RXCRC
    checksum = cpu_to_le32(crc32(0, buf, rx_len));
#else
    checksum = 0;
#endif

    /* Put packet into RBA */
    DPRINTF("Receive packet at %08x\n", (s->regs[SONIC_CRBA1] << 16) | s->regs[SONIC_CRBA0]);
    address = (s->regs[SONIC_CRBA1] << 16) | s->regs[SONIC_CRBA0];
    s->memory_rw(s->mem_opaque, address, (uint8_t*)buf, rx_len, 1);
    address += rx_len;
    s->memory_rw(s->mem_opaque, address, (uint8_t*)&checksum, 4, 1);
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
    s->memory_rw(s->mem_opaque, (s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA], (uint8_t *)data, size, 1);

    /* Move to next descriptor */
    size = sizeof(uint16_t) * width;
    s->memory_rw(s->mem_opaque,
        ((s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA]) + sizeof(uint16_t) * 5 * width,
        (uint8_t *)data, size, 0);
    s->regs[SONIC_LLFA] = data[0 * width];
    if (s->regs[SONIC_LLFA] & 0x1) {
        /* EOL detected */
        s->regs[SONIC_ISR] |= SONIC_ISR_RDE;
    } else {
        data[0 * width] = 0; /* in_use */
        s->memory_rw(s->mem_opaque,
            ((s->regs[SONIC_URDA] << 16) | s->regs[SONIC_CRDA]) + sizeof(uint16_t) * 6 * width,
            (uint8_t *)data, size, 1);
        s->regs[SONIC_CRDA] = s->regs[SONIC_LLFA];
        s->regs[SONIC_ISR] |= SONIC_ISR_PKTRX;
        s->regs[SONIC_RSC] = (s->regs[SONIC_RSC] & 0xff00) | (((s->regs[SONIC_RSC] & 0x00ff) + 1) & 0x00ff);

        if (s->regs[SONIC_RCR] & SONIC_RCR_LPKT) {
            /* Read next RRA */
            do_read_rra(s);
        }
    }

    /* Done */
    dp8393x_update_irq(s);
}

static void nic_reset(void *opaque)
{
    dp8393xState *s = opaque;
    qemu_del_timer(s->watchdog);

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

static void nic_cleanup(VLANClientState *vc)
{
    dp8393xState *s = vc->opaque;

    cpu_unregister_io_memory(s->mmio_index);

    qemu_del_timer(s->watchdog);
    qemu_free_timer(s->watchdog);

    qemu_free(s);
}

void dp83932_init(NICInfo *nd, target_phys_addr_t base, int it_shift,
                  qemu_irq irq, void* mem_opaque,
                  void (*memory_rw)(void *opaque, target_phys_addr_t addr, uint8_t *buf, int len, int is_write))
{
    dp8393xState *s;

    qemu_check_nic_model(nd, "dp83932");

    s = qemu_mallocz(sizeof(dp8393xState));

    s->mem_opaque = mem_opaque;
    s->memory_rw = memory_rw;
    s->it_shift = it_shift;
    s->irq = irq;
    s->watchdog = qemu_new_timer(vm_clock, dp8393x_watchdog, s);
    s->regs[SONIC_SR] = 0x0004; /* only revision recognized by Linux */

    s->vc = qemu_new_vlan_client(nd->vlan, nd->model, nd->name,
                                 nic_receive, nic_can_receive, nic_cleanup, s);

    qemu_format_nic_info_str(s->vc, nd->macaddr);
    qemu_register_reset(nic_reset, s);
    nic_reset(s);

    s->mmio_index = cpu_register_io_memory(0, dp8393x_read, dp8393x_write, s);
    cpu_register_physical_memory(base, 0x40 << it_shift, s->mmio_index);
}
