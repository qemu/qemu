/*
 * i.MX Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2013 Jean-Christophe Dubois. <jcd@tribudubois.net>
 *
 * Based on Coldfire Fast Ethernet Controller emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/net/imx_fec.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "net/checksum.h"
#include "net/eth.h"
#include "trace.h"

/* For crc32 */
#include <zlib.h>

#define IMX_MAX_DESC    1024

static const char *imx_default_reg_name(IMXFECState *s, uint32_t index)
{
    static char tmp[20];
    sprintf(tmp, "index %d", index);
    return tmp;
}

static const char *imx_fec_reg_name(IMXFECState *s, uint32_t index)
{
    switch (index) {
    case ENET_FRBR:
        return "FRBR";
    case ENET_FRSR:
        return "FRSR";
    case ENET_MIIGSK_CFGR:
        return "MIIGSK_CFGR";
    case ENET_MIIGSK_ENR:
        return "MIIGSK_ENR";
    default:
        return imx_default_reg_name(s, index);
    }
}

static const char *imx_enet_reg_name(IMXFECState *s, uint32_t index)
{
    switch (index) {
    case ENET_RSFL:
        return "RSFL";
    case ENET_RSEM:
        return "RSEM";
    case ENET_RAEM:
        return "RAEM";
    case ENET_RAFL:
        return "RAFL";
    case ENET_TSEM:
        return "TSEM";
    case ENET_TAEM:
        return "TAEM";
    case ENET_TAFL:
        return "TAFL";
    case ENET_TIPG:
        return "TIPG";
    case ENET_FTRL:
        return "FTRL";
    case ENET_TACC:
        return "TACC";
    case ENET_RACC:
        return "RACC";
    case ENET_ATCR:
        return "ATCR";
    case ENET_ATVR:
        return "ATVR";
    case ENET_ATOFF:
        return "ATOFF";
    case ENET_ATPER:
        return "ATPER";
    case ENET_ATCOR:
        return "ATCOR";
    case ENET_ATINC:
        return "ATINC";
    case ENET_ATSTMP:
        return "ATSTMP";
    case ENET_TGSR:
        return "TGSR";
    case ENET_TCSR0:
        return "TCSR0";
    case ENET_TCCR0:
        return "TCCR0";
    case ENET_TCSR1:
        return "TCSR1";
    case ENET_TCCR1:
        return "TCCR1";
    case ENET_TCSR2:
        return "TCSR2";
    case ENET_TCCR2:
        return "TCCR2";
    case ENET_TCSR3:
        return "TCSR3";
    case ENET_TCCR3:
        return "TCCR3";
    default:
        return imx_default_reg_name(s, index);
    }
}

static const char *imx_eth_reg_name(IMXFECState *s, uint32_t index)
{
    switch (index) {
    case ENET_EIR:
        return "EIR";
    case ENET_EIMR:
        return "EIMR";
    case ENET_RDAR:
        return "RDAR";
    case ENET_TDAR:
        return "TDAR";
    case ENET_ECR:
        return "ECR";
    case ENET_MMFR:
        return "MMFR";
    case ENET_MSCR:
        return "MSCR";
    case ENET_MIBC:
        return "MIBC";
    case ENET_RCR:
        return "RCR";
    case ENET_TCR:
        return "TCR";
    case ENET_PALR:
        return "PALR";
    case ENET_PAUR:
        return "PAUR";
    case ENET_OPD:
        return "OPD";
    case ENET_IAUR:
        return "IAUR";
    case ENET_IALR:
        return "IALR";
    case ENET_GAUR:
        return "GAUR";
    case ENET_GALR:
        return "GALR";
    case ENET_TFWR:
        return "TFWR";
    case ENET_RDSR:
        return "RDSR";
    case ENET_TDSR:
        return "TDSR";
    case ENET_MRBR:
        return "MRBR";
    default:
        if (s->is_fec) {
            return imx_fec_reg_name(s, index);
        } else {
            return imx_enet_reg_name(s, index);
        }
    }
}

/*
 * Versions of this device with more than one TX descriptor save the
 * 2nd and 3rd descriptors in a subsection, to maintain migration
 * compatibility with previous versions of the device that only
 * supported a single descriptor.
 */
static bool imx_eth_is_multi_tx_ring(void *opaque)
{
    IMXFECState *s = IMX_FEC(opaque);

    return s->tx_ring_num > 1;
}

