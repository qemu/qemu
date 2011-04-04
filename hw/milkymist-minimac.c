/*
 *  QEMU model of the Milkymist minimac block.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
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
 *
 *
 * Specification available at:
 *   http://www.milkymist.org/socdoc/minimac.pdf
 *
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "net.h"
#include "qemu-error.h"

#include <zlib.h>

enum {
    R_SETUP = 0,
    R_MDIO,
    R_STATE0,
    R_ADDR0,
    R_COUNT0,
    R_STATE1,
    R_ADDR1,
    R_COUNT1,
    R_STATE2,
    R_ADDR2,
    R_COUNT2,
    R_STATE3,
    R_ADDR3,
    R_COUNT3,
    R_TXADDR,
    R_TXCOUNT,
    R_MAX
};

enum {
    SETUP_RX_RST = (1<<0),
    SETUP_TX_RST = (1<<2),
};

enum {
    MDIO_DO  = (1<<0),
    MDIO_DI  = (1<<1),
    MDIO_OE  = (1<<2),
    MDIO_CLK = (1<<3),
};

enum {
    STATE_EMPTY   = 0,
    STATE_LOADED  = 1,
    STATE_PENDING = 2,
};

enum {
    MDIO_OP_WRITE = 1,
    MDIO_OP_READ  = 2,
};

enum mdio_state {
    MDIO_STATE_IDLE,
    MDIO_STATE_READING,
    MDIO_STATE_WRITING,
};

enum {
    R_PHY_ID1  = 2,
    R_PHY_ID2  = 3,
    R_PHY_MAX  = 32
};

#define MINIMAC_MTU 1530

struct MilkymistMinimacMdioState {
    int last_clk;
    int count;
    uint32_t data;
    uint16_t data_out;
    int state;

    uint8_t phy_addr;
    uint8_t reg_addr;
};
typedef struct MilkymistMinimacMdioState MilkymistMinimacMdioState;

struct MilkymistMinimacState {
    SysBusDevice busdev;
    NICState *nic;
    NICConf conf;
    char *phy_model;

    qemu_irq rx_irq;
    qemu_irq tx_irq;

    uint32_t regs[R_MAX];

    MilkymistMinimacMdioState mdio;

    uint16_t phy_regs[R_PHY_MAX];
};
typedef struct MilkymistMinimacState MilkymistMinimacState;

static const uint8_t preamble_sfd[] = {
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xd5
};

static void minimac_mdio_write_reg(MilkymistMinimacState *s,
        uint8_t phy_addr, uint8_t reg_addr, uint16_t value)
{
    trace_milkymist_minimac_mdio_write(phy_addr, reg_addr, value);

    /* nop */
}

static uint16_t minimac_mdio_read_reg(MilkymistMinimacState *s,
        uint8_t phy_addr, uint8_t reg_addr)
{
    uint16_t r = s->phy_regs[reg_addr];

    trace_milkymist_minimac_mdio_read(phy_addr, reg_addr, r);

    return r;
}

static void minimac_update_mdio(MilkymistMinimacState *s)
{
    MilkymistMinimacMdioState *m = &s->mdio;

    /* detect rising clk edge */
    if (m->last_clk == 0 && (s->regs[R_MDIO] & MDIO_CLK)) {
        /* shift data in */
        int bit = ((s->regs[R_MDIO] & MDIO_DO)
                   && (s->regs[R_MDIO] & MDIO_OE)) ? 1 : 0;
        m->data = (m->data << 1) | bit;

        /* check for sync */
        if (m->data == 0xffffffff) {
            m->count = 32;
        }

        if (m->count == 16) {
            uint8_t start = (m->data >> 14) & 0x3;
            uint8_t op = (m->data >> 12) & 0x3;
            uint8_t ta = (m->data) & 0x3;

            if (start == 1 && op == MDIO_OP_WRITE && ta == 2) {
                m->state = MDIO_STATE_WRITING;
            } else if (start == 1 && op == MDIO_OP_READ && (ta & 1) == 0) {
                m->state = MDIO_STATE_READING;
            } else {
                m->state = MDIO_STATE_IDLE;
            }

            if (m->state != MDIO_STATE_IDLE) {
                m->phy_addr = (m->data >> 7) & 0x1f;
                m->reg_addr = (m->data >> 2) & 0x1f;
            }

            if (m->state == MDIO_STATE_READING) {
                m->data_out = minimac_mdio_read_reg(s, m->phy_addr,
                        m->reg_addr);
            }
        }

        if (m->count < 16 && m->state == MDIO_STATE_READING) {
            int bit = (m->data_out & 0x8000) ? 1 : 0;
            m->data_out <<= 1;

            if (bit) {
                s->regs[R_MDIO] |= MDIO_DI;
            } else {
                s->regs[R_MDIO] &= ~MDIO_DI;
            }
        }

        if (m->count == 0 && m->state) {
            if (m->state == MDIO_STATE_WRITING) {
                uint16_t data = m->data & 0xffff;
                minimac_mdio_write_reg(s, m->phy_addr, m->reg_addr, data);
            }
            m->state = MDIO_STATE_IDLE;
        }
        m->count--;
    }

    m->last_clk = (s->regs[R_MDIO] & MDIO_CLK) ? 1 : 0;
}

