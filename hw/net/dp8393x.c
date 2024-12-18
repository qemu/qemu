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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/net/dp8393x.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include <zlib.h> /* for crc32 */
#include "qom/object.h"
#include "trace.h"

static const char *reg_names[] = {
    "CR", "DCR", "RCR", "TCR", "IMR", "ISR", "UTDA", "CTDA",
    "TPS", "TFC", "TSA0", "TSA1", "TFS", "URDA", "CRDA", "CRBA0",
    "CRBA1", "RBWC0", "RBWC1", "EOBC", "URRA", "RSA", "REA", "RRP",
    "RWP", "TRBA0", "TRBA1", "0x1b", "0x1c", "0x1d", "0x1e", "LLFA",
    "TTDA", "CEP", "CAP2", "CAP1", "CAP0", "CE", "CDP", "CDC",
    "SR", "WT0", "WT1", "RSC", "CRCT", "FAET", "MPT", "MDT",
    "0x30", "0x31", "0x32", "0x33", "0x34", "0x35", "0x36", "0x37",
    "0x38", "0x39", "0x3a", "0x3b", "0x3c", "0x3d", "0x3e", "DCR2" };

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

#define SONIC_ISR_RBAE   0x0010
#define SONIC_ISR_RBE    0x0020
#define SONIC_ISR_RDE    0x0040
#define SONIC_ISR_TC     0x0080
#define SONIC_ISR_TXDN   0x0200
#define SONIC_ISR_PKTRX  0x0400
#define SONIC_ISR_PINT   0x0800
#define SONIC_ISR_LCD    0x1000

#define SONIC_DESC_EOL   0x0001
#define SONIC_DESC_ADDR  0xFFFE


/*
 * Accessor functions for values which are formed by
 * concatenating two 16 bit device registers. By putting these
 * in their own functions with a uint32_t return type we avoid the
 * pitfall of implicit sign extension where ((x << 16) | y) is a
 * signed 32 bit integer that might get sign-extended to a 64 bit integer.
 */
static uint32_t dp8393x_cdp(dp8393xState *s)
{
    return (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_CDP];
}

static uint32_t dp8393x_crba(dp8393xState *s)
{
    return (s->regs[SONIC_CRBA1] << 16) | s->regs[SONIC_CRBA0];
}

static uint32_t dp8393x_crda(dp8393xState *s)
{
    return (s->regs[SONIC_URDA] << 16) |
           (s->regs[SONIC_CRDA] & SONIC_DESC_ADDR);
}

static uint32_t dp8393x_rbwc(dp8393xState *s)
{
    return (s->regs[SONIC_RBWC1] << 16) | s->regs[SONIC_RBWC0];
}

static uint32_t dp8393x_rrp(dp8393xState *s)
{
    return (s->regs[SONIC_URRA] << 16) | s->regs[SONIC_RRP];
}

static uint32_t dp8393x_tsa(dp8393xState *s)
{
    return (s->regs[SONIC_TSA1] << 16) | s->regs[SONIC_TSA0];
}

static uint32_t dp8393x_ttda(dp8393xState *s)
{
    return (s->regs[SONIC_UTDA] << 16) |
           (s->regs[SONIC_TTDA] & SONIC_DESC_ADDR);
}

static uint32_t dp8393x_wt(dp8393xState *s)
{
    return s->regs[SONIC_WT1] << 16 | s->regs[SONIC_WT0];
}

static uint16_t dp8393x_get(dp8393xState *s, hwaddr addr, int offset)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    uint16_t val;

    if (s->regs[SONIC_DCR] & SONIC_DCR_DW) {
        addr += offset << 2;
        if (s->big_endian) {
            val = address_space_ldl_be(&s->as, addr, attrs, NULL);
        } else {
            val = address_space_ldl_le(&s->as, addr, attrs, NULL);
        }
    } else {
        addr += offset << 1;
        if (s->big_endian) {
            val = address_space_lduw_be(&s->as, addr, attrs, NULL);
        } else {
            val = address_space_lduw_le(&s->as, addr, attrs, NULL);
        }
    }

    return val;
}

