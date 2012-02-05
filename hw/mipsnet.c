#include "hw.h"
#include "net.h"
#include "trace.h"
#include "sysbus.h"

/* MIPSnet register offsets */

#define MIPSNET_DEV_ID		0x00
#define MIPSNET_BUSY		0x08
#define MIPSNET_RX_DATA_COUNT	0x0c
#define MIPSNET_TX_DATA_COUNT	0x10
#define MIPSNET_INT_CTL		0x14
# define MIPSNET_INTCTL_TXDONE		0x00000001
# define MIPSNET_INTCTL_RXDONE		0x00000002
# define MIPSNET_INTCTL_TESTBIT		0x80000000
#define MIPSNET_INTERRUPT_INFO	0x18
#define MIPSNET_RX_DATA_BUFFER	0x1c
#define MIPSNET_TX_DATA_BUFFER	0x20

#define MAX_ETH_FRAME_SIZE	1514

typedef struct MIPSnetState {
    SysBusDevice busdev;

    uint32_t busy;
    uint32_t rx_count;
    uint32_t rx_read;
    uint32_t tx_count;
    uint32_t tx_written;
    uint32_t intctl;
    uint8_t rx_buffer[MAX_ETH_FRAME_SIZE];
    uint8_t tx_buffer[MAX_ETH_FRAME_SIZE];
    MemoryRegion io;
    qemu_irq irq;
    NICState *nic;
    NICConf conf;
} MIPSnetState;

static void mipsnet_reset(MIPSnetState *s)
{
    s->busy = 1;
    s->rx_count = 0;
    s->rx_read = 0;
    s->tx_count = 0;
    s->tx_written = 0;
    s->intctl = 0;
    memset(s->rx_buffer, 0, MAX_ETH_FRAME_SIZE);
    memset(s->tx_buffer, 0, MAX_ETH_FRAME_SIZE);
}

static void mipsnet_update_irq(MIPSnetState *s)
{
    int isr = !!s->intctl;
    trace_mipsnet_irq(isr, s->intctl);
    qemu_set_irq(s->irq, isr);
}

static int mipsnet_buffer_full(MIPSnetState *s)
{
    if (s->rx_count >= MAX_ETH_FRAME_SIZE)
        return 1;
    return 0;
}

static int mipsnet_can_receive(VLANClientState *nc)
{
    MIPSnetState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    if (s->busy)
        return 0;
    return !mipsnet_buffer_full(s);
}

static ssize_t mipsnet_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    MIPSnetState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    trace_mipsnet_receive(size);
    if (!mipsnet_can_receive(nc))
        return -1;

    s->busy = 1;

    /* Just accept everything. */

    /* Write packet data. */
    memcpy(s->rx_buffer, buf, size);

    s->rx_count = size;
    s->rx_read = 0;

    /* Now we can signal we have received something. */
    s->intctl |= MIPSNET_INTCTL_RXDONE;
    mipsnet_update_irq(s);

    return size;
}

static uint64_t mipsnet_ioport_read(void *opaque, target_phys_addr_t addr,
                                    unsigned int size)
{
    MIPSnetState *s = opaque;
    int ret = 0;

    addr &= 0x3f;
    switch (addr) {
    case MIPSNET_DEV_ID:
	ret = be32_to_cpu(0x4d495053);		/* MIPS */
        break;
    case MIPSNET_DEV_ID + 4:
	ret = be32_to_cpu(0x4e455430);		/* NET0 */
        break;
    case MIPSNET_BUSY:
	ret = s->busy;
        break;
    case MIPSNET_RX_DATA_COUNT:
	ret = s->rx_count;
        break;
    case MIPSNET_TX_DATA_COUNT:
	ret = s->tx_count;
        break;
    case MIPSNET_INT_CTL:
	ret = s->intctl;
        s->intctl &= ~MIPSNET_INTCTL_TESTBIT;
        break;
    case MIPSNET_INTERRUPT_INFO:
        /* XXX: This seems to be a per-VPE interrupt number. */
	ret = 0;
        break;
    case MIPSNET_RX_DATA_BUFFER:
        if (s->rx_count) {
            s->rx_count--;
            ret = s->rx_buffer[s->rx_read++];
        }
        break;
    /* Reads as zero. */
    case MIPSNET_TX_DATA_BUFFER:
    default:
        break;
    }
    trace_mipsnet_read(addr, ret);
    return ret;
}

