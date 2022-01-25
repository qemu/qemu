/*
 * Aeroflex Gaisler GRETH 10/100 Ethernet MAC
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "sysemu/dma.h"
#include "net/eth.h"

#include "hw/net/greth.h"

#define REG_CONTROL         0x0
#define REG_STATUS          0x4
#define REG_MAC_MSB         0x8
#define REG_MAC_LSB         0xc
#define REG_SEND_DESCR_PTR  0x14
#define REG_RECV_DESCR_PTR  0x18

#define CONTROL_MULTICAST_EN    0x800
#define CONTROL_SPEED           0x80
#define CONTROL_RESET           0x40
#define CONTROL_PROMISCUOUS     0x20
#define CONTROL_FULL_DUPLEX     0x10
#define CONTROL_RECV_IRQ_EN     0x8
#define CONTROL_SEND_IRQ_EN     0x4
#define CONTROL_RECV_EN         0x2
#define CONTROL_SEND_EN         0x1

#define CONTROL_MASK \
    (CONTROL_MULTICAST_EN | CONTROL_SPEED | CONTROL_PROMISCUOUS | CONTROL_FULL_DUPLEX | \
    CONTROL_RECV_IRQ_EN | CONTROL_SEND_IRQ_EN | CONTROL_RECV_EN |CONTROL_SEND_EN )

#define STATUS_INVALID_ADDR     0x80
#define STATUS_TOO_SMALL        0x40
#define STATUS_SEND_DMA_ERROR   0x20
#define STATUS_RECV_DMA_ERROR   0x10
#define STATUS_SEND_IRQ         0x8
#define STATUS_RECV_IRQ         0x4
#define STATUS_SEND_ERROR       0x2
#define STATUS_RECV_ERROR       0x1

#define STATUS_MASK \
    (STATUS_SEND_DMA_ERROR | STATUS_RECV_DMA_ERROR | STATUS_SEND_IRQ | STATUS_RECV_IRQ | \
    STATUS_SEND_ERROR | STATUS_RECV_ERROR)

#define DESCR_PTR_BASE_MASK     0xfffffc00
#define DESCR_PTR_OFFSET_MASK   0x3fc
#define DESCR_PTR_INCREMENT     0x8

/* DMA logic */
typedef struct {
    union {
        struct {
            uint32_t length : 11;
            uint32_t enabled : 1;
            uint32_t wrap : 1;
            uint32_t irq_enabled : 1;
            uint32_t underrun_err : 1;
            uint32_t limit_err : 1;
            uint32_t : 16;
        };
        uint32_t cmd;
    };

    uint32_t address;
} send_desc_t;

typedef struct {
    union {
        struct {
            uint32_t length : 11;
            uint32_t enabled : 1;
            uint32_t wrap : 1;
            uint32_t irq_enabled : 1;
            uint32_t alignment_err : 1;
            uint32_t frame_too_long_err : 1;
            uint32_t crc_err : 1;
            uint32_t overrun_err : 1;
            uint32_t length_err : 1;
            uint32_t : 7;
            uint32_t multicast : 1;
            uint32_t : 5;
        };
        uint32_t cmd;
    };

    uint32_t address;
} recv_desc_t;

static int read_send_desc(GRETHState *s, dma_addr_t addr, send_desc_t *desc)
{
    if (dma_memory_read(s->addr_space, addr, desc, sizeof(send_desc_t))) {
        return -1;
    }
    desc->cmd = cpu_to_be32(desc->cmd);
    desc->address = cpu_to_be32(desc->address);
    return 0;
}

static int write_send_desc(GRETHState *s, dma_addr_t addr, send_desc_t *desc)
{
    send_desc_t temp;

    temp.cmd = be32_to_cpu(desc->cmd);
    temp.address = be32_to_cpu(desc->address);
    if (dma_memory_write(s->addr_space, addr, &temp, sizeof(send_desc_t))) {
        return -1;
    }
    return 0;
}

static int read_recv_desc(GRETHState *s, dma_addr_t addr, recv_desc_t *desc)
{
    if (dma_memory_read(s->addr_space, addr, desc, sizeof(recv_desc_t))) {
        return -1;
    }
    desc->cmd = cpu_to_be32(desc->cmd);
    desc->address = cpu_to_be32(desc->address);
    return 0;
}

static int write_recv_desc(GRETHState *s, dma_addr_t addr, recv_desc_t *desc)
{
    send_desc_t temp;

    temp.cmd = be32_to_cpu(desc->cmd);
    temp.address = be32_to_cpu(desc->address);
    if (dma_memory_write(s->addr_space, addr, &temp, sizeof(recv_desc_t))) {
        return -1;
    }
    return 0;
}

