/*
 *  QEMU model of the Milkymist minimac2 block.
 *
 *  Copyright (c) 2011 Michael Walle <michael@walle.cc>
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
 *   not available yet
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h" /* FIXME: why does this use TARGET_PAGE_ALIGN? */
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "net/net.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"

#include <zlib.h>

enum {
    R_SETUP = 0,
    R_MDIO,
    R_STATE0,
    R_COUNT0,
    R_STATE1,
    R_COUNT1,
    R_TXCOUNT,
    R_MAX
};

enum {
    SETUP_PHY_RST = (1<<0),
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

#define MINIMAC2_MTU 1530
#define MINIMAC2_BUFFER_SIZE 2048

struct MilkymistMinimac2MdioState {
    int last_clk;
    int count;
    uint32_t data;
    uint16_t data_out;
    int state;

    uint8_t phy_addr;
    uint8_t reg_addr;
};
typedef struct MilkymistMinimac2MdioState MilkymistMinimac2MdioState;

#define TYPE_MILKYMIST_MINIMAC2 "milkymist-minimac2"
#define MILKYMIST_MINIMAC2(obj) \
    OBJECT_CHECK(MilkymistMinimac2State, (obj), TYPE_MILKYMIST_MINIMAC2)

struct MilkymistMinimac2State {
    SysBusDevice parent_obj;

    NICState *nic;
    NICConf conf;
    char *phy_model;
    MemoryRegion buffers;
    MemoryRegion regs_region;

    qemu_irq rx_irq;
    qemu_irq tx_irq;

    uint32_t regs[R_MAX];

    MilkymistMinimac2MdioState mdio;

    uint16_t phy_regs[R_PHY_MAX];

    uint8_t *rx0_buf;
    uint8_t *rx1_buf;
    uint8_t *tx_buf;
};
typedef struct MilkymistMinimac2State MilkymistMinimac2State;

static const uint8_t preamble_sfd[] = {
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xd5
};

static void minimac2_mdio_write_reg(MilkymistMinimac2State *s,
        uint8_t phy_addr, uint8_t reg_addr, uint16_t value)
{
    trace_milkymist_minimac2_mdio_write(phy_addr, reg_addr, value);

    /* nop */
}

static uint16_t minimac2_mdio_read_reg(MilkymistMinimac2State *s,
        uint8_t phy_addr, uint8_t reg_addr)
{
    uint16_t r = s->phy_regs[reg_addr];

    trace_milkymist_minimac2_mdio_read(phy_addr, reg_addr, r);

    return r;
}

static void minimac2_update_mdio(MilkymistMinimac2State *s)
{
    MilkymistMinimac2MdioState *m = &s->mdio;

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
                m->data_out = minimac2_mdio_read_reg(s, m->phy_addr,
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
                minimac2_mdio_write_reg(s, m->phy_addr, m->reg_addr, data);
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
        qemu_log_mask(LOG_GUEST_ERROR, "milkymist_minimac2: frame too big "
                      "(%zd bytes)\n", payload_size);
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

static void minimac2_tx(MilkymistMinimac2State *s)
{
    uint32_t txcount = s->regs[R_TXCOUNT];
    uint8_t *buf = s->tx_buf;

    if (txcount < 64) {
        error_report("milkymist_minimac2: ethernet frame too small (%u < %u)",
                txcount, 64);
        goto err;
    }

    if (txcount > MINIMAC2_MTU) {
        error_report("milkymist_minimac2: MTU exceeded (%u > %u)",
                txcount, MINIMAC2_MTU);
        goto err;
    }

    if (memcmp(buf, preamble_sfd, 8) != 0) {
        error_report("milkymist_minimac2: frame doesn't contain the preamble "
                "and/or the SFD (%02x %02x %02x %02x %02x %02x %02x %02x)",
                buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        goto err;
    }

    trace_milkymist_minimac2_tx_frame(txcount - 12);

    /* send packet, skipping preamble and sfd */
    qemu_send_packet_raw(qemu_get_queue(s->nic), buf + 8, txcount - 12);

    s->regs[R_TXCOUNT] = 0;

err:
    trace_milkymist_minimac2_pulse_irq_tx();
    qemu_irq_pulse(s->tx_irq);
}

static void update_rx_interrupt(MilkymistMinimac2State *s)
{
    if (s->regs[R_STATE0] == STATE_PENDING
            || s->regs[R_STATE1] == STATE_PENDING) {
        trace_milkymist_minimac2_raise_irq_rx();
        qemu_irq_raise(s->rx_irq);
    } else {
        trace_milkymist_minimac2_lower_irq_rx();
        qemu_irq_lower(s->rx_irq);
    }
}

static ssize_t minimac2_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    MilkymistMinimac2State *s = qemu_get_nic_opaque(nc);

    uint32_t r_count;
    uint32_t r_state;
    uint8_t *rx_buf;

    size_t frame_size;

    trace_milkymist_minimac2_rx_frame(buf, size);

    /* choose appropriate slot */
    if (s->regs[R_STATE0] == STATE_LOADED) {
        r_count = R_COUNT0;
        r_state = R_STATE0;
        rx_buf = s->rx0_buf;
    } else if (s->regs[R_STATE1] == STATE_LOADED) {
        r_count = R_COUNT1;
        r_state = R_STATE1;
        rx_buf = s->rx1_buf;
    } else {
        return 0;
    }

    /* assemble frame */
    frame_size = assemble_frame(rx_buf, MINIMAC2_BUFFER_SIZE, buf, size);

    if (frame_size == 0) {
        return size;
    }

    trace_milkymist_minimac2_rx_transfer(rx_buf, frame_size);

    /* update slot */
    s->regs[r_count] = frame_size;
    s->regs[r_state] = STATE_PENDING;

    update_rx_interrupt(s);

    return size;
}

static uint64_t
minimac2_read(void *opaque, hwaddr addr, unsigned size)
{
    MilkymistMinimac2State *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_SETUP:
    case R_MDIO:
    case R_STATE0:
    case R_COUNT0:
    case R_STATE1:
    case R_COUNT1:
    case R_TXCOUNT:
        r = s->regs[addr];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "milkymist_minimac2_rd%d: 0x%" HWADDR_PRIx "\n",
                      size, addr << 2);
        break;
    }

    trace_milkymist_minimac2_memory_read(addr << 2, r);

    return r;
}

static int minimac2_can_rx(MilkymistMinimac2State *s)
{
    if (s->regs[R_STATE0] == STATE_LOADED) {
        return 1;
    }
    if (s->regs[R_STATE1] == STATE_LOADED) {
        return 1;
    }

    return 0;
}

static void
minimac2_write(void *opaque, hwaddr addr, uint64_t value,
               unsigned size)
{
    MilkymistMinimac2State *s = opaque;

    trace_milkymist_minimac2_memory_write(addr, value);

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

        minimac2_update_mdio(s);
    } break;
    case R_TXCOUNT:
        s->regs[addr] = value;
        if (value > 0) {
            minimac2_tx(s);
        }
        break;
    case R_STATE0:
    case R_STATE1:
        s->regs[addr] = value;
        update_rx_interrupt(s);
        if (minimac2_can_rx(s)) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    case R_SETUP:
    case R_COUNT0:
    case R_COUNT1:
        s->regs[addr] = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "milkymist_minimac2_wr%d: 0x%" HWADDR_PRIx
                      " = 0x%" PRIx64 "\n",
                      size, addr << 2, value);
        break;
    }
}