static void mipsnet_ioport_write(void *opaque, target_phys_addr_t addr,
                                 uint64_t val, unsigned int size)
{
    MIPSnetState *s = opaque;

    addr &= 0x3f;
    trace_mipsnet_write(addr, val);
    switch (addr) {
    case MIPSNET_TX_DATA_COUNT:
	s->tx_count = (val <= MAX_ETH_FRAME_SIZE) ? val : 0;
        s->tx_written = 0;
        break;
    case MIPSNET_INT_CTL:
        if (val & MIPSNET_INTCTL_TXDONE) {
            s->intctl &= ~MIPSNET_INTCTL_TXDONE;
        } else if (val & MIPSNET_INTCTL_RXDONE) {
            s->intctl &= ~MIPSNET_INTCTL_RXDONE;
        } else if (val & MIPSNET_INTCTL_TESTBIT) {
            mipsnet_reset(s);
            s->intctl |= MIPSNET_INTCTL_TESTBIT;
        } else if (!val) {
            /* ACK testbit interrupt, flag was cleared on read. */
        }
        s->busy = !!s->intctl;
        mipsnet_update_irq(s);
        break;
    case MIPSNET_TX_DATA_BUFFER:
        s->tx_buffer[s->tx_written++] = val;
        if (s->tx_written == s->tx_count) {
            /* Send buffer. */
            trace_mipsnet_send(s->tx_count);
            qemu_send_packet(&s->nic->nc, s->tx_buffer, s->tx_count);
            s->tx_count = s->tx_written = 0;
            s->intctl |= MIPSNET_INTCTL_TXDONE;
            s->busy = 1;
            mipsnet_update_irq(s);
        }
        break;
    /* Read-only registers */
    case MIPSNET_DEV_ID:
    case MIPSNET_BUSY:
    case MIPSNET_RX_DATA_COUNT:
    case MIPSNET_INTERRUPT_INFO:
    case MIPSNET_RX_DATA_BUFFER:
    default:
        break;
    }
}

static const VMStateDescription vmstate_mipsnet = {
    .name = "mipsnet",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(busy, MIPSnetState),
        VMSTATE_UINT32(rx_count, MIPSnetState),
        VMSTATE_UINT32(rx_read, MIPSnetState),
        VMSTATE_UINT32(tx_count, MIPSnetState),
        VMSTATE_UINT32(tx_written, MIPSnetState),
        VMSTATE_UINT32(intctl, MIPSnetState),
        VMSTATE_BUFFER(rx_buffer, MIPSnetState),
        VMSTATE_BUFFER(tx_buffer, MIPSnetState),
        VMSTATE_END_OF_LIST()
    }
};

static void mipsnet_cleanup(VLANClientState *nc)
{
    MIPSnetState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    s->nic = NULL;
}

static NetClientInfo net_mipsnet_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = mipsnet_can_receive,
    .receive = mipsnet_receive,
    .cleanup = mipsnet_cleanup,
};

static const MemoryRegionOps mipsnet_ioport_ops = {
    .read = mipsnet_ioport_read,
    .write = mipsnet_ioport_write,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static int mipsnet_sysbus_init(SysBusDevice *dev)
{
    MIPSnetState *s = DO_UPCAST(MIPSnetState, busdev, dev);

    memory_region_init_io(&s->io, &mipsnet_ioport_ops, s, "mipsnet-io", 36);
    sysbus_init_mmio(dev, &s->io);
    sysbus_init_irq(dev, &s->irq);

    s->nic = qemu_new_nic(&net_mipsnet_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->qdev.id, s);
    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);

    return 0;
}

static void mipsnet_sysbus_reset(DeviceState *dev)
{
    MIPSnetState *s = DO_UPCAST(MIPSnetState, busdev.qdev, dev);
    mipsnet_reset(s);
}

static Property mipsnet_properties[] = {
    DEFINE_NIC_PROPERTIES(MIPSnetState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void mipsnet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = mipsnet_sysbus_init;
    dc->desc = "MIPS Simulator network device";
    dc->reset = mipsnet_sysbus_reset;
    dc->vmsd = &vmstate_mipsnet;
    dc->props = mipsnet_properties;
}

static TypeInfo mipsnet_info = {
    .name          = "mipsnet",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSnetState),
    .class_init    = mipsnet_class_init,
};

static void mipsnet_register_types(void)
{
    type_register_static(&mipsnet_info);
}

type_init(mipsnet_register_types)
