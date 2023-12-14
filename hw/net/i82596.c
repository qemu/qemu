/*
 * QEMU Intel i82596 (Apricot) emulation
 *
 * Copyright (c) 2019 Helge Deller <deller@gmx.de>
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * This software was written to be compatible with the specification:
 * https://www.intel.com/assets/pdf/general/82596ca.pdf
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "net/net.h"
#include "net/eth.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "qemu/module.h"
#include "trace.h"
#include "i82596.h"
#include <zlib.h>       /* For crc32 */

#if defined(ENABLE_DEBUG)
#define DBG(x)          x
#else
#define DBG(x)          do { } while (0)
#endif

#define USE_TIMER       0

#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

#define PKT_BUF_SZ      1536
#define MAX_MC_CNT      64

#define ISCP_BUSY       0x0001

#define I596_NULL       ((uint32_t)0xffffffff)

#define SCB_STATUS_CX   0x8000 /* CU finished command with I bit */
#define SCB_STATUS_FR   0x4000 /* RU finished receiving a frame */
#define SCB_STATUS_CNA  0x2000 /* CU left active state */
#define SCB_STATUS_RNR  0x1000 /* RU left active state */

#define SCB_COMMAND_ACK_MASK \
        (SCB_STATUS_CX | SCB_STATUS_FR | SCB_STATUS_CNA | SCB_STATUS_RNR)

#define CU_IDLE         0
#define CU_SUSPENDED    1
#define CU_ACTIVE       2

#define RX_IDLE         0
#define RX_SUSPENDED    1
#define RX_READY        4

#define CMD_EOL         0x8000  /* The last command of the list, stop. */
#define CMD_SUSP        0x4000  /* Suspend after doing cmd. */
#define CMD_INTR        0x2000  /* Interrupt after doing cmd. */

#define CMD_FLEX        0x0008  /* Enable flexible memory model */

enum commands {
        CmdNOp = 0, CmdSASetup = 1, CmdConfigure = 2, CmdMulticastList = 3,
        CmdTx = 4, CmdTDR = 5, CmdDump = 6, CmdDiagnose = 7
};

#define STAT_C          0x8000  /* Set to 0 after execution */
#define STAT_B          0x4000  /* Command being executed */
#define STAT_OK         0x2000  /* Command executed ok */
#define STAT_A          0x1000  /* Command aborted */

#define I596_EOF        0x8000
#define SIZE_MASK       0x3fff

/* various flags in the chip config registers */
#define I596_PREFETCH   (s->config[0] & 0x80)
#define I596_PROMISC    (s->config[8] & 0x01)
#define I596_BC_DISABLE (s->config[8] & 0x02) /* broadcast disable */
#define I596_NOCRC_INS  (s->config[8] & 0x08)
#define I596_CRCINM     (s->config[11] & 0x04) /* CRC appended */
#define I596_MC_ALL     (s->config[11] & 0x20)
#define I596_MULTIIA    (s->config[13] & 0x40)


static uint8_t get_byte(uint32_t addr)
{
    return ldub_phys(&address_space_memory, addr);
}

static void set_byte(uint32_t addr, uint8_t c)
{
    return stb_phys(&address_space_memory, addr, c);
}

static uint16_t get_uint16(uint32_t addr)
{
    return lduw_be_phys(&address_space_memory, addr);
}

static void set_uint16(uint32_t addr, uint16_t w)
{
    return stw_be_phys(&address_space_memory, addr, w);
}

static uint32_t get_uint32(uint32_t addr)
{
    uint32_t lo = lduw_be_phys(&address_space_memory, addr);
    uint32_t hi = lduw_be_phys(&address_space_memory, addr + 2);
    return (hi << 16) | lo;
}

static void set_uint32(uint32_t addr, uint32_t val)
{
    set_uint16(addr, (uint16_t) val);
    set_uint16(addr + 2, val >> 16);
}


struct qemu_ether_header {
    uint8_t ether_dhost[6];
    uint8_t ether_shost[6];
    uint16_t ether_type;
};