static void dp8393x_put(dp8393xState *s,
                        hwaddr addr, int offset, uint16_t val)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    if (s->regs[SONIC_DCR] & SONIC_DCR_DW) {
        addr += offset << 2;
        if (s->big_endian) {
            address_space_stl_be(&s->as, addr, val, attrs, NULL);
        } else {
            address_space_stl_le(&s->as, addr, val, attrs, NULL);
        }
    } else {
        addr += offset << 1;
        if (s->big_endian) {
            address_space_stw_be(&s->as, addr, val, attrs, NULL);
        } else {
            address_space_stw_le(&s->as, addr, val, attrs, NULL);
        }
    }
}

static void dp8393x_update_irq(dp8393xState *s)
{
    int level = (s->regs[SONIC_IMR] & s->regs[SONIC_ISR]) ? 1 : 0;

    if (level != s->irq_level) {
        s->irq_level = level;
        if (level) {
            trace_dp8393x_raise_irq(s->regs[SONIC_ISR]);
        } else {
            trace_dp8393x_lower_irq();
        }
    }

    qemu_set_irq(s->irq, level);
}

static void dp8393x_do_load_cam(dp8393xState *s)
{
    int width, size;
    uint16_t index;

    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;
    size = sizeof(uint16_t) * 4 * width;

    while (s->regs[SONIC_CDC] & 0x1f) {
        /* Fill current entry */
        index = dp8393x_get(s, dp8393x_cdp(s), 0) & 0xf;
        s->cam[index][0] = dp8393x_get(s, dp8393x_cdp(s), 1);
        s->cam[index][1] = dp8393x_get(s, dp8393x_cdp(s), 2);
        s->cam[index][2] = dp8393x_get(s, dp8393x_cdp(s), 3);
        trace_dp8393x_load_cam(index,
                               s->cam[index][0] >> 8, s->cam[index][0] & 0xff,
                               s->cam[index][1] >> 8, s->cam[index][1] & 0xff,
                               s->cam[index][2] >> 8, s->cam[index][2] & 0xff);
        /* Move to next entry */
        s->regs[SONIC_CDC]--;
        s->regs[SONIC_CDP] += size;
    }

    /* Read CAM enable */
    s->regs[SONIC_CE] = dp8393x_get(s, dp8393x_cdp(s), 0);
    trace_dp8393x_load_cam_done(s->regs[SONIC_CE]);

    /* Done */
    s->regs[SONIC_CR] &= ~SONIC_CR_LCAM;
    s->regs[SONIC_ISR] |= SONIC_ISR_LCD;
    dp8393x_update_irq(s);
}

static void dp8393x_do_read_rra(dp8393xState *s)
{
    int width, size;

    /* Read memory */
    width = (s->regs[SONIC_DCR] & SONIC_DCR_DW) ? 2 : 1;
    size = sizeof(uint16_t) * 4 * width;

    /* Update SONIC registers */
    s->regs[SONIC_CRBA0] = dp8393x_get(s, dp8393x_rrp(s), 0);
    s->regs[SONIC_CRBA1] = dp8393x_get(s, dp8393x_rrp(s), 1);
    s->regs[SONIC_RBWC0] = dp8393x_get(s, dp8393x_rrp(s), 2);
    s->regs[SONIC_RBWC1] = dp8393x_get(s, dp8393x_rrp(s), 3);
    trace_dp8393x_read_rra_regs(s->regs[SONIC_CRBA0], s->regs[SONIC_CRBA1],
                                s->regs[SONIC_RBWC0], s->regs[SONIC_RBWC1]);

    /* Go to next entry */
    s->regs[SONIC_RRP] += size;

    /* Handle wrap */
    if (s->regs[SONIC_RRP] == s->regs[SONIC_REA]) {
        s->regs[SONIC_RRP] = s->regs[SONIC_RSA];
    }

    /* Warn the host if CRBA now has the last available resource */
    if (s->regs[SONIC_RRP] == s->regs[SONIC_RWP]) {
        s->regs[SONIC_ISR] |= SONIC_ISR_RBE;
        dp8393x_update_irq(s);
    }

    /* Allow packet reception */
    s->last_rba_is_full = false;
}