static const VMStateDescription vmstate_imx_eth_txdescs = {
    .name = "imx.fec/txdescs",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = imx_eth_is_multi_tx_ring,
    .fields = (VMStateField[]) {
         VMSTATE_UINT32(tx_descriptor[1], IMXFECState),
         VMSTATE_UINT32(tx_descriptor[2], IMXFECState),
         VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_imx_eth = {
    .name = TYPE_IMX_FEC,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IMXFECState, ENET_MAX),
        VMSTATE_UINT32(rx_descriptor, IMXFECState),
        VMSTATE_UINT32(tx_descriptor[0], IMXFECState),
        VMSTATE_UINT32(phy_status, IMXFECState),
        VMSTATE_UINT32(phy_control, IMXFECState),
        VMSTATE_UINT32(phy_advertise, IMXFECState),
        VMSTATE_UINT32(phy_int, IMXFECState),
        VMSTATE_UINT32(phy_int_mask, IMXFECState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_imx_eth_txdescs,
        NULL
    },
};

#define PHY_INT_ENERGYON            (1 << 7)
#define PHY_INT_AUTONEG_COMPLETE    (1 << 6)
#define PHY_INT_FAULT               (1 << 5)
#define PHY_INT_DOWN                (1 << 4)
#define PHY_INT_AUTONEG_LP          (1 << 3)
#define PHY_INT_PARFAULT            (1 << 2)
#define PHY_INT_AUTONEG_PAGE        (1 << 1)

static void imx_eth_update(IMXFECState *s);

/*
 * The MII phy could raise a GPIO to the processor which in turn
 * could be handled as an interrpt by the OS.
 * For now we don't handle any GPIO/interrupt line, so the OS will
 * have to poll for the PHY status.
 */
static void imx_phy_update_irq(IMXFECState *s)
{
    imx_eth_update(s);
}

static void imx_phy_update_link(IMXFECState *s)
{
    /* Autonegotiation status mirrors link status.  */
    if (qemu_get_queue(s->nic)->link_down) {
        trace_imx_phy_update_link("down");
        s->phy_status &= ~0x0024;
        s->phy_int |= PHY_INT_DOWN;
    } else {
        trace_imx_phy_update_link("up");
        s->phy_status |= 0x0024;
        s->phy_int |= PHY_INT_ENERGYON;
        s->phy_int |= PHY_INT_AUTONEG_COMPLETE;
    }
    imx_phy_update_irq(s);
}

static void imx_eth_set_link(NetClientState *nc)
{
    imx_phy_update_link(IMX_FEC(qemu_get_nic_opaque(nc)));
}

static void imx_phy_reset(IMXFECState *s)
{
    trace_imx_phy_reset();

    s->phy_status = 0x7809;
    s->phy_control = 0x3000;
    s->phy_advertise = 0x01e1;
    s->phy_int_mask = 0;
    s->phy_int = 0;
    imx_phy_update_link(s);
}

static uint32_t imx_phy_read(IMXFECState *s, int reg)
{
    uint32_t val;
    uint32_t phy = reg / 32;

    if (!s->phy_connected) {
        return 0xffff;
    }

    if (phy != s->phy_num) {
        if (s->phy_consumer && phy == s->phy_consumer->phy_num) {
            s = s->phy_consumer;
        } else {
            trace_imx_phy_read_num(phy, s->phy_num);
            return 0xffff;
        }
    }

    reg %= 32;

    switch (reg) {
    case 0:     /* Basic Control */
        val = s->phy_control;
        break;
    case 1:     /* Basic Status */
        val = s->phy_status;
        break;
    case 2:     /* ID1 */
        val = 0x0007;
        break;
    case 3:     /* ID2 */
        val = 0xc0d1;
        break;
    case 4:     /* Auto-neg advertisement */
        val = s->phy_advertise;
        break;
    case 5:     /* Auto-neg Link Partner Ability */
        val = 0x0f71;
        break;
    case 6:     /* Auto-neg Expansion */
        val = 1;
        break;
    case 29:    /* Interrupt source.  */
        val = s->phy_int;
        s->phy_int = 0;
        imx_phy_update_irq(s);
        break;
    case 30:    /* Interrupt mask */
        val = s->phy_int_mask;
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "[%s.phy]%s: reg %d not implemented\n",
                      TYPE_IMX_FEC, __func__, reg);
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s.phy]%s: Bad address at offset %d\n",
                      TYPE_IMX_FEC, __func__, reg);
        val = 0;
        break;
    }

    trace_imx_phy_read(val, phy, reg);

    return val;
}

static void imx_phy_write(IMXFECState *s, int reg, uint32_t val)
{
    uint32_t phy = reg / 32;

    if (!s->phy_connected) {
        return;
    }

    if (phy != s->phy_num) {
        if (s->phy_consumer && phy == s->phy_consumer->phy_num) {
            s = s->phy_consumer;
        } else {
            trace_imx_phy_write_num(phy, s->phy_num);
            return;
        }
    }

    reg %= 32;

    trace_imx_phy_write(val, phy, reg);

    switch (reg) {
    case 0:     /* Basic Control */
        if (val & 0x8000) {
            imx_phy_reset(s);
        } else {
            s->phy_control = val & 0x7980;
            /* Complete autonegotiation immediately.  */
            if (val & 0x1000) {
                s->phy_status |= 0x0020;
            }
        }
        break;
    case 4:     /* Auto-neg advertisement */
        s->phy_advertise = (val & 0x2d7f) | 0x80;
        break;
    case 30:    /* Interrupt mask */
        s->phy_int_mask = val & 0xff;
        imx_phy_update_irq(s);
        break;
    case 17:
    case 18:
    case 27:
    case 31:
        qemu_log_mask(LOG_UNIMP, "[%s.phy)%s: reg %d not implemented\n",
                      TYPE_IMX_FEC, __func__, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s.phy]%s: Bad address at offset %d\n",
                      TYPE_IMX_FEC, __func__, reg);
        break;
    }
}

static void imx_fec_read_bd(IMXFECBufDesc *bd, dma_addr_t addr)
{
    dma_memory_read(&address_space_memory, addr, bd, sizeof(*bd),
                    MEMTXATTRS_UNSPECIFIED);

    trace_imx_fec_read_bd(addr, bd->flags, bd->length, bd->data);
}