static const MemoryRegionOps minimac2_ops = {
    .read = minimac2_read,
    .write = minimac2_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void milkymist_minimac2_reset(DeviceState *d)
{
    MilkymistMinimac2State *s = MILKYMIST_MINIMAC2(d);
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

static NetClientInfo net_milkymist_minimac2_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = minimac2_rx,
};

static void milkymist_minimac2_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    MilkymistMinimac2State *s = MILKYMIST_MINIMAC2(dev);
    size_t buffers_size = TARGET_PAGE_ALIGN(3 * MINIMAC2_BUFFER_SIZE);

    sysbus_init_irq(sbd, &s->rx_irq);
    sysbus_init_irq(sbd, &s->tx_irq);

    memory_region_init_io(&s->regs_region, OBJECT(dev), &minimac2_ops, s,
                          "milkymist-minimac2", R_MAX * 4);
    sysbus_init_mmio(sbd, &s->regs_region);

    /* register buffers memory */
    memory_region_init_ram_nomigrate(&s->buffers, OBJECT(dev), "milkymist-minimac2.buffers",
                           buffers_size, &error_fatal);
    vmstate_register_ram_global(&s->buffers);
    s->rx0_buf = memory_region_get_ram_ptr(&s->buffers);
    s->rx1_buf = s->rx0_buf + MINIMAC2_BUFFER_SIZE;
    s->tx_buf = s->rx1_buf + MINIMAC2_BUFFER_SIZE;

    sysbus_init_mmio(sbd, &s->buffers);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_milkymist_minimac2_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static const VMStateDescription vmstate_milkymist_minimac2_mdio = {
    .name = "milkymist-minimac2-mdio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(last_clk, MilkymistMinimac2MdioState),
        VMSTATE_INT32(count, MilkymistMinimac2MdioState),
        VMSTATE_UINT32(data, MilkymistMinimac2MdioState),
        VMSTATE_UINT16(data_out, MilkymistMinimac2MdioState),
        VMSTATE_INT32(state, MilkymistMinimac2MdioState),
        VMSTATE_UINT8(phy_addr, MilkymistMinimac2MdioState),
        VMSTATE_UINT8(reg_addr, MilkymistMinimac2MdioState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_milkymist_minimac2 = {
    .name = "milkymist-minimac2",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistMinimac2State, R_MAX),
        VMSTATE_UINT16_ARRAY(phy_regs, MilkymistMinimac2State, R_PHY_MAX),
        VMSTATE_STRUCT(mdio, MilkymistMinimac2State, 0,
                vmstate_milkymist_minimac2_mdio, MilkymistMinimac2MdioState),
        VMSTATE_END_OF_LIST()
    }
};

static Property milkymist_minimac2_properties[] = {
    DEFINE_NIC_PROPERTIES(MilkymistMinimac2State, conf),
    DEFINE_PROP_STRING("phy_model", MilkymistMinimac2State, phy_model),
    DEFINE_PROP_END_OF_LIST(),
};

static void milkymist_minimac2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = milkymist_minimac2_realize;
    dc->reset = milkymist_minimac2_reset;
    dc->vmsd = &vmstate_milkymist_minimac2;
    device_class_set_props(dc, milkymist_minimac2_properties);
}

static const TypeInfo milkymist_minimac2_info = {
    .name          = TYPE_MILKYMIST_MINIMAC2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MilkymistMinimac2State),
    .class_init    = milkymist_minimac2_class_init,
};

static void milkymist_minimac2_register_types(void)
{
    type_register_static(&milkymist_minimac2_info);
}

type_init(milkymist_minimac2_register_types)