static void dp8393x_do_software_reset(dp8393xState *s)
{
    timer_del(s->watchdog);

    s->regs[SONIC_CR] &= ~(SONIC_CR_LCAM | SONIC_CR_RRRA | SONIC_CR_TXP |
                           SONIC_CR_HTX);
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

    ticks = dp8393x_wt(s);
    s->wt_last_update = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    delay = NANOSECONDS_PER_SECOND * ticks / 5000000;
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
    val = dp8393x_wt(s);
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

static bool dp8393x_can_receive(NetClientState *nc);

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
    int tx_len, len;
    uint16_t i;

    while (1) {
        /* Read memory */
        s->regs[SONIC_TTDA] = s->regs[SONIC_CTDA];
        trace_dp8393x_transmit_packet(dp8393x_ttda(s));
        tx_len = 0;

        /* Update registers */
        s->regs[SONIC_TCR] = dp8393x_get(s, dp8393x_ttda(s), 1) & 0xf000;
        s->regs[SONIC_TPS] = dp8393x_get(s, dp8393x_ttda(s), 2);
        s->regs[SONIC_TFC] = dp8393x_get(s, dp8393x_ttda(s), 3);
        s->regs[SONIC_TSA0] = dp8393x_get(s, dp8393x_ttda(s), 4);
        s->regs[SONIC_TSA1] = dp8393x_get(s, dp8393x_ttda(s), 5);
        s->regs[SONIC_TFS] = dp8393x_get(s, dp8393x_ttda(s), 6);

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
            address_space_read(&s->as, dp8393x_tsa(s), MEMTXATTRS_UNSPECIFIED,
                               &s->tx_buffer[tx_len], len);
            tx_len += len;

            i++;
            if (i != s->regs[SONIC_TFC]) {
                /* Read next fragment details */
                s->regs[SONIC_TSA0] = dp8393x_get(s, dp8393x_ttda(s),
                                                  4 + 3 * i);
                s->regs[SONIC_TSA1] = dp8393x_get(s, dp8393x_ttda(s),
                                                  5 + 3 * i);
                s->regs[SONIC_TFS] = dp8393x_get(s, dp8393x_ttda(s),
                                                 6 + 3 * i);
            }
        }

        /* Handle Ethernet checksum */
        if (!(s->regs[SONIC_TCR] & SONIC_TCR_CRCI)) {
            /*
             * Don't append FCS there, to look like slirp packets
             * which don't have one
             */
        } else {
            /* Remove existing FCS */
            tx_len -= 4;
            if (tx_len < 0) {
                trace_dp8393x_transmit_txlen_error(tx_len);
                break;
            }
        }

        if (s->regs[SONIC_RCR] & (SONIC_RCR_LB1 | SONIC_RCR_LB0)) {
            /* Loopback */
            s->regs[SONIC_TCR] |= SONIC_TCR_CRSL;
            if (nc->info->can_receive(nc)) {
                s->loopback_packet = 1;
                qemu_receive_packet(nc, s->tx_buffer, tx_len);
            }
        } else {
            /* Transmit packet */
            qemu_send_packet(nc, s->tx_buffer, tx_len);
        }
        s->regs[SONIC_TCR] |= SONIC_TCR_PTX;

        /* Write status */
        dp8393x_put(s, dp8393x_ttda(s), 0, s->regs[SONIC_TCR] & 0x0fff);

        if (!(s->regs[SONIC_CR] & SONIC_CR_HTX)) {
            /* Read footer of packet */
            s->regs[SONIC_CTDA] = dp8393x_get(s, dp8393x_ttda(s),
                                              4 + 3 * s->regs[SONIC_TFC]);
            if (s->regs[SONIC_CTDA] & SONIC_DESC_EOL) {
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

    if (command & SONIC_CR_HTX) {
        dp8393x_do_halt_transmission(s);
    }
    if (command & SONIC_CR_TXP) {
        dp8393x_do_transmit_packets(s);
    }
    if (command & SONIC_CR_RXDIS) {
        dp8393x_do_receiver_disable(s);
    }
    if (command & SONIC_CR_RXEN) {
        dp8393x_do_receiver_enable(s);
    }
    if (command & SONIC_CR_STP) {
        dp8393x_do_stop_timer(s);
    }
    if (command & SONIC_CR_ST) {
        dp8393x_do_start_timer(s);
    }
    if (command & SONIC_CR_RST) {
        dp8393x_do_software_reset(s);
    }
    if (command & SONIC_CR_RRRA) {
        dp8393x_do_read_rra(s);
        s->regs[SONIC_CR] &= ~SONIC_CR_RRRA;
    }
    if (command & SONIC_CR_LCAM) {
        dp8393x_do_load_cam(s);
    }
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
            val = s->cam[s->regs[SONIC_CEP] & 0xf][SONIC_CAP0 - reg];
        }
        break;
    /* All other registers have no special constraints */
    default:
        val = s->regs[reg];
    }

    trace_dp8393x_read(reg, reg_names[reg], val, size);

    return val;
}