/* Network logic */
// needed to generate irqs
static void greth_update_irq(GRETHState *s);

static bool greth_can_receive(NetClientState *nc)
{
    GRETHState *s = GRETH(qemu_get_nic_opaque(nc));

    if (!(s->ctrl & CONTROL_RECV_EN)) {
        return 0;
    }

    recv_desc_t desc;
    if (read_recv_desc(s, s->recv_desc, &desc)) {
        s->status |= STATUS_RECV_DMA_ERROR;
        return 0;
    }

    if (!desc.enabled) {
        return 0;
    }

    return 1;
}

static int check_packet_type(GRETHState *s, const uint8_t *buf)
{
    eth_pkt_types_e pkt_type = get_eth_packet_type(PKT_GET_ETH_HDR(buf));

    switch (pkt_type) {
    case ETH_PKT_MCAST:
        if (!(s->ctrl & CONTROL_MULTICAST_EN)) {
            return -1;
        }
        break;

    case ETH_PKT_UCAST:
        if (memcmp(buf, s->conf.macaddr.a, ETH_ALEN)) {
            return -1;
        }

    default: break;
    }

    return 0;
}

static ssize_t greth_receive(NetClientState *nc, const uint8_t *buf, size_t len)
{
    GRETHState *s = GRETH(qemu_get_nic_opaque(nc));
    recv_desc_t desc;

    if (!greth_can_receive(nc)) {
        return -1;
    }

    if (check_packet_type(s, buf)) {
        return len;
    }

    if (read_recv_desc(s, s->recv_desc, &desc)) {
        s->status |= STATUS_RECV_DMA_ERROR;
        return -1;
    }

    if (dma_memory_write(s->addr_space, desc.address, buf, len)) {
        s->status |= STATUS_RECV_DMA_ERROR;
        return -1;
    }

    desc.length = len;
    desc.enabled = 0;

    if (write_recv_desc(s, s->recv_desc, &desc)) {
        s->status |= STATUS_RECV_DMA_ERROR;
        return -1;
    }

    if (desc.irq_enabled) {
        s->status |= STATUS_RECV_IRQ;
        greth_update_irq(s);
    }

    // change address
    if (desc.wrap) {
        s->recv_desc &= DESCR_PTR_BASE_MASK;
    } else {
        uint32_t offset = s->recv_desc & DESCR_PTR_OFFSET_MASK;
        offset = (offset + DESCR_PTR_INCREMENT) & DESCR_PTR_OFFSET_MASK;
        s->recv_desc = (s->recv_desc & DESCR_PTR_BASE_MASK) + offset;
    }

    return len;
}

static void greth_send_all(GRETHState *s)
{
    uint8_t buffer[10*1024];
    send_desc_t desc;
    uint32_t addr = s->send_desc;

    while (1) {
        // get descriptor
        if (read_send_desc(s, addr, &desc)) {
            s->status |= STATUS_SEND_DMA_ERROR;
            return;
        }

        // stop if disabled
        if (!desc.enabled) {
            return;
        }

        // read data
        if (dma_memory_read(s->addr_space, desc.address, buffer, desc.length)) {
            s->status |= STATUS_SEND_DMA_ERROR;
            return;
        }

        qemu_send_packet(qemu_get_queue(s->nic), buffer, desc.length);

        // TODO: check IRQ bit and generate it
        if (desc.irq_enabled) {
            s->status |= STATUS_SEND_IRQ;
            greth_update_irq(s);
        }

        desc.enabled = 0;
        if (write_send_desc(s, addr, &desc)) {
            s->status |= STATUS_SEND_DMA_ERROR;
            return;
        }

        // change address
        if (desc.wrap) {
            s->send_desc &= DESCR_PTR_BASE_MASK;
        } else {
            uint32_t offset = s->send_desc & DESCR_PTR_OFFSET_MASK;
            offset = (offset + DESCR_PTR_INCREMENT) & DESCR_PTR_OFFSET_MASK;
            s->send_desc = (s->send_desc & DESCR_PTR_BASE_MASK) + offset;
        }
    }
}