static void imx_fec_write_bd(IMXFECBufDesc *bd, dma_addr_t addr)
{
    dma_memory_write(&address_space_memory, addr, bd, sizeof(*bd),
                     MEMTXATTRS_UNSPECIFIED);
}

static void imx_enet_read_bd(IMXENETBufDesc *bd, dma_addr_t addr)
{
    dma_memory_read(&address_space_memory, addr, bd, sizeof(*bd),
                    MEMTXATTRS_UNSPECIFIED);

    trace_imx_enet_read_bd(addr, bd->flags, bd->length, bd->data,
                   bd->option, bd->status);
}

static void imx_enet_write_bd(IMXENETBufDesc *bd, dma_addr_t addr)
{
    dma_memory_write(&address_space_memory, addr, bd, sizeof(*bd),
                     MEMTXATTRS_UNSPECIFIED);
}

static void imx_eth_update(IMXFECState *s)
{
    /*
     * Previous versions of qemu had the ENET_INT_MAC and ENET_INT_TS_TIMER
     * interrupts swapped. This worked with older versions of Linux (4.14
     * and older) since Linux associated both interrupt lines with Ethernet
     * MAC interrupts. Specifically,
     * - Linux 4.15 and later have separate interrupt handlers for the MAC and
     *   timer interrupts. Those versions of Linux fail with versions of QEMU
     *   with swapped interrupt assignments.
     * - In linux 4.14, both interrupt lines were registered with the Ethernet
     *   MAC interrupt handler. As a result, all versions of qemu happen to
     *   work, though that is accidental.
     * - In Linux 4.9 and older, the timer interrupt was registered directly
     *   with the Ethernet MAC interrupt handler. The MAC interrupt was
     *   redirected to a GPIO interrupt to work around erratum ERR006687.
     *   This was implemented using the SOC's IOMUX block. In qemu, this GPIO
     *   interrupt never fired since IOMUX is currently not supported in qemu.
     *   Linux instead received MAC interrupts on the timer interrupt.
     *   As a result, qemu versions with the swapped interrupt assignment work,
     *   albeit accidentally, but qemu versions with the correct interrupt
     *   assignment fail.
     *
     * To ensure that all versions of Linux work, generate ENET_INT_MAC
     * interrupts on both interrupt lines. This should be changed if and when
     * qemu supports IOMUX.
     */
    if (s->regs[ENET_EIR] & s->regs[ENET_EIMR] &
        (ENET_INT_MAC | ENET_INT_TS_TIMER)) {
        qemu_set_irq(s->irq[1], 1);
    } else {
        qemu_set_irq(s->irq[1], 0);
    }

    if (s->regs[ENET_EIR] & s->regs[ENET_EIMR] & ENET_INT_MAC) {
        qemu_set_irq(s->irq[0], 1);
    } else {
        qemu_set_irq(s->irq[0], 0);
    }
}

static void imx_fec_do_tx(IMXFECState *s)
{
    int frame_size = 0, descnt = 0;
    uint8_t *ptr = s->frame;
    uint32_t addr = s->tx_descriptor[0];

    while (descnt++ < IMX_MAX_DESC) {
        IMXFECBufDesc bd;
        int len;

        imx_fec_read_bd(&bd, addr);
        if ((bd.flags & ENET_BD_R) == 0) {

            /* Run out of descriptors to transmit.  */
            trace_imx_eth_tx_bd_busy();

            break;
        }
        len = bd.length;
        if (frame_size + len > ENET_MAX_FRAME_SIZE) {
            len = ENET_MAX_FRAME_SIZE - frame_size;
            s->regs[ENET_EIR] |= ENET_INT_BABT;
        }
        dma_memory_read(&address_space_memory, bd.data, ptr, len,
                        MEMTXATTRS_UNSPECIFIED);
        ptr += len;
        frame_size += len;
        if (bd.flags & ENET_BD_L) {
            /* Last buffer in frame.  */
            qemu_send_packet(qemu_get_queue(s->nic), s->frame, frame_size);
            ptr = s->frame;
            frame_size = 0;
            s->regs[ENET_EIR] |= ENET_INT_TXF;
        }
        s->regs[ENET_EIR] |= ENET_INT_TXB;
        bd.flags &= ~ENET_BD_R;
        /* Write back the modified descriptor.  */
        imx_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & ENET_BD_W) != 0) {
            addr = s->regs[ENET_TDSR];
        } else {
            addr += sizeof(bd);
        }
    }

    s->tx_descriptor[0] = addr;

    imx_eth_update(s);
}