static size_t assemble_frame(uint8_t *buf, size_t size,
        const uint8_t *payload, size_t payload_size)
{
    uint32_t crc;

    if (size < payload_size + 12) {
        error_report("milkymist_minimac: received too big ethernet frame");
        return 0;
    }

    /* prepend preamble and sfd */
    memcpy(buf, preamble_sfd, 8);

    /* now copy the payload */
    memcpy(buf + 8, payload, payload_size);

    /* pad frame if needed */
    if (payload_size < 60) {
        memset(buf + payload_size + 8, 0, 60 - payload_size);
        payload_size = 60;
    }

    /* append fcs */
    crc = cpu_to_le32(crc32(0, buf + 8, payload_size));
    memcpy(buf + payload_size + 8, &crc, 4);

    return payload_size + 12;
}

static void minimac_tx(MilkymistMinimacState *s)
{
    uint8_t buf[MINIMAC_MTU];
    uint32_t txcount = s->regs[R_TXCOUNT];

    /* do nothing if transmission logic is in reset */
    if (s->regs[R_SETUP] & SETUP_TX_RST) {
        return;
    }

    if (txcount < 64) {
        error_report("milkymist_minimac: ethernet frame too small (%u < %u)\n",
                txcount, 64);
        return;
    }

    if (txcount > MINIMAC_MTU) {
        error_report("milkymist_minimac: MTU exceeded (%u > %u)\n",
                txcount, MINIMAC_MTU);
        return;
    }

    /* dma */
    cpu_physical_memory_read(s->regs[R_TXADDR], buf, txcount);

    if (memcmp(buf, preamble_sfd, 8) != 0) {
        error_report("milkymist_minimac: frame doesn't contain the preamble "
                "and/or the SFD (%02x %02x %02x %02x %02x %02x %02x %02x)\n",
                buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        return;
    }

    trace_milkymist_minimac_tx_frame(txcount - 12);

    /* send packet, skipping preamble and sfd */
    qemu_send_packet_raw(&s->nic->nc, buf + 8, txcount - 12);

    s->regs[R_TXCOUNT] = 0;

    trace_milkymist_minimac_pulse_irq_tx();
    qemu_irq_pulse(s->tx_irq);
}

static ssize_t minimac_rx(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    MilkymistMinimacState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    uint32_t r_addr;
    uint32_t r_count;
    uint32_t r_state;

    uint8_t frame_buf[MINIMAC_MTU];
    size_t frame_size;

    trace_milkymist_minimac_rx_frame(buf, size);

    /* discard frames if nic is in reset */
    if (s->regs[R_SETUP] & SETUP_RX_RST) {
        return size;
    }

    /* choose appropriate slot */
    if (s->regs[R_STATE0] == STATE_LOADED) {
        r_addr = R_ADDR0;
        r_count = R_COUNT0;
        r_state = R_STATE0;
    } else if (s->regs[R_STATE1] == STATE_LOADED) {
        r_addr = R_ADDR1;
        r_count = R_COUNT1;
        r_state = R_STATE1;
    } else if (s->regs[R_STATE2] == STATE_LOADED) {
        r_addr = R_ADDR2;
        r_count = R_COUNT2;
        r_state = R_STATE2;
    } else if (s->regs[R_STATE3] == STATE_LOADED) {
        r_addr = R_ADDR3;
        r_count = R_COUNT3;
        r_state = R_STATE3;
    } else {
        trace_milkymist_minimac_drop_rx_frame(buf);
        return size;
    }

    /* assemble frame */
    frame_size = assemble_frame(frame_buf, sizeof(frame_buf), buf, size);

    if (frame_size == 0) {
        return size;
    }

    trace_milkymist_minimac_rx_transfer(buf, frame_size);

    /* do dma */
    cpu_physical_memory_write(s->regs[r_addr], frame_buf, frame_size);

    /* update slot */
    s->regs[r_count] = frame_size;
    s->regs[r_state] = STATE_PENDING;

    trace_milkymist_minimac_pulse_irq_rx();
    qemu_irq_pulse(s->rx_irq);

    return size;
}

static uint32_t
minimac_read(void *opaque, target_phys_addr_t addr)
{
    MilkymistMinimacState *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_SETUP:
    case R_MDIO:
    case R_STATE0:
    case R_ADDR0:
    case R_COUNT0:
    case R_STATE1:
    case R_ADDR1:
    case R_COUNT1:
    case R_STATE2:
    case R_ADDR2:
    case R_COUNT2:
    case R_STATE3:
    case R_ADDR3:
    case R_COUNT3:
    case R_TXADDR:
    case R_TXCOUNT:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_minimac: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_minimac_memory_read(addr << 2, r);

    return r;
}

static void
minimac_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MilkymistMinimacState *s = opaque;

    trace_milkymist_minimac_memory_read(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_MDIO:
    {
        /* MDIO_DI is read only */
        int mdio_di = (s->regs[R_MDIO] & MDIO_DI);
        s->regs[R_MDIO] = value;
        if (mdio_di) {
            s->regs[R_MDIO] |= mdio_di;
        } else {
            s->regs[R_MDIO] &= ~mdio_di;
        }

        minimac_update_mdio(s);
    } break;
    case R_TXCOUNT:
        s->regs[addr] = value;
        if (value > 0) {
            minimac_tx(s);
        }
        break;
    case R_SETUP:
    case R_STATE0:
    case R_ADDR0:
    case R_COUNT0:
    case R_STATE1:
    case R_ADDR1:
    case R_COUNT1:
    case R_STATE2:
    case R_ADDR2:
    case R_COUNT2:
    case R_STATE3:
    case R_ADDR3:
    case R_COUNT3:
    case R_TXADDR:
        s->regs[addr] = value;
        break;

    default:
        error_report("milkymist_minimac: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static CPUReadMemoryFunc * const minimac_read_fn[] = {
    NULL,
    NULL,
    &minimac_read,
};

static CPUWriteMemoryFunc * const minimac_write_fn[] = {
    NULL,
    NULL,
    &minimac_write,
};

static int minimac_can_rx(VLANClientState *nc)
{
    MilkymistMinimacState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    /* discard frames if nic is in reset */
    if (s->regs[R_SETUP] & SETUP_RX_RST) {
        return 1;
    }

    if (s->regs[R_STATE0] == STATE_LOADED) {
        return 1;
    }
    if (s->regs[R_STATE1] == STATE_LOADED) {
        return 1;
    }
    if (s->regs[R_STATE2] == STATE_LOADED) {
        return 1;
    }
    if (s->regs[R_STATE3] == STATE_LOADED) {
        return 1;
    }

    return 0;
}

static void minimac_cleanup(VLANClientState *nc)
{
    MilkymistMinimacState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    s->nic = NULL;
}

static void milkymist_minimac_reset(DeviceState *d)
{
    MilkymistMinimacState *s =
            container_of(d, MilkymistMinimacState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
    for (i = 0; i < R_PHY_MAX; i++) {
        s->phy_regs[i] = 0;
    }

    /* defaults */
    s->phy_regs[R_PHY_ID1] = 0x0022; /* Micrel KSZ8001L */
    s->phy_regs[R_PHY_ID2] = 0x161a;
}

static NetClientInfo net_milkymist_minimac_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = minimac_can_rx,
    .receive = minimac_rx,
    .cleanup = minimac_cleanup,
};

static int milkymist_minimac_init(SysBusDevice *dev)
{
    MilkymistMinimacState *s = FROM_SYSBUS(typeof(*s), dev);
    int regs;

    sysbus_init_irq(dev, &s->rx_irq);
    sysbus_init_irq(dev, &s->tx_irq);

    regs = cpu_register_io_memory(minimac_read_fn, minimac_write_fn, s,
            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, regs);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_milkymist_minimac_info, &s->conf,
                          dev->qdev.info->name, dev->qdev.id, s);
    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);

    return 0;
}