/* Registers logic */
static void greth_update_irq(GRETHState *s)
{
    if ((s->status & STATUS_SEND_IRQ && s->ctrl & CONTROL_SEND_IRQ_EN) ||
        (s->status & STATUS_RECV_IRQ && s->ctrl & CONTROL_RECV_IRQ_EN)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void greth_soft_reset(GRETHState *s)
{
    s->ctrl = CONTROL_SPEED;
    s->status &= STATUS_MASK;
}

static uint64_t greth_read(void *opaque, hwaddr offset, unsigned size)
{
    GRETHState *s = GRETH(opaque);
    uint64_t val = 0;

    switch (offset) {
    case REG_CONTROL:
        val = s->ctrl;
        break;

    case REG_MAC_MSB:
        val = s->mac_msb;
        break;

    case REG_MAC_LSB:
        val = s->mac_lsb;
        break;

    case REG_SEND_DESCR_PTR:
        val = s->send_desc;
        break;

    case REG_RECV_DESCR_PTR:
        val = s->recv_desc;
        break;

    default: break;
    }

    // DeviceState *dev = DEVICE(opaque);
    // printf("%s_READ (%4lx): %8lx\n", dev->canonical_path, offset, val);
    return val;
}

static void greth_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    GRETHState *s = GRETH(opaque);

    switch (offset) {
    case REG_CONTROL:
        if (val & CONTROL_RESET) {
            greth_soft_reset(s);
            break;
        }

        if (val & CONTROL_SEND_EN) {
            greth_send_all(s);
        }

        if (val & CONTROL_RECV_EN) {
            if (greth_can_receive(qemu_get_queue(s->nic))) {
                qemu_flush_queued_packets(qemu_get_queue(s->nic));
            }
        }

        s->ctrl = val & CONTROL_MASK;
        break;

    case REG_STATUS:
        if (val & STATUS_SEND_IRQ) {
            val &= ~STATUS_SEND_IRQ;
        }

        if (val & STATUS_RECV_IRQ) {
            val &= ~STATUS_RECV_IRQ;
        }

        greth_update_irq(s);
        break;

    case REG_MAC_MSB:
        s->mac_msb = val;
        s->conf.macaddr.a[0] = val >> 8;
        s->conf.macaddr.a[1] = val >> 0;
        break;

    case REG_MAC_LSB:
        s->mac_lsb = val;
        s->conf.macaddr.a[2] = val >> 24;
        s->conf.macaddr.a[3] = val >> 16;
        s->conf.macaddr.a[4] = val >> 8;
        s->conf.macaddr.a[5] = val >> 0;
        break;

    case REG_SEND_DESCR_PTR:
        s->send_desc = val & (DESCR_PTR_BASE_MASK | DESCR_PTR_OFFSET_MASK);
        break;

    case REG_RECV_DESCR_PTR:
        s->recv_desc = val & (DESCR_PTR_BASE_MASK | DESCR_PTR_OFFSET_MASK);
        break;

    default: break;
    }

    // DeviceState *dev = DEVICE(opaque);
    // printf("%s_WRITE(%4lx): %8lx\n", dev->canonical_path, offset, val);
}

static void greth_reset(DeviceState *dev)
{
    GRETHState *s = GRETH(dev);

    greth_soft_reset(s);

    s->status = 0;
    s->mac_msb = 0;
    s->mac_lsb = 0;
    s->send_desc = 0;
    s->recv_desc = 0;
}

static const MemoryRegionOps greth_ops = {
    .read = greth_read,
    .write = greth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static NetClientInfo net_greth_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = greth_can_receive,
    .receive = greth_receive,
};

static void greth_realize(DeviceState *dev, Error **errp)
{
    GRETHState *s = GRETH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &greth_ops, s, "greth", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_greth_info, &s->conf,
                            object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    // set default address space
    if (s->addr_space == NULL) {
        s->addr_space = &address_space_memory;
    }
}

void greth_change_address_space(GRETHState *s, AddressSpace *addr_space, Error **errp)
{
    if (object_property_get_bool(OBJECT(s), "realized", errp)) {
        error_setg(errp, "Can't change address_space of realized device\n");
    }

    s->addr_space = addr_space;
}

static Property greth_properties[] = {
    DEFINE_NIC_PROPERTIES(GRETHState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void greth_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->desc = "Aeroflex Gaisler GRETH Controller";
    dc->realize = greth_realize;
    dc->reset = greth_reset;
    device_class_set_props(dc, greth_properties);
}

static const TypeInfo greth_info = {
    .name = TYPE_GRETH,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GRETHState),
    .class_init = greth_class_init,
};

static void greth_register_type(void)
{
    type_register_static(&greth_info);
}

type_init(greth_register_type)