#define PRINT_PKTHDR(txt, BUF) do {                  \
    struct qemu_ether_header *hdr = (void *)(BUF); \
    printf(txt ": packet dhost=" MAC_FMT ", shost=" MAC_FMT ", type=0x%04x\n",\
           MAC_ARG(hdr->ether_dhost), MAC_ARG(hdr->ether_shost),        \
           be16_to_cpu(hdr->ether_type));       \
} while (0)

static void i82596_transmit(I82596State *s, uint32_t addr)
{
    uint32_t tdb_p; /* Transmit Buffer Descriptor */

    /* TODO: Check flexible mode */
    tdb_p = get_uint32(addr + 8);
    while (tdb_p != I596_NULL) {
        uint16_t size, len;
        uint32_t tba;

        size = get_uint16(tdb_p);
        len = size & SIZE_MASK;
        tba = get_uint32(tdb_p + 8);
        trace_i82596_transmit(len, tba);

        if (s->nic && len) {
            assert(len <= sizeof(s->tx_buffer));
            address_space_read(&address_space_memory, tba,
                               MEMTXATTRS_UNSPECIFIED, s->tx_buffer, len);
            DBG(PRINT_PKTHDR("Send", &s->tx_buffer));
            DBG(printf("Sending %d bytes\n", len));
            qemu_send_packet(qemu_get_queue(s->nic), s->tx_buffer, len);
        }

        /* was this the last package? */
        if (size & I596_EOF) {
            break;
        }

        /* get next buffer pointer */
        tdb_p = get_uint32(tdb_p + 4);
    }
}

static void set_individual_address(I82596State *s, uint32_t addr)
{
    NetClientState *nc;
    uint8_t *m;

    nc = qemu_get_queue(s->nic);
    m = s->conf.macaddr.a;
    address_space_read(&address_space_memory, addr + 8,
                       MEMTXATTRS_UNSPECIFIED, m, ETH_ALEN);
    qemu_format_nic_info_str(nc, m);
    trace_i82596_new_mac(nc->info_str);
}

static void set_multicast_list(I82596State *s, uint32_t addr)
{
    uint16_t mc_count, i;

    memset(&s->mult[0], 0, sizeof(s->mult));
    mc_count = get_uint16(addr + 8) / ETH_ALEN;
    addr += 10;
    if (mc_count > MAX_MC_CNT) {
        mc_count = MAX_MC_CNT;
    }
    for (i = 0; i < mc_count; i++) {
        uint8_t multicast_addr[ETH_ALEN];
        address_space_read(&address_space_memory, addr + i * ETH_ALEN,
                           MEMTXATTRS_UNSPECIFIED, multicast_addr, ETH_ALEN);
        DBG(printf("Add multicast entry " MAC_FMT "\n",
                    MAC_ARG(multicast_addr)));
        unsigned mcast_idx = (net_crc32(multicast_addr, ETH_ALEN) &
                              BITS(7, 2)) >> 2;
        assert(mcast_idx < 8 * sizeof(s->mult));
        s->mult[mcast_idx >> 3] |= (1 << (mcast_idx & 7));
    }
    trace_i82596_set_multicast(mc_count);
}

void i82596_set_link_status(NetClientState *nc)
{
    I82596State *d = qemu_get_nic_opaque(nc);

    d->lnkst = nc->link_down ? 0 : 0x8000;
}

static void update_scb_status(I82596State *s)
{
    s->scb_status = (s->scb_status & 0xf000)
        | (s->cu_status << 8) | (s->rx_status << 4);
    set_uint16(s->scb, s->scb_status);
}


static void i82596_s_reset(I82596State *s)
{
    trace_i82596_s_reset(s);
    s->scp = 0;
    s->scb_status = 0;
    s->cu_status = CU_IDLE;
    s->rx_status = RX_SUSPENDED;
    s->cmd_p = I596_NULL;
    s->lnkst = 0x8000; /* initial link state: up */
    s->ca = s->ca_active = 0;
    s->send_irq = 0;
}