static void imx_enet_do_tx(IMXFECState *s, uint32_t index)
{
    int frame_size = 0, descnt = 0;

    uint8_t *ptr = s->frame;
    uint32_t addr, int_txb, int_txf, tdsr;
    size_t ring;

    switch (index) {
    case ENET_TDAR:
        ring    = 0;
        int_txb = ENET_INT_TXB;
        int_txf = ENET_INT_TXF;
        tdsr    = ENET_TDSR;
        break;
    case ENET_TDAR1:
        ring    = 1;
        int_txb = ENET_INT_TXB1;
        int_txf = ENET_INT_TXF1;
        tdsr    = ENET_TDSR1;
        break;
    case ENET_TDAR2:
        ring    = 2;
        int_txb = ENET_INT_TXB2;
        int_txf = ENET_INT_TXF2;
        tdsr    = ENET_TDSR2;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bogus value for index %x\n",
                      __func__, index);
        abort();
        break;
    }

    addr = s->tx_descriptor[ring];

    while (descnt++ < IMX_MAX_DESC) {
        IMXENETBufDesc bd;
        int len;

        imx_enet_read_bd(&bd, addr);
        if ((bd.flags & ENET_BD_R) == 0) {
            /* Run out of descriptors to transmit.  */

            trace_imx_eth_tx_bd_busy();

            break;
        }
        len = bd.length;
        if (frame_size + len > ENET_MAX_FRAME_SIZE) {
            len = ENET_MAX_FRAME_SIZE - frame_size;
            s->regs[ENET_EIR] |= ENET_INT_BABT;
        }
        dma_memory_read(&address_space_memory, bd.data, ptr, len,
                        MEMTXATTRS_UNSPECIFIED);
        ptr += len;
        frame_size += len;
        if (bd.flags & ENET_BD_L) {
            int csum = 0;

            if (bd.option & ENET_BD_PINS) {
                csum |= (CSUM_TCP | CSUM_UDP);
            }
            if (bd.option & ENET_BD_IINS) {
                csum |= CSUM_IP;
            }
            if (csum) {
                net_checksum_calculate(s->frame, frame_size, csum);
            }

            /* Last buffer in frame.  */

            qemu_send_packet(qemu_get_queue(s->nic), s->frame, frame_size);
            ptr = s->frame;

            frame_size = 0;
            if (bd.option & ENET_BD_TX_INT) {
                s->regs[ENET_EIR] |= int_txf;
            }
            /* Indicate that we've updated the last buffer descriptor. */
            bd.last_buffer = ENET_BD_BDU;
        }
        if (bd.option & ENET_BD_TX_INT) {
            s->regs[ENET_EIR] |= int_txb;
        }
        bd.flags &= ~ENET_BD_R;
        /* Write back the modified descriptor.  */
        imx_enet_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & ENET_BD_W) != 0) {
            addr = s->regs[tdsr];
        } else {
            addr += sizeof(bd);
        }
    }

    s->tx_descriptor[ring] = addr;

    imx_eth_update(s);
}

static void imx_eth_do_tx(IMXFECState *s, uint32_t index)
{
    if (!s->is_fec && (s->regs[ENET_ECR] & ENET_ECR_EN1588)) {
        imx_enet_do_tx(s, index);
    } else {
        imx_fec_do_tx(s);
    }
}

static void imx_eth_enable_rx(IMXFECState *s, bool flush)
{
    IMXFECBufDesc bd;

    imx_fec_read_bd(&bd, s->rx_descriptor);

    s->regs[ENET_RDAR] = (bd.flags & ENET_BD_E) ? ENET_RDAR_RDAR : 0;

    if (!s->regs[ENET_RDAR]) {
        trace_imx_eth_rx_bd_full();
    } else if (flush) {
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
    }
}

static void imx_eth_reset(DeviceState *d)
{
    IMXFECState *s = IMX_FEC(d);

    /* Reset the Device */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[ENET_ECR]   = 0xf0000000;
    s->regs[ENET_MIBC]  = 0xc0000000;
    s->regs[ENET_RCR]   = 0x05ee0001;
    s->regs[ENET_OPD]   = 0x00010000;

    s->regs[ENET_PALR]  = (s->conf.macaddr.a[0] << 24)
                          | (s->conf.macaddr.a[1] << 16)
                          | (s->conf.macaddr.a[2] << 8)
                          | s->conf.macaddr.a[3];
    s->regs[ENET_PAUR]  = (s->conf.macaddr.a[4] << 24)
                          | (s->conf.macaddr.a[5] << 16)
                          | 0x8808;

    if (s->is_fec) {
        s->regs[ENET_FRBR]  = 0x00000600;
        s->regs[ENET_FRSR]  = 0x00000500;
        s->regs[ENET_MIIGSK_ENR]  = 0x00000006;
    } else {
        s->regs[ENET_RAEM]  = 0x00000004;
        s->regs[ENET_RAFL]  = 0x00000004;
        s->regs[ENET_TAEM]  = 0x00000004;
        s->regs[ENET_TAFL]  = 0x00000008;
        s->regs[ENET_TIPG]  = 0x0000000c;
        s->regs[ENET_FTRL]  = 0x000007ff;
        s->regs[ENET_ATPER] = 0x3b9aca00;
    }

    s->rx_descriptor = 0;
    memset(s->tx_descriptor, 0, sizeof(s->tx_descriptor));

    /* We also reset the PHY */
    imx_phy_reset(s);
}

static uint32_t imx_default_read(IMXFECState *s, uint32_t index)
{
    qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                  PRIx32 "\n", TYPE_IMX_FEC, __func__, index * 4);
    return 0;
}

static uint32_t imx_fec_read(IMXFECState *s, uint32_t index)
{
    switch (index) {
    case ENET_FRBR:
    case ENET_FRSR:
    case ENET_MIIGSK_CFGR:
    case ENET_MIIGSK_ENR:
        return s->regs[index];
    default:
        return imx_default_read(s, index);
    }
}

