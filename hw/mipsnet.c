#include "hw.h"
#include "mips.h"
#include "net.h"
#include "isa.h"

//#define DEBUG_MIPSNET_SEND
//#define DEBUG_MIPSNET_RECEIVE
//#define DEBUG_MIPSNET_DATA
//#define DEBUG_MIPSNET_IRQ

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
    uint32_t busy;
    uint32_t rx_count;
    uint32_t rx_read;
    uint32_t tx_count;
    uint32_t tx_written;
    uint32_t intctl;
    uint8_t rx_buffer[MAX_ETH_FRAME_SIZE];
    uint8_t tx_buffer[MAX_ETH_FRAME_SIZE];
    int io_base;
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
#ifdef DEBUG_MIPSNET_IRQ
    printf("mipsnet: Set IRQ to %d (%02x)\n", isr, s->intctl);
#endif
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

#ifdef DEBUG_MIPSNET_RECEIVE
    printf("mipsnet: receiving len=%zu\n", size);
#endif
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

static uint32_t mipsnet_ioport_read(void *opaque, uint32_t addr)
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
#ifdef DEBUG_MIPSNET_DATA
    printf("mipsnet: read addr=0x%02x val=0x%02x\n", addr, ret);
#endif
    return ret;
}

static void mipsnet_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    MIPSnetState *s = opaque;

    addr &= 0x3f;
#ifdef DEBUG_MIPSNET_DATA
    printf("mipsnet: write addr=0x%02x val=0x%02x\n", addr, val);
#endif
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
#ifdef DEBUG_MIPSNET_SEND
            printf("mipsnet: sending len=%d\n", s->tx_count);
#endif
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

static void mipsnet_save(QEMUFile *f, void *opaque)
{
    MIPSnetState *s = opaque;

    qemu_put_be32s(f, &s->busy);
    qemu_put_be32s(f, &s->rx_count);
    qemu_put_be32s(f, &s->rx_read);
    qemu_put_be32s(f, &s->tx_count);
    qemu_put_be32s(f, &s->tx_written);
    qemu_put_be32s(f, &s->intctl);
    qemu_put_buffer(f, s->rx_buffer, MAX_ETH_FRAME_SIZE);
    qemu_put_buffer(f, s->tx_buffer, MAX_ETH_FRAME_SIZE);
}

static int mipsnet_load(QEMUFile *f, void *opaque, int version_id)
{
    MIPSnetState *s = opaque;

    if (version_id > 0)
        return -EINVAL;

    qemu_get_be32s(f, &s->busy);
    qemu_get_be32s(f, &s->rx_count);
    qemu_get_be32s(f, &s->rx_read);
    qemu_get_be32s(f, &s->tx_count);
    qemu_get_be32s(f, &s->tx_written);
    qemu_get_be32s(f, &s->intctl);
    qemu_get_buffer(f, s->rx_buffer, MAX_ETH_FRAME_SIZE);
    qemu_get_buffer(f, s->tx_buffer, MAX_ETH_FRAME_SIZE);

    return 0;
}

static void mipsnet_cleanup(VLANClientState *nc)
{
    MIPSnetState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    unregister_savevm(NULL, "mipsnet", s);

    isa_unassign_ioport(s->io_base, 36);

    qemu_free(s);
}

static NetClientInfo net_mipsnet_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = mipsnet_can_receive,
    .receive = mipsnet_receive,
    .cleanup = mipsnet_cleanup,
};

void mipsnet_init (int base, qemu_irq irq, NICInfo *nd)
{
    MIPSnetState *s;

    qemu_check_nic_model(nd, "mipsnet");

    s = qemu_mallocz(sizeof(MIPSnetState));

    register_ioport_write(base, 36, 1, mipsnet_ioport_write, s);
    register_ioport_read(base, 36, 1, mipsnet_ioport_read, s);
    register_ioport_write(base, 36, 2, mipsnet_ioport_write, s);
    register_ioport_read(base, 36, 2, mipsnet_ioport_read, s);
    register_ioport_write(base, 36, 4, mipsnet_ioport_write, s);
    register_ioport_read(base, 36, 4, mipsnet_ioport_read, s);

    s->io_base = base;
    s->irq = irq;

    if (nd) {
        memcpy(s->conf.macaddr.a, nd->macaddr, sizeof(nd->macaddr));
        s->conf.vlan = nd->vlan;
        s->conf.peer = nd->netdev;

        s->nic = qemu_new_nic(&net_mipsnet_info, &s->conf,
                              nd->model, nd->name, s);

        qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);
    }

    mipsnet_reset(s);
    register_savevm(NULL, "mipsnet", 0, 0, mipsnet_save, mipsnet_load, s);
}