static void command_loop(I82596State *s)
{
    uint16_t cmd;
    uint16_t status;
    uint8_t byte_cnt;

    DBG(printf("STARTING COMMAND LOOP cmd_p=%08x\n", s->cmd_p));

    while (s->cmd_p != I596_NULL) {
        /* set status */
        status = STAT_B;
        set_uint16(s->cmd_p, status);
        status = STAT_C | STAT_OK; /* update, but write later */

        cmd = get_uint16(s->cmd_p + 2);
        DBG(printf("Running command %04x at %08x\n", cmd, s->cmd_p));

        switch (cmd & 0x07) {
        case CmdNOp:
            break;
        case CmdSASetup:
            set_individual_address(s, s->cmd_p);
            break;
        case CmdConfigure:
            byte_cnt = get_byte(s->cmd_p + 8) & 0x0f;
            byte_cnt = MAX(byte_cnt, 4);
            byte_cnt = MIN(byte_cnt, sizeof(s->config));
            /* copy byte_cnt max. */
            address_space_read(&address_space_memory, s->cmd_p + 8,
                               MEMTXATTRS_UNSPECIFIED, s->config, byte_cnt);
            /* config byte according to page 35ff */
            s->config[2] &= 0x82; /* mask valid bits */
            s->config[2] |= 0x40;
            s->config[7]  &= 0xf7; /* clear zero bit */
            assert(I596_NOCRC_INS == 0); /* do CRC insertion */
            s->config[10] = MAX(s->config[10], 5); /* min frame length */
            s->config[12] &= 0x40; /* only full duplex field valid */
            s->config[13] |= 0x3f; /* set ones in byte 13 */
            break;
        case CmdTDR:
            /* get signal LINK */
            set_uint32(s->cmd_p + 8, s->lnkst);
            break;
        case CmdTx:
            i82596_transmit(s, s->cmd_p);
            break;
        case CmdMulticastList:
            set_multicast_list(s, s->cmd_p);
            break;
        case CmdDump:
        case CmdDiagnose:
            printf("FIXME Command %d !!\n", cmd & 7);
            assert(0);
        }

        /* update status */
        set_uint16(s->cmd_p, status);

        s->cmd_p = get_uint32(s->cmd_p + 4); /* get link address */
        DBG(printf("NEXT addr would be %08x\n", s->cmd_p));
        if (s->cmd_p == 0) {
            s->cmd_p = I596_NULL;
        }

        /* Stop when last command of the list. */
        if (cmd & CMD_EOL) {
            s->cmd_p = I596_NULL;
        }
        /* Suspend after doing cmd? */
        if (cmd & CMD_SUSP) {
            s->cu_status = CU_SUSPENDED;
            printf("FIXME SUSPEND !!\n");
        }
        /* Interrupt after doing cmd? */
        if (cmd & CMD_INTR) {
            s->scb_status |= SCB_STATUS_CX;
        } else {
            s->scb_status &= ~SCB_STATUS_CX;
        }
        update_scb_status(s);

        /* Interrupt after doing cmd? */
        if (cmd & CMD_INTR) {
            s->send_irq = 1;
        }

        if (s->cu_status != CU_ACTIVE) {
            break;
        }
    }
    DBG(printf("FINISHED COMMAND LOOP\n"));
    qemu_flush_queued_packets(qemu_get_queue(s->nic));
}

static void i82596_flush_queue_timer(void *opaque)
{
    I82596State *s = opaque;
    if (0) {
        timer_del(s->flush_queue_timer);
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
        timer_mod(s->flush_queue_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000);
    }
}