static uint32_t imx_enet_read(IMXFECState *s, uint32_t index)
{
    switch (index) {
    case ENET_RSFL:
    case ENET_RSEM:
    case ENET_RAEM:
    case ENET_RAFL:
    case ENET_TSEM:
    case ENET_TAEM:
    case ENET_TAFL:
    case ENET_TIPG:
    case ENET_FTRL:
    case ENET_TACC:
    case ENET_RACC:
    case ENET_ATCR:
    case ENET_ATVR:
    case ENET_ATOFF:
    case ENET_ATPER:
    case ENET_ATCOR:
    case ENET_ATINC:
    case ENET_ATSTMP:
    case ENET_TGSR:
    case ENET_TCSR0:
    case ENET_TCCR0:
    case ENET_TCSR1:
    case ENET_TCCR1:
    case ENET_TCSR2:
    case ENET_TCCR2:
    case ENET_TCSR3:
    case ENET_TCCR3:
        return s->regs[index];
    default:
        return imx_default_read(s, index);
    }
}

static uint64_t imx_eth_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t value = 0;
    IMXFECState *s = IMX_FEC(opaque);
    uint32_t index = offset >> 2;

    switch (index) {
    case ENET_EIR:
    case ENET_EIMR:
    case ENET_RDAR:
    case ENET_TDAR:
    case ENET_ECR:
    case ENET_MMFR:
    case ENET_MSCR:
    case ENET_MIBC:
    case ENET_RCR:
    case ENET_TCR:
    case ENET_PALR:
    case ENET_PAUR:
    case ENET_OPD:
    case ENET_IAUR:
    case ENET_IALR:
    case ENET_GAUR:
    case ENET_GALR:
    case ENET_TFWR:
    case ENET_RDSR:
    case ENET_TDSR:
    case ENET_MRBR:
        value = s->regs[index];
        break;
    default:
        if (s->is_fec) {
            value = imx_fec_read(s, index);
        } else {
            value = imx_enet_read(s, index);
        }
        break;
    }

    trace_imx_eth_read(index, imx_eth_reg_name(s, index), value);

    return value;
}

static void imx_default_write(IMXFECState *s, uint32_t index, uint32_t value)
{
    qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad address at offset 0x%"
                  PRIx32 "\n", TYPE_IMX_FEC, __func__, index * 4);
    return;
}

static void imx_fec_write(IMXFECState *s, uint32_t index, uint32_t value)
{
    switch (index) {
    case ENET_FRBR:
        /* FRBR is read only */
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Register FRBR is read only\n",
                      TYPE_IMX_FEC, __func__);
        break;
    case ENET_FRSR:
        s->regs[index] = (value & 0x000003fc) | 0x00000400;
        break;
    case ENET_MIIGSK_CFGR:
        s->regs[index] = value & 0x00000053;
        break;
    case ENET_MIIGSK_ENR:
        s->regs[index] = (value & 0x00000002) ? 0x00000006 : 0;
        break;
    default:
        imx_default_write(s, index, value);
        break;
    }
}

static void imx_enet_write(IMXFECState *s, uint32_t index, uint32_t value)
{
    switch (index) {
    case ENET_RSFL:
    case ENET_RSEM:
    case ENET_RAEM:
    case ENET_RAFL:
    case ENET_TSEM:
    case ENET_TAEM:
    case ENET_TAFL:
        s->regs[index] = value & 0x000001ff;
        break;
    case ENET_TIPG:
        s->regs[index] = value & 0x0000001f;
        break;
    case ENET_FTRL:
        s->regs[index] = value & 0x00003fff;
        break;
    case ENET_TACC:
        s->regs[index] = value & 0x00000019;
        break;
    case ENET_RACC:
        s->regs[index] = value & 0x000000C7;
        break;
    case ENET_ATCR:
        s->regs[index] = value & 0x00002a9d;
        break;
    case ENET_ATVR:
    case ENET_ATOFF:
    case ENET_ATPER:
        s->regs[index] = value;
        break;
    case ENET_ATSTMP:
        /* ATSTMP is read only */
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Register ATSTMP is read only\n",
                      TYPE_IMX_FEC, __func__);
        break;
    case ENET_ATCOR:
        s->regs[index] = value & 0x7fffffff;
        break;
    case ENET_ATINC:
        s->regs[index] = value & 0x00007f7f;
        break;
    case ENET_TGSR:
        /* implement clear timer flag */
        s->regs[index] &= ~(value & 0x0000000f); /* all bits W1C */
        break;
    case ENET_TCSR0:
    case ENET_TCSR1:
    case ENET_TCSR2:
    case ENET_TCSR3:
        s->regs[index] &= ~(value & 0x00000080); /* W1C bits */
        s->regs[index] &= ~0x0000007d; /* writable fields */
        s->regs[index] |= (value & 0x0000007d);
        break;
    case ENET_TCCR0:
    case ENET_TCCR1:
    case ENET_TCCR2:
    case ENET_TCCR3:
        s->regs[index] = value;
        break;
    default:
        imx_default_write(s, index, value);
        break;
    }
}