static const VMStateDescription vmstate_milkymist_minimac_mdio = {
    .name = "milkymist_minimac_mdio",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT32(last_clk, MilkymistMinimacMdioState),
        VMSTATE_INT32(count, MilkymistMinimacMdioState),
        VMSTATE_UINT32(data, MilkymistMinimacMdioState),
        VMSTATE_UINT16(data_out, MilkymistMinimacMdioState),
        VMSTATE_INT32(state, MilkymistMinimacMdioState),
        VMSTATE_UINT8(phy_addr, MilkymistMinimacMdioState),
        VMSTATE_UINT8(reg_addr, MilkymistMinimacMdioState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_milkymist_minimac = {
    .name = "milkymist-minimac",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistMinimacState, R_MAX),
        VMSTATE_UINT16_ARRAY(phy_regs, MilkymistMinimacState, R_PHY_MAX),
        VMSTATE_STRUCT(mdio, MilkymistMinimacState, 0,
                vmstate_milkymist_minimac_mdio, MilkymistMinimacMdioState),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo milkymist_minimac_info = {
    .init = milkymist_minimac_init,
    .qdev.name  = "milkymist-minimac",
    .qdev.size  = sizeof(MilkymistMinimacState),
    .qdev.vmsd  = &vmstate_milkymist_minimac,
    .qdev.reset = milkymist_minimac_reset,
    .qdev.props = (Property[]) {
        DEFINE_NIC_PROPERTIES(MilkymistMinimacState, conf),
        DEFINE_PROP_STRING("phy_model", MilkymistMinimacState, phy_model),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void milkymist_minimac_register(void)
{
    sysbus_register_withprop(&milkymist_minimac_info);
}

device_init(milkymist_minimac_register)