static void examine_scb(I82596State *s)
{
    uint16_t command, cuc, ruc;

    /* get the scb command word */
    command = get_uint16(s->scb + 2);
    cuc = (command >> 8) & 0x7;
    ruc = (command >> 4) & 0x7;
    DBG(printf("MAIN COMMAND %04x  cuc %02x ruc %02x\n", command, cuc, ruc));
    /* and clear the scb command word */
    set_uint16(s->scb + 2, 0);

    s->scb_status &= ~(command & SCB_COMMAND_ACK_MASK);

    switch (cuc) {
    case 0:     /* no change */
        break;
    case 1:     /* CUC_START */
        s->cu_status = CU_ACTIVE;
        break;
    case 4:     /* CUC_ABORT */
        s->cu_status = CU_SUSPENDED;
        s->scb_status |= SCB_STATUS_CNA; /* CU left active state */
        break;
    default:
        printf("WARNING: Unknown CUC %d!\n", cuc);
    }

    switch (ruc) {
    case 0:     /* no change */
        break;
    case 1:     /* RX_START */
    case 2:     /* RX_RESUME */
        s->rx_status = RX_IDLE;
        if (USE_TIMER) {
            timer_mod(s->flush_queue_timer, qemu_clock_get_ms(
                                QEMU_CLOCK_VIRTUAL) + 1000);
        }
        break;
    case 3:     /* RX_SUSPEND */
    case 4:     /* RX_ABORT */
        s->rx_status = RX_SUSPENDED;
        s->scb_status |= SCB_STATUS_RNR; /* RU left active state */
        break;
    default:
        printf("WARNING: Unknown RUC %d!\n", ruc);
    }

    if (command & 0x80) { /* reset bit set? */
        i82596_s_reset(s);
    }

    /* execute commands from SCBL */
    if (s->cu_status != CU_SUSPENDED) {
        if (s->cmd_p == I596_NULL) {
            s->cmd_p = get_uint32(s->scb + 4);
        }
    }

    /* update scb status */
    update_scb_status(s);

    command_loop(s);
}

static void signal_ca(I82596State *s)
{
    uint32_t iscp = 0;

    /* trace_i82596_channel_attention(s); */
    if (s->scp) {
        /* CA after reset -> do init with new scp. */
        s->sysbus = get_byte(s->scp + 3); /* big endian */
        DBG(printf("SYSBUS = %08x\n", s->sysbus));
        if (((s->sysbus >> 1) & 0x03) != 2) {
            printf("WARNING: NO LINEAR MODE !!\n");
        }
        if ((s->sysbus >> 7)) {
            printf("WARNING: 32BIT LINMODE IN B-STEPPING NOT SUPPORTED !!\n");
        }
        iscp = get_uint32(s->scp + 8);
        s->scb = get_uint32(iscp + 4);
        set_byte(iscp + 1, 0); /* clear BUSY flag in iscp */
        s->scp = 0;
    }

    s->ca++;    /* count ca() */
    if (!s->ca_active) {
        s->ca_active = 1;
        while (s->ca)   {
            examine_scb(s);
            s->ca--;
        }
        s->ca_active = 0;
    }

    if (s->send_irq) {
        s->send_irq = 0;
        qemu_set_irq(s->irq, 1);
    }
}

void i82596_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    I82596State *s = opaque;
    /* printf("i82596_ioport_writew addr=0x%08x val=0x%04x\n", addr, val); */
    switch (addr) {
    case PORT_RESET: /* Reset */
        i82596_s_reset(s);
        break;
    case PORT_ALTSCP:
        s->scp = val;
        break;
    case PORT_CA:
        signal_ca(s);
        break;
    }
}

uint32_t i82596_ioport_readw(void *opaque, uint32_t addr)
{
    return -1;
}

void i82596_h_reset(void *opaque)
{
    I82596State *s = opaque;

    i82596_s_reset(s);
}

bool i82596_can_receive(NetClientState *nc)
{
    I82596State *s = qemu_get_nic_opaque(nc);

    if (s->rx_status == RX_SUSPENDED) {
        return false;
    }

    if (!s->lnkst) {
        return false;
    }

    if (USE_TIMER && !timer_pending(s->flush_queue_timer)) {
        return true;
    }

    return true;
}