static void imx_eth_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    IMXFECState *s = IMX_FEC(opaque);
    const bool single_tx_ring = !imx_eth_is_multi_tx_ring(s);
    uint32_t index = offset >> 2;

    trace_imx_eth_write(index, imx_eth_reg_name(s, index), value);

    switch (index) {
    case ENET_EIR:
        s->regs[index] &= ~value;
        break;
    case ENET_EIMR:
        s->regs[index] = value;
        break;
    case ENET_RDAR:
        if (s->regs[ENET_ECR] & ENET_ECR_ETHEREN) {
            if (!s->regs[index]) {
                imx_eth_enable_rx(s, true);
            }
        } else {
            s->regs[index] = 0;
        }
        break;
    case ENET_TDAR1:
    case ENET_TDAR2:
        if (unlikely(single_tx_ring)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "[%s]%s: trying to access TDAR2 or TDAR1\n",
                          TYPE_IMX_FEC, __func__);
            return;
        }
        /* fall through */
    case ENET_TDAR:
        if (s->regs[ENET_ECR] & ENET_ECR_ETHEREN) {
            s->regs[index] = ENET_TDAR_TDAR;
            imx_eth_do_tx(s, index);
        }
        s->regs[index] = 0;
        break;
    case ENET_ECR:
        if (value & ENET_ECR_RESET) {
            return imx_eth_reset(DEVICE(s));
        }
        s->regs[index] = value;
        if ((s->regs[index] & ENET_ECR_ETHEREN) == 0) {
            s->regs[ENET_RDAR] = 0;
            s->rx_descriptor = s->regs[ENET_RDSR];
            s->regs[ENET_TDAR]  = 0;
            s->regs[ENET_TDAR1] = 0;
            s->regs[ENET_TDAR2] = 0;
            s->tx_descriptor[0] = s->regs[ENET_TDSR];
            s->tx_descriptor[1] = s->regs[ENET_TDSR1];
            s->tx_descriptor[2] = s->regs[ENET_TDSR2];
        }
        break;
    case ENET_MMFR:
        s->regs[index] = value;
        if (extract32(value, 29, 1)) {
            /* This is a read operation */
            s->regs[ENET_MMFR] = deposit32(s->regs[ENET_MMFR], 0, 16,
                                           imx_phy_read(s,
                                                       extract32(value,
                                                                 18, 10)));
        } else {
            /* This is a write operation */
            imx_phy_write(s, extract32(value, 18, 10), extract32(value, 0, 16));
        }
        /* raise the interrupt as the PHY operation is done */
        s->regs[ENET_EIR] |= ENET_INT_MII;
        break;
    case ENET_MSCR:
        s->regs[index] = value & 0xfe;
        break;
    case ENET_MIBC:
        /* TODO: Implement MIB.  */
        s->regs[index] = (value & 0x80000000) ? 0xc0000000 : 0;
        break;
    case ENET_RCR:
        s->regs[index] = value & 0x07ff003f;
        /* TODO: Implement LOOP mode.  */
        break;
    case ENET_TCR:
        /* We transmit immediately, so raise GRA immediately.  */
        s->regs[index] = value;
        if (value & 1) {
            s->regs[ENET_EIR] |= ENET_INT_GRA;
        }
        break;
    case ENET_PALR:
        s->regs[index] = value;
        s->conf.macaddr.a[0] = value >> 24;
        s->conf.macaddr.a[1] = value >> 16;
        s->conf.macaddr.a[2] = value >> 8;
        s->conf.macaddr.a[3] = value;
        break;
    case ENET_PAUR:
        s->regs[index] = (value | 0x0000ffff) & 0xffff8808;
        s->conf.macaddr.a[4] = value >> 24;
        s->conf.macaddr.a[5] = value >> 16;
        break;
    case ENET_OPD:
        s->regs[index] = (value & 0x0000ffff) | 0x00010000;
        break;
    case ENET_IAUR:
    case ENET_IALR:
    case ENET_GAUR:
    case ENET_GALR:
        /* TODO: implement MAC hash filtering.  */
        break;
    case ENET_TFWR:
        if (s->is_fec) {
            s->regs[index] = value & 0x3;
        } else {
            s->regs[index] = value & 0x13f;
        }
        break;
    case ENET_RDSR:
        if (s->is_fec) {
            s->regs[index] = value & ~3;
        } else {
            s->regs[index] = value & ~7;
        }
        s->rx_descriptor = s->regs[index];
        break;
    case ENET_TDSR:
        if (s->is_fec) {
            s->regs[index] = value & ~3;
        } else {
            s->regs[index] = value & ~7;
        }
        s->tx_descriptor[0] = s->regs[index];
        break;
    case ENET_TDSR1:
        if (unlikely(single_tx_ring)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "[%s]%s: trying to access TDSR1\n",
                          TYPE_IMX_FEC, __func__);
            return;
        }

        s->regs[index] = value & ~7;
        s->tx_descriptor[1] = s->regs[index];
        break;
    case ENET_TDSR2:
        if (unlikely(single_tx_ring)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "[%s]%s: trying to access TDSR2\n",
                          TYPE_IMX_FEC, __func__);
            return;
        }

        s->regs[index] = value & ~7;
        s->tx_descriptor[2] = s->regs[index];
        break;
    case ENET_MRBR:
        s->regs[index] = value & 0x00003ff0;
        break;
    default:
        if (s->is_fec) {
            imx_fec_write(s, index, value);
        } else {
            imx_enet_write(s, index, value);
        }
        return;
    }

    imx_eth_update(s);
}