static void dp8393x_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned int size)
{
    dp8393xState *s = opaque;
    int reg = addr >> s->it_shift;

    trace_dp8393x_write(reg, reg_names[reg], val, size);

    switch (reg) {
    /* Command register */
    case SONIC_CR:
        dp8393x_do_command(s, val);
        break;
    /* Prevent write to read-only registers */
    case SONIC_CAP2:
    case SONIC_CAP1:
    case SONIC_CAP0:
    case SONIC_SR:
    case SONIC_MDT:
        trace_dp8393x_write_invalid(reg);
        break;
    /* Accept write to some registers only when in reset mode */
    case SONIC_DCR:
        if (s->regs[SONIC_CR] & SONIC_CR_RST) {
            s->regs[reg] = val & 0xbfff;
        } else {
            trace_dp8393x_write_invalid_dcr("DCR");
        }
        break;
    case SONIC_DCR2:
        if (s->regs[SONIC_CR] & SONIC_CR_RST) {
            s->regs[reg] = val & 0xf017;
        } else {
            trace_dp8393x_write_invalid_dcr("DCR2");
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
            dp8393x_do_read_rra(s);
        }
        dp8393x_update_irq(s);
        break;
    /* The guest is required to store aligned pointers here */
    case SONIC_RSA:
    case SONIC_REA:
    case SONIC_RRP:
    case SONIC_RWP:
        if (s->regs[SONIC_DCR] & SONIC_DCR_DW) {
            s->regs[reg] = val & 0xfffc;
        } else {
            s->regs[reg] = val & 0xfffe;
        }
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
        dp8393x_set_next_tick(s);
    }
}

/*
 * Since .impl.max_access_size is effectively controlled by the it_shift
 * property, leave it unspecified for now to allow the memory API to
 * correctly zero extend the 16-bit register values to the access size up to and
 * including it_shift.
 */