ssize_t i82596_receive(NetClientState *nc, const uint8_t *buf, size_t sz)
{
    I82596State *s = qemu_get_nic_opaque(nc);
    uint32_t rfd_p;
    uint32_t rbd;
    uint16_t is_broadcast = 0;
    size_t len = sz; /* length of data for guest (including CRC) */
    size_t bufsz = sz; /* length of data in buf */
    uint32_t crc;
    uint8_t *crc_ptr;
    static const uint8_t broadcast_macaddr[6] = {
                0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    DBG(printf("i82596_receive() start\n"));

    if (USE_TIMER && timer_pending(s->flush_queue_timer)) {
        return 0;
    }

    /* first check if receiver is enabled */
    if (s->rx_status == RX_SUSPENDED) {
        trace_i82596_receive_analysis(">>> Receiving suspended");
        return -1;
    }

    if (!s->lnkst) {
        trace_i82596_receive_analysis(">>> Link down");
        return -1;
    }

    /* Received frame smaller than configured "min frame len"? */
    if (sz < s->config[10]) {
        printf("Received frame too small, %zu vs. %u bytes\n",
               sz, s->config[10]);
        return -1;
    }

    DBG(printf("Received %lu bytes\n", sz));

    if (I596_PROMISC) {

        /* promiscuous: receive all */
        trace_i82596_receive_analysis(
                ">>> packet received in promiscuous mode");

    } else {

        if (!memcmp(buf,  broadcast_macaddr, 6)) {
            /* broadcast address */
            if (I596_BC_DISABLE) {
                trace_i82596_receive_analysis(">>> broadcast packet rejected");

                return len;
            }

            trace_i82596_receive_analysis(">>> broadcast packet received");
            is_broadcast = 1;

        } else if (buf[0] & 0x01) {
            /* multicast */
            if (!I596_MC_ALL) {
                trace_i82596_receive_analysis(">>> multicast packet rejected");

                return len;
            }

            int mcast_idx = (net_crc32(buf, ETH_ALEN) & BITS(7, 2)) >> 2;
            assert(mcast_idx < 8 * sizeof(s->mult));

            if (!(s->mult[mcast_idx >> 3] & (1 << (mcast_idx & 7)))) {
                trace_i82596_receive_analysis(">>> multicast address mismatch");

                return len;
            }

            trace_i82596_receive_analysis(">>> multicast packet received");
            is_broadcast = 1;

        } else if (!memcmp(s->conf.macaddr.a, buf, 6)) {

            /* match */
            trace_i82596_receive_analysis(
                    ">>> physical address matching packet received");

        } else {

            trace_i82596_receive_analysis(">>> unknown packet");

            return len;
        }
    }

    /* Calculate the ethernet checksum (4 bytes) */
    len += 4;
    crc = cpu_to_be32(crc32(~0, buf, sz));
    crc_ptr = (uint8_t *) &crc;

    rfd_p = get_uint32(s->scb + 8); /* get Receive Frame Descriptor */
    assert(rfd_p && rfd_p != I596_NULL);

    /* get first Receive Buffer Descriptor Address */
    rbd = get_uint32(rfd_p + 8);
    assert(rbd && rbd != I596_NULL);

    trace_i82596_receive_packet(len);
    /* PRINT_PKTHDR("Receive", buf); */

    while (len) {
        uint16_t command, status;
        uint32_t next_rfd;

        command = get_uint16(rfd_p + 2);
        assert(command & CMD_FLEX); /* assert Flex Mode */
        /* get first Receive Buffer Descriptor Address */
        rbd = get_uint32(rfd_p + 8);
        assert(get_uint16(rfd_p + 14) == 0);

        /* printf("Receive: rfd is %08x\n", rfd_p); */

        while (len) {
            uint16_t buffer_size, num;
            uint32_t rba;
            size_t bufcount, crccount;

            /* printf("Receive: rbd is %08x\n", rbd); */
            buffer_size = get_uint16(rbd + 12);
            /* printf("buffer_size is 0x%x\n", buffer_size); */
            assert(buffer_size != 0);

            num = buffer_size & SIZE_MASK;
            if (num > len) {
                num = len;
            }
            rba = get_uint32(rbd + 8);
            /* printf("rba is 0x%x\n", rba); */
            /*
             * Calculate how many bytes we want from buf[] and how many
             * from the CRC.
             */
            if ((len - num) >= 4) {
                /* The whole guest buffer, we haven't hit the CRC yet */
                bufcount = num;
            } else {
                /* All that's left of buf[] */
                bufcount = len - 4;
            }
            crccount = num - bufcount;

            if (bufcount > 0) {
                /* Still some of the actual data buffer to transfer */
                assert(bufsz >= bufcount);
                bufsz -= bufcount;
                address_space_write(&address_space_memory, rba,
                                    MEMTXATTRS_UNSPECIFIED, buf, bufcount);
                rba += bufcount;
                buf += bufcount;
                len -= bufcount;
            }

            /* Write as much of the CRC as fits */
            if (crccount > 0) {
                address_space_write(&address_space_memory, rba,
                                    MEMTXATTRS_UNSPECIFIED, crc_ptr, crccount);
                rba += crccount;
                crc_ptr += crccount;
                len -= crccount;
            }

            num |= 0x4000; /* set F BIT */
            if (len == 0) {
                num |= I596_EOF; /* set EOF BIT */
            }
            set_uint16(rbd + 0, num); /* write actual count with flags */

            /* get next rbd */
            rbd = get_uint32(rbd + 4);
            /* printf("Next Receive: rbd is %08x\n", rbd); */

            if (buffer_size & I596_EOF) /* last entry */
                break;
        }

        /* Housekeeping, see pg. 18 */
        next_rfd = get_uint32(rfd_p + 4);
        set_uint32(next_rfd + 8, rbd);

        status = STAT_C | STAT_OK | is_broadcast;
        set_uint16(rfd_p, status);

        if (command & CMD_SUSP) {  /* suspend after command? */
            s->rx_status = RX_SUSPENDED;
            s->scb_status |= SCB_STATUS_RNR; /* RU left active state */
            break;
        }
        if (command & CMD_EOL) /* was it last Frame Descriptor? */
            break;

        assert(len == 0);
    }

    assert(len == 0);

    s->scb_status |= SCB_STATUS_FR; /* set "RU finished receiving frame" bit. */
    update_scb_status(s);

    /* send IRQ that we received data */
    qemu_set_irq(s->irq, 1);
    /* s->send_irq = 1; */

    if (0) {
        DBG(printf("Checking:\n"));
        rfd_p = get_uint32(s->scb + 8); /* get Receive Frame Descriptor */
        DBG(printf("Next Receive: rfd is %08x\n", rfd_p));
        rfd_p = get_uint32(rfd_p + 4); /* get Next Receive Frame Descriptor */
        DBG(printf("Next Receive: rfd is %08x\n", rfd_p));
        /* get first Receive Buffer Descriptor Address */
        rbd = get_uint32(rfd_p + 8);
        DBG(printf("Next Receive: rbd is %08x\n", rbd));
    }

    return sz;
}


const VMStateDescription vmstate_i82596 = {
    .name = "i82596",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(lnkst, I82596State),
        VMSTATE_TIMER_PTR(flush_queue_timer, I82596State),
        VMSTATE_END_OF_LIST()
    }
};

void i82596_common_init(DeviceState *dev, I82596State *s, NetClientInfo *info)
{
    if (s->conf.macaddr.a[0] == 0) {
        qemu_macaddr_default_if_unset(&s->conf.macaddr);
    }
    s->nic = qemu_new_nic(info, &s->conf, object_get_typename(OBJECT(dev)),
                dev->id, &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    if (USE_TIMER) {
        s->flush_queue_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    i82596_flush_queue_timer, s);
    }
    s->lnkst = 0x8000; /* initial link state: up */
}