static bool imx_eth_can_receive(NetClientState *nc)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));

    return !!s->regs[ENET_RDAR];
}

static ssize_t imx_fec_receive(NetClientState *nc, const uint8_t *buf,
                               size_t len)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));
    IMXFECBufDesc bd;
    uint32_t flags = 0;
    uint32_t addr;
    uint32_t crc;
    uint32_t buf_addr;
    uint8_t *crc_ptr;
    unsigned int buf_len;
    size_t size = len;

    trace_imx_fec_receive(size);

    if (!s->regs[ENET_RDAR]) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Unexpected packet\n",
                      TYPE_IMX_FEC, __func__);
        return 0;
    }

    crc = cpu_to_be32(crc32(~0, buf, size));
    /* Increase size by 4, loop below reads the last 4 bytes from crc_ptr. */
    size += 4;
    crc_ptr = (uint8_t *) &crc;

    /* Huge frames are truncated.  */
    if (size > ENET_MAX_FRAME_SIZE) {
        size = ENET_MAX_FRAME_SIZE;
        flags |= ENET_BD_TR | ENET_BD_LG;
    }

    /* Frames larger than the user limit just set error flags.  */
    if (size > (s->regs[ENET_RCR] >> 16)) {
        flags |= ENET_BD_LG;
    }

    addr = s->rx_descriptor;
    while (size > 0) {
        imx_fec_read_bd(&bd, addr);
        if ((bd.flags & ENET_BD_E) == 0) {
            /* No descriptors available.  Bail out.  */
            /*
             * FIXME: This is wrong. We should probably either
             * save the remainder for when more RX buffers are
             * available, or flag an error.
             */
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Lost end of frame\n",
                          TYPE_IMX_FEC, __func__);
            break;
        }
        buf_len = (size <= s->regs[ENET_MRBR]) ? size : s->regs[ENET_MRBR];
        bd.length = buf_len;
        size -= buf_len;

        trace_imx_fec_receive_len(addr, bd.length);

        /* The last 4 bytes are the CRC.  */
        if (size < 4) {
            buf_len += size - 4;
        }
        buf_addr = bd.data;
        dma_memory_write(&address_space_memory, buf_addr, buf, buf_len,
                         MEMTXATTRS_UNSPECIFIED);
        buf += buf_len;
        if (size < 4) {
            dma_memory_write(&address_space_memory, buf_addr + buf_len,
                             crc_ptr, 4 - size, MEMTXATTRS_UNSPECIFIED);
            crc_ptr += 4 - size;
        }
        bd.flags &= ~ENET_BD_E;
        if (size == 0) {
            /* Last buffer in frame.  */
            bd.flags |= flags | ENET_BD_L;

            trace_imx_fec_receive_last(bd.flags);

            s->regs[ENET_EIR] |= ENET_INT_RXF;
        } else {
            s->regs[ENET_EIR] |= ENET_INT_RXB;
        }
        imx_fec_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & ENET_BD_W) != 0) {
            addr = s->regs[ENET_RDSR];
        } else {
            addr += sizeof(bd);
        }
    }
    s->rx_descriptor = addr;
    imx_eth_enable_rx(s, false);
    imx_eth_update(s);
    return len;
}