static const MemoryRegionOps dp8393x_ops = {
    .read = dp8393x_read,
    .write = dp8393x_write,
    .impl.min_access_size = 2,
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

static bool dp8393x_can_receive(NetClientState *nc)
{
    dp8393xState *s = qemu_get_nic_opaque(nc);

    return !!(s->regs[SONIC_CR] & SONIC_CR_RXEN);
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
    if ((s->regs[SONIC_RCR] & SONIC_RCR_BRD) &&
         !memcmp(buf, bcast, sizeof(bcast))) {
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
                               size_t pkt_size)
{
    dp8393xState *s = qemu_get_nic_opaque(nc);
    int packet_type;
    uint32_t available, address;
    int rx_len, padded_len;
    uint32_t checksum;
    int size;

    s->regs[SONIC_RCR] &= ~(SONIC_RCR_PRX | SONIC_RCR_LBK | SONIC_RCR_FAER |
        SONIC_RCR_CRCR | SONIC_RCR_LPKT | SONIC_RCR_BC | SONIC_RCR_MC);

    if (s->last_rba_is_full) {
        return pkt_size;
    }

    rx_len = pkt_size + sizeof(checksum);
    if (s->regs[SONIC_DCR] & SONIC_DCR_DW) {
        padded_len = ((rx_len - 1) | 3) + 1;
    } else {
        padded_len = ((rx_len - 1) | 1) + 1;
    }

    if (padded_len > dp8393x_rbwc(s) * 2) {
        trace_dp8393x_receive_oversize(pkt_size);
        s->regs[SONIC_ISR] |= SONIC_ISR_RBAE;
        dp8393x_update_irq(s);
        s->regs[SONIC_RCR] |= SONIC_RCR_LPKT;
        goto done;
    }

    packet_type = dp8393x_receive_filter(s, buf, pkt_size);
    if (packet_type < 0) {
        trace_dp8393x_receive_not_netcard();
        return -1;
    }

    /* Check for EOL */
    if (s->regs[SONIC_LLFA] & SONIC_DESC_EOL) {
        /* Are we still in resource exhaustion? */
        s->regs[SONIC_LLFA] = dp8393x_get(s, dp8393x_crda(s), 5);
        if (s->regs[SONIC_LLFA] & SONIC_DESC_EOL) {
            /* Still EOL ; stop reception */
            return -1;
        }
        /* Link has been updated by host */

        /* Clear in_use */
        dp8393x_put(s, dp8393x_crda(s), 6, 0x0000);

        /* Move to next descriptor */
        s->regs[SONIC_CRDA] = s->regs[SONIC_LLFA];
        s->regs[SONIC_ISR] |= SONIC_ISR_PKTRX;
    }

    /* Save current position */
    s->regs[SONIC_TRBA1] = s->regs[SONIC_CRBA1];
    s->regs[SONIC_TRBA0] = s->regs[SONIC_CRBA0];

    /* Calculate the ethernet checksum */
    checksum = cpu_to_le32(crc32(0, buf, pkt_size));

    /* Put packet into RBA */
    trace_dp8393x_receive_packet(dp8393x_crba(s));
    address = dp8393x_crba(s);
    address_space_write(&s->as, address, MEMTXATTRS_UNSPECIFIED,
                        buf, pkt_size);
    address += pkt_size;

    /* Put frame checksum into RBA */
    address_space_write(&s->as, address, MEMTXATTRS_UNSPECIFIED,
                        &checksum, sizeof(checksum));
    address += sizeof(checksum);

    /* Pad short packets to keep pointers aligned */
    if (rx_len < padded_len) {
        size = padded_len - rx_len;
        address_space_write(&s->as, address, MEMTXATTRS_UNSPECIFIED,
                            "\xFF\xFF\xFF", size);
        address += size;
    }

    s->regs[SONIC_CRBA1] = address >> 16;
    s->regs[SONIC_CRBA0] = address & 0xffff;
    available = dp8393x_rbwc(s);
    available -= padded_len >> 1;
    s->regs[SONIC_RBWC1] = available >> 16;
    s->regs[SONIC_RBWC0] = available & 0xffff;

    /* Update status */
    if (dp8393x_rbwc(s) < s->regs[SONIC_EOBC]) {
        s->regs[SONIC_RCR] |= SONIC_RCR_LPKT;
    }
    s->regs[SONIC_RCR] |= packet_type;
    s->regs[SONIC_RCR] |= SONIC_RCR_PRX;
    if (s->loopback_packet) {
        s->regs[SONIC_RCR] |= SONIC_RCR_LBK;
        s->loopback_packet = 0;
    }

    /* Write status to memory */
    trace_dp8393x_receive_write_status(dp8393x_crda(s));
    dp8393x_put(s, dp8393x_crda(s), 0, s->regs[SONIC_RCR]); /* status */
    dp8393x_put(s, dp8393x_crda(s), 1, rx_len); /* byte count */
    dp8393x_put(s, dp8393x_crda(s), 2, s->regs[SONIC_TRBA0]); /* pkt_ptr0 */
    dp8393x_put(s, dp8393x_crda(s), 3, s->regs[SONIC_TRBA1]); /* pkt_ptr1 */
    dp8393x_put(s, dp8393x_crda(s), 4, s->regs[SONIC_RSC]); /* seq_no */

    /* Check link field */
    s->regs[SONIC_LLFA] = dp8393x_get(s, dp8393x_crda(s), 5);
    if (s->regs[SONIC_LLFA] & SONIC_DESC_EOL) {
        /* EOL detected */
        s->regs[SONIC_ISR] |= SONIC_ISR_RDE;
    } else {
        /* Clear in_use */
        dp8393x_put(s, dp8393x_crda(s), 6, 0x0000);

        /* Move to next descriptor */
        s->regs[SONIC_CRDA] = s->regs[SONIC_LLFA];
        s->regs[SONIC_ISR] |= SONIC_ISR_PKTRX;
    }

    dp8393x_update_irq(s);

    s->regs[SONIC_RSC] = (s->regs[SONIC_RSC] & 0xff00) |
                         ((s->regs[SONIC_RSC] + 1) & 0x00ff);

done:

    if (s->regs[SONIC_RCR] & SONIC_RCR_LPKT) {
        if (s->regs[SONIC_RRP] == s->regs[SONIC_RWP]) {
            /* Stop packet reception */
            s->last_rba_is_full = true;
        } else {
            /* Read next resource */
            dp8393x_do_read_rra(s);
        }
    }

    return pkt_size;
}

static void dp8393x_reset(DeviceState *dev)
{
    dp8393xState *s = DP8393X(dev);
    timer_del(s->watchdog);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[SONIC_SR] = 0x0004; /* only revision recognized by Linux/mips */
    s->regs[SONIC_CR] = SONIC_CR_RST | SONIC_CR_STP | SONIC_CR_RXDIS;
    s->regs[SONIC_DCR] &= ~(SONIC_DCR_EXBUS | SONIC_DCR_LBR);
    s->regs[SONIC_RCR] &= ~(SONIC_RCR_LB0 | SONIC_RCR_LB1 | SONIC_RCR_BRD |
                            SONIC_RCR_RNT);
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
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = dp8393x_can_receive,
    .receive = dp8393x_receive,
};

static void dp8393x_instance_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    dp8393xState *s = DP8393X(obj);

    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void dp8393x_realize(DeviceState *dev, Error **errp)
{
    dp8393xState *s = DP8393X(dev);

    address_space_init(&s->as, s->dma_mr, "dp8393x");
    memory_region_init_io(&s->mmio, OBJECT(dev), &dp8393x_ops, s,
                          "dp8393x-regs", SONIC_REG_COUNT << s->it_shift);

    s->nic = qemu_new_nic(&net_dp83932_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    s->watchdog = timer_new_ns(QEMU_CLOCK_VIRTUAL, dp8393x_watchdog, s);
}

static const VMStateDescription vmstate_dp8393x = {
    .name = "dp8393x",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField []) {
        VMSTATE_UINT16_2DARRAY(cam, dp8393xState, 16, 3),
        VMSTATE_UINT16_ARRAY(regs, dp8393xState, SONIC_REG_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static const Property dp8393x_properties[] = {
    DEFINE_NIC_PROPERTIES(dp8393xState, conf),
    DEFINE_PROP_LINK("dma_mr", dp8393xState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_UINT8("it_shift", dp8393xState, it_shift, 0),
    DEFINE_PROP_BOOL("big_endian", dp8393xState, big_endian, false),
};

static void dp8393x_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->realize = dp8393x_realize;
    device_class_set_legacy_reset(dc, dp8393x_reset);
    dc->vmsd = &vmstate_dp8393x;
    device_class_set_props(dc, dp8393x_properties);
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