static ssize_t imx_enet_receive(NetClientState *nc, const uint8_t *buf,
                                size_t len)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));
    IMXENETBufDesc bd;
    uint32_t flags = 0;
    uint32_t addr;
    uint32_t crc;
    uint32_t buf_addr;
    uint8_t *crc_ptr;
    unsigned int buf_len;
    size_t size = len;
    bool shift16 = s->regs[ENET_RACC] & ENET_RACC_SHIFT16;

    trace_imx_enet_receive(size);

    if (!s->regs[ENET_RDAR]) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Unexpected packet\n",
                      TYPE_IMX_FEC, __func__);
        return 0;
    }

    crc = cpu_to_be32(crc32(~0, buf, size));
    /* Increase size by 4, loop below reads the last 4 bytes from crc_ptr. */
    size += 4;
    crc_ptr = (uint8_t *) &crc;

    if (shift16) {
        size += 2;
    }

    /* Huge frames are truncated. */
    if (size > s->regs[ENET_FTRL]) {
        size = s->regs[ENET_FTRL];
        flags |= ENET_BD_TR | ENET_BD_LG;
    }

    /* Frames larger than the user limit just set error flags.  */
    if (size > (s->regs[ENET_RCR] >> 16)) {
        flags |= ENET_BD_LG;
    }

    addr = s->rx_descriptor;
    while (size > 0) {
        imx_enet_read_bd(&bd, addr);
        if ((bd.flags & ENET_BD_E) == 0) {
            /* No descriptors available.  Bail out.  */
            /*
             * FIXME: This is wrong. We should probably either
             * save the remainder for when more RX buffers are
             * available, or flag an error.
             */
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Lost end of frame\n",
                          TYPE_IMX_FEC, __func__);
            break;
        }
        buf_len = MIN(size, s->regs[ENET_MRBR]);
        bd.length = buf_len;
        size -= buf_len;

        trace_imx_enet_receive_len(addr, bd.length);

        /* The last 4 bytes are the CRC.  */
        if (size < 4) {
            buf_len += size - 4;
        }
        buf_addr = bd.data;

        if (shift16) {
            /*
             * If SHIFT16 bit of ENETx_RACC register is set we need to
             * align the payload to 4-byte boundary.
             */
            const uint8_t zeros[2] = { 0 };

            dma_memory_write(&address_space_memory, buf_addr, zeros,
                             sizeof(zeros), MEMTXATTRS_UNSPECIFIED);

            buf_addr += sizeof(zeros);
            buf_len  -= sizeof(zeros);

            /* We only do this once per Ethernet frame */
            shift16 = false;
        }

        dma_memory_write(&address_space_memory, buf_addr, buf, buf_len,
                         MEMTXATTRS_UNSPECIFIED);
        buf += buf_len;
        if (size < 4) {
            dma_memory_write(&address_space_memory, buf_addr + buf_len,
                             crc_ptr, 4 - size, MEMTXATTRS_UNSPECIFIED);
            crc_ptr += 4 - size;
        }
        bd.flags &= ~ENET_BD_E;
        if (size == 0) {
            /* Last buffer in frame.  */
            bd.flags |= flags | ENET_BD_L;

            trace_imx_enet_receive_last(bd.flags);

            /* Indicate that we've updated the last buffer descriptor. */
            bd.last_buffer = ENET_BD_BDU;
            if (bd.option & ENET_BD_RX_INT) {
                s->regs[ENET_EIR] |= ENET_INT_RXF;
            }
        } else {
            if (bd.option & ENET_BD_RX_INT) {
                s->regs[ENET_EIR] |= ENET_INT_RXB;
            }
        }
        imx_enet_write_bd(&bd, addr);
        /* Advance to the next descriptor.  */
        if ((bd.flags & ENET_BD_W) != 0) {
            addr = s->regs[ENET_RDSR];
        } else {
            addr += sizeof(bd);
        }
    }
    s->rx_descriptor = addr;
    imx_eth_enable_rx(s, false);
    imx_eth_update(s);
    return len;
}

static ssize_t imx_eth_receive(NetClientState *nc, const uint8_t *buf,
                                size_t len)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));

    if (!s->is_fec && (s->regs[ENET_ECR] & ENET_ECR_EN1588)) {
        return imx_enet_receive(nc, buf, len);
    } else {
        return imx_fec_receive(nc, buf, len);
    }
}

static const MemoryRegionOps imx_eth_ops = {
    .read                  = imx_eth_read,
    .write                 = imx_eth_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness            = DEVICE_NATIVE_ENDIAN,
};

static void imx_eth_cleanup(NetClientState *nc)
{
    IMXFECState *s = IMX_FEC(qemu_get_nic_opaque(nc));

    s->nic = NULL;
}

static NetClientInfo imx_eth_net_info = {
    .type                = NET_CLIENT_DRIVER_NIC,
    .size                = sizeof(NICState),
    .can_receive         = imx_eth_can_receive,
    .receive             = imx_eth_receive,
    .cleanup             = imx_eth_cleanup,
    .link_status_changed = imx_eth_set_link,
};


static void imx_eth_realize(DeviceState *dev, Error **errp)
{
    IMXFECState *s = IMX_FEC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &imx_eth_ops, s,
                          TYPE_IMX_FEC, FSL_IMX25_FEC_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq[0]);
    sysbus_init_irq(sbd, &s->irq[1]);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);

    s->nic = qemu_new_nic(&imx_eth_net_info, &s->conf,
                          object_get_typename(OBJECT(dev)),
                          dev->id, s);

    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static Property imx_eth_properties[] = {
    DEFINE_NIC_PROPERTIES(IMXFECState, conf),
    DEFINE_PROP_UINT32("tx-ring-num", IMXFECState, tx_ring_num, 1),
    DEFINE_PROP_UINT32("phy-num", IMXFECState, phy_num, 0),
    DEFINE_PROP_BOOL("phy-connected", IMXFECState, phy_connected, true),
    DEFINE_PROP_LINK("phy-consumer", IMXFECState, phy_consumer, TYPE_IMX_FEC,
                     IMXFECState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void imx_eth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd    = &vmstate_imx_eth;
    dc->reset   = imx_eth_reset;
    device_class_set_props(dc, imx_eth_properties);
    dc->realize = imx_eth_realize;
    dc->desc    = "i.MX FEC/ENET Ethernet Controller";
}

static void imx_fec_init(Object *obj)
{
    IMXFECState *s = IMX_FEC(obj);

    s->is_fec = true;
}

static void imx_enet_init(Object *obj)
{
    IMXFECState *s = IMX_FEC(obj);

    s->is_fec = false;
}

static const TypeInfo imx_fec_info = {
    .name          = TYPE_IMX_FEC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXFECState),
    .instance_init = imx_fec_init,
    .class_init    = imx_eth_class_init,
};

static const TypeInfo imx_enet_info = {
    .name          = TYPE_IMX_ENET,
    .parent        = TYPE_IMX_FEC,
    .instance_init = imx_enet_init,
};

static void imx_eth_register_types(void)
{
    type_register_static(&imx_fec_info);
    type_register_static(&imx_enet_info);
}

type_init(imx_eth_register_types)
