/*
 * QEMU Intel i82596 (Apricot) emulation
 *
 * Copyright (c) 2019 Helge Deller <deller@gmx.de>
 *
 * Additional functionality added by:
 * Soumyajyotii Ssarkar <soumyajyotisarkar23@gmail.com>
 * During GSOC 2025 under mentorship of Helge Deller.
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 * This software was written to be compatible with the specification:
 * https://parisc.docs.kernel.org/en/latest/_downloads/96672be0650d9fc046bbcea40b92482f/82596CA.pdf
 *
 * INDEX:
 * 1.  Reset
 * 2.  Address Translation
 * 3.  Transmit functions
 * 4.  Receive Helper functions
 * 5.  Receive functions
 * 6.  Misc Functionality Functions
 * 6.1 Individual Address
 * 6.2 Multicast Address List
 * 6.3 Link Status
 * 6.4 CSMA/CD functions
 * 6.5 Unified CRC Calculation
 * 6.6 Unified Statistics Update
 * 7.  Bus Throttling Timer
 * 8.  Dump functions
 * 9.  Configure
 * 10. Command Loop
 * 11. Examine SCB
 * 12. Channel attention (CA)
 * 13. LASI interface
 * 14. Polling functions
 * 15. QOM and interface functions
 *
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "net/net.h"
#include "net/eth.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "qemu/module.h"
#include "trace.h"
#include "i82596.h"
#include <zlib.h> /* for crc32 */

#define ENABLE_DEBUG 0

#if defined(ENABLE_DEBUG)
#define DBG(x)          x
#else
#define DBG(x)          do { } while (0)
#endif

#define USE_TIMER       1

#define MAX_MC_CNT      64
#define I596_NULL       ((uint32_t)0xffffffff)
#define BITS(n, m)      (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

#define SCB_STATUS_CX   0x8000  /* CU finished command with I bit */
#define SCB_STATUS_FR   0x4000  /* RU finished receiving a frame */
#define SCB_STATUS_CNA  0x2000  /* CU left active state */
#define SCB_STATUS_RNR  0x1000  /* RU left active state */
#define SCB_ACK_MASK    0xF000  /* All interrupt acknowledge bits */

/* 82596 Operational Modes */
#define I82586_MODE                 0x00
#define I82596_MODE_SEGMENTED       0x01
#define I82596_MODE_LINEAR          0x02

/* Monitor Options */
#define MONITOR_NORMAL      0x00
#define MONITOR_FILTERED    0x01
#define MONITOR_ALL         0x02
#define MONITOR_DISABLED    0x03

/* Operation mode flags from SYSBUS byte */
#define SYSBUS_LOCK_EN         0x08
#define SYSBUS_INT_ACTIVE_LOW  0x10
#define SYSBUS_BIG_ENDIAN_32   0x80
#define SYSBUS_THROTTLE_MASK   0x60

/* SCB commands - Command Unit (CU) */
#define SCB_CUC_NOP            0x00
#define SCB_CUC_START          0x01
#define SCB_CUC_RESUME         0x02
#define SCB_CUC_SUSPEND        0x03
#define SCB_CUC_ABORT          0x04
#define SCB_CUC_LOAD_THROTTLE  0x05
#define SCB_CUC_LOAD_START     0x06

/* SCB commands - Receive Unit (RU) */
#define SCB_RUC_NOP            0x00
#define SCB_RUC_START          0x01
#define SCB_RUC_RESUME         0x02
#define SCB_RUC_SUSPEND        0x03
#define SCB_RUC_ABORT          0x04

/* SCB statuses - Command Unit (CU) */
#define CU_IDLE         0
#define CU_SUSPENDED    1
#define CU_ACTIVE       2

/* SCB statuses - Receive Unit (RU) */
#define RX_IDLE         0x00
#define RX_SUSPENDED    0x01
#define RX_NO_RESOURCES 0x02
#define RX_READY        0x04
#define RX_NO_RESO_RBD  0x0A
#define RX_NO_MORE_RBD  0x0C

#define CMD_FLEX        0x0008
#define CMD_MASK        0x0007

#define CMD_EOL         0x8000
#define CMD_SUSP        0x4000
#define CMD_INTR        0x2000

#define ISCP_BUSY                   0x01
#define NANOSECONDS_PER_MICROSECOND 1000

#define DUMP_BUF_SZ                 304

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

#define CSMA_SLOT_TIME         51
#define CSMA_MAX_RETRIES       16
#define CSMA_BACKOFF_LIMIT     10

/* Global Flags fetched from config bytes */
#define I596_PREFETCH       (s->config[0] & 0x80)
#define SAVE_BAD_FRAMES     (s->config[2] & 0x80)
#define I596_NO_SRC_ADD_IN  (s->config[3] & 0x08)
#define I596_LOOPBACK       (s->config[3] >> 6)
#define I596_PROMISC        (s->config[8] & 0x01)
#define I596_BC_DISABLE     (s->config[8] & 0x02)
#define I596_NOCRC_INS      (s->config[8] & 0x08)
#define I596_CRC16_32       (s->config[8] & 0x10)
#define I596_PADDING        (s->config[8] & 0x80)
#define I596_MIN_FRAME_LEN  (s->config[10])
#define I596_CRCINM         (s->config[11] & 0x04)
#define I596_MONITOR_MODE   ((s->config[11] >> 6) & 0x03)
#define I596_MC_ALL         (s->config[11] & 0x20)
#define I596_FULL_DUPLEX    (s->config[12] & 0x40)
#define I596_MULTIIA        (s->config[13] & 0x40)

/* RX Error flags */
#define RX_COLLISIONS         0x0001
#define RX_LENGTH_ERRORS      0x0080
#define RX_OVER_ERRORS        0x0100
#define RX_FIFO_ERRORS        0x0400
#define RX_FRAME_ERRORS       0x0800
#define RX_CRC_ERRORS         0x1000
#define RX_LENGTH_ERRORS_ALT  0x2000
#define RFD_STATUS_TRUNC      0x0020
#define RFD_STATUS_NOBUFS     0x0200

/* TX Error flags */
#define TX_COLLISIONS       0x0020
#define TX_HEARTBEAT_ERRORS 0x0040
#define TX_CARRIER_ERRORS   0x0400
#define TX_COLLISIONS_ALT   0x0800
#define TX_ABORTED_ERRORS   0x1000

static void i82596_update_scb_irq(I82596State *s, bool trigger);
static void i82596_update_cu_status(I82596State *s, uint16_t cmd_status,
                                     bool generate_interrupt);
static void update_scb_status(I82596State *s);
static void examine_scb(I82596State *s);
static bool i82596_check_medium_status(I82596State *s);
static int i82596_csma_backoff(I82596State *s, int retry_count);
static uint16_t i82596_calculate_crc16(const uint8_t *data, size_t len);
static size_t i82596_append_crc(I82596State *s, uint8_t *buffer, size_t len);
static void i82596_bus_throttle_timer(void *opaque);
static void i82596_flush_queue_timer(void *opaque);
static int i82596_flush_packet_queue(I82596State *s);
static void i82596_update_statistics(I82596State *s, bool is_tx,
                                      uint16_t error_flags,
                                      uint16_t collision_count);

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

/* Centralized error detection and update mechanism */
static void i82596_record_error(I82596State *s, uint16_t error_type, bool is_tx)
{
    if (is_tx) {
        if (error_type & TX_ABORTED_ERRORS) {
            s->tx_aborted_errors++;
            set_uint32(s->scb + 28, s->tx_aborted_errors);
        }
    } else {
        if (error_type & RX_CRC_ERRORS) {
            s->crc_err++;
            set_uint32(s->scb + 16, s->crc_err);
        }

        if (error_type & (RX_LENGTH_ERRORS | RX_LENGTH_ERRORS_ALT |
                          RX_FRAME_ERRORS)) {
            s->align_err++;
            set_uint32(s->scb + 18, s->align_err);
        }

        if (error_type & RFD_STATUS_NOBUFS) {
            s->resource_err++;
            set_uint32(s->scb + 20, s->resource_err);
        }

        if (error_type & (RX_OVER_ERRORS | RX_FIFO_ERRORS)) {
            s->over_err++;
            set_uint32(s->scb + 22, s->over_err);
        }

        if (error_type & RFD_STATUS_TRUNC) {
            s->short_fr_error++;
            set_uint32(s->scb + 26, s->short_fr_error);
        }
    }
}

/* Packet Header Debugger */
struct qemu_ether_header {
    uint8_t ether_dhost[6];
    uint8_t ether_shost[6];
    uint16_t ether_type;
};

#define PRINT_PKTHDR(txt, BUF) do {                  \
} while (0)

static void i82596_cleanup(I82596State *s)
{
    if (s->throttle_timer) {
        timer_del(s->throttle_timer);
    }
    if (s->flush_queue_timer) {
        timer_del(s->flush_queue_timer);
    }
    s->queue_head = 0;
    s->queue_tail = 0;
    s->queue_count = 0;
}

static void i82596_s_reset(I82596State *s)
{
    trace_i82596_s_reset(s);
    i82596_cleanup(s);

    /* Clearing config bits */
    memset(s->config, 0, sizeof(s->config));
    s->scp = 0x00FFFFF4;
    s->scb = 0;
    s->scb_base = 0;
    s->scb_status = 0;
    s->cu_status = CU_IDLE;
    s->rx_status = RX_IDLE;
    s->cmd_p = I596_NULL;
    s->lnkst = 0x8000;
    s->ca = s->ca_active = 0;
    s->send_irq = 0;

    /* Statistical Counters */
    s->crc_err = 0;
    s->align_err = 0;
    s->resource_err = 0;
    s->over_err = 0;
    s->rcvdt_err = 0;
    s->short_fr_error = 0;
    s->total_frames = 0;
    s->total_good_frames = 0;
    s->collision_events = 0;
    s->total_collisions = 0;
    s->tx_good_frames = 0;
    s->tx_collisions = 0;
    s->tx_aborted_errors = 0;
    s->last_tx_len = 0;

    s->last_good_rfa = 0;
    s->current_rx_desc = 0;
    s->current_tx_desc = 0;
    s->tx_retry_addr = 0;
    s->tx_retry_count = 0;

    s->rnr_signaled = false;
    s->flushing_queue = false;

    memset(s->tx_buffer, 0, sizeof(s->tx_buffer));
    memset(s->rx_buffer, 0, sizeof(s->rx_buffer));
    s->tx_frame_len = 0;
    s->rx_frame_len = 0;
}

void i82596_h_reset(void *opaque)
{
    I82596State *s = opaque;

    i82596_s_reset(s);
}

/*
 * Address Translation Implementation
 * Handles segmented and linear memory modes for i82596.
 * Returns physical address for DMA operations.
 * Returns I596_NULL (0xffffffff) on invalid addresses.
 */
static inline uint32_t i82596_translate_address(I82596State *s,
                                                 uint32_t logical_addr,
                                                 bool is_data_buffer)
{
    if (logical_addr == I596_NULL || logical_addr == 0) {
        return logical_addr;
    }

    switch (s->mode) {
    case I82596_MODE_LINEAR:
        return logical_addr;

    case I82596_MODE_SEGMENTED: {
        uint32_t base = (logical_addr >> 16) & 0xFFFF;
        uint32_t offset = logical_addr & 0xFFFF;

        if (is_data_buffer) {
            return (base << 4) + offset;
        } else {
            if (base == 0xFFFF && offset == 0xFFFF) {
                return I596_NULL;
            }
            return s->scb_base + ((base << 4) + offset);
        }
    }

    case I82586_MODE:
    default:
        if (is_data_buffer) {
            return logical_addr;
        } else {
            if ((logical_addr & 0xFFFF0000) == 0xFFFF0000) {
                return I596_NULL;
            }
            return s->scb_base + logical_addr;
        }
    }
}

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

static void i82596_configure(I82596State *s, uint32_t addr)
{
    uint8_t byte_cnt;
    byte_cnt = get_byte(addr + 8) & 0x0f;

    byte_cnt = MAX(byte_cnt, 4);
    byte_cnt = MIN(byte_cnt, sizeof(s->config));
    /* copy byte_cnt max. */
    address_space_read(&address_space_memory, addr + 8,
                       MEMTXATTRS_UNSPECIFIED, s->config, byte_cnt);
    /* config byte according to page 35ff */
    s->config[2] &= 0x82; /* mask valid bits */
    s->config[2] |= 0x40;
    s->config[7]  &= 0xf7; /* clear zero bit */
    assert(I596_NOCRC_INS == 0); /* do CRC insertion */
    s->config[10] = MAX(s->config[10], 5); /* min frame length */
    s->config[12] &= 0x40; /* only full duplex field valid */
    s->config[13] |= 0x3f; /* set ones in byte 13 */
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
    I82596State *s = qemu_get_nic_opaque(nc);
    bool was_up = s->lnkst != 0;

    s->lnkst = nc->link_down ? 0 : 0x8000;
    bool is_up = s->lnkst != 0;

    if (!was_up && is_up && s->rx_status == RX_READY) {
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
    }
}

static bool G_GNUC_UNUSED i82596_check_medium_status(I82596State *s)
{
    if (I596_FULL_DUPLEX) {
        return true;
    }

    if (!s->throttle_state) {
        return false;
    }

    if (!I596_LOOPBACK && (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) % 100 < 5)) {
        s->collision_events++;
        return false;
    }

    return true;
}

static int G_GNUC_UNUSED i82596_csma_backoff(I82596State *s, int retry_count)
{
    int backoff_factor, slot_count, backoff_time;

    backoff_factor = MIN(retry_count + 1, CSMA_BACKOFF_LIMIT);
    slot_count = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) % (1 << backoff_factor);
    backoff_time = slot_count * CSMA_SLOT_TIME;

    return backoff_time;
}

static uint16_t i82596_calculate_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    size_t i, j;

    for (i = 0; i < len; i++) {
        crc ^= data[i] << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static size_t G_GNUC_UNUSED i82596_append_crc(I82596State *s, uint8_t *buffer, size_t len)
{
    if (len + 4 > PKT_BUF_SZ) {
        return len;
    }

    if (I596_CRC16_32) {
        uint32_t crc = crc32(~0, buffer, len);
        crc = cpu_to_be32(crc);
        memcpy(&buffer[len], &crc, sizeof(crc));
        return len + sizeof(crc);
    } else {
        uint16_t crc = i82596_calculate_crc16(buffer, len);
        crc = cpu_to_be16(crc);
        memcpy(&buffer[len], &crc, sizeof(crc));
        return len + sizeof(crc);
    }
}

static void G_GNUC_UNUSED i82596_update_statistics(I82596State *s, bool is_tx,
                                      uint16_t error_flags,
                                      uint16_t collision_count)
{
    if (is_tx) {
        if (collision_count > 0) {
            s->tx_collisions += collision_count;
            s->collision_events++;
            s->total_collisions += collision_count;
            set_uint32(s->scb + 32, s->tx_collisions);
        }
        if (error_flags) {
            i82596_record_error(s, error_flags, true);
        }
        if (!(error_flags & (TX_ABORTED_ERRORS | TX_CARRIER_ERRORS))) {
            s->tx_good_frames++;
            set_uint32(s->scb + 36, s->tx_good_frames);
        }
    } else {
        s->total_frames++;
        set_uint32(s->scb + 40, s->total_frames);
        if (error_flags) {
            i82596_record_error(s, error_flags, false);
        } else {
            s->total_good_frames++;
            set_uint32(s->scb + 44, s->total_good_frames);
        }
    }
}

/* Bus Throttle Functionality */
static void G_GNUC_UNUSED i82596_bus_throttle_timer(void *opaque)
{
    I82596State *s = opaque;

    if (s->throttle_state) {
        s->throttle_state = false;
        if (s->t_off > 0) {
            timer_mod(s->throttle_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      (s->t_off * NANOSECONDS_PER_MICROSECOND));
        }
    } else {
        s->throttle_state = true;
        if (s->t_on > 0) {
            timer_mod(s->throttle_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      (s->t_on * NANOSECONDS_PER_MICROSECOND));
        }

        if (s->cu_status == CU_ACTIVE || s->rx_status == RX_READY) {
            examine_scb(s);
        }
    }
}

static int G_GNUC_UNUSED i82596_flush_packet_queue(I82596State *s)
{
    /* Stub for now - will be implemented in Patch 2 */
    return 0;
}

static void G_GNUC_UNUSED i82596_flush_queue_timer(void *opaque)
{
    /* Stub for now - will be implemented in Patch 2 */
}

static void i82596_update_scb_irq(I82596State *s, bool trigger)
{
    if (trigger) {
        s->send_irq = 1;
        qemu_set_irq(s->irq, 1);
    }
}

static void G_GNUC_UNUSED i82596_update_cu_status(I82596State *s, uint16_t cmd_status,
                                     bool generate_interrupt)
{
    if (cmd_status & STAT_C) {
        if (cmd_status & STAT_OK) {
            if (s->cu_status == CU_ACTIVE && s->cmd_p == I596_NULL) {
                s->cu_status = CU_IDLE;
                s->scb_status |= SCB_STATUS_CNA;
            }
        } else {
            s->cu_status = CU_IDLE;
            s->scb_status |= SCB_STATUS_CNA;
        }

        if (generate_interrupt) {
            s->scb_status |= SCB_STATUS_CX;
            i82596_update_scb_irq(s, true);
        }
    }

    update_scb_status(s);
}

static void update_scb_status(I82596State *s)
{
    s->scb_status = (s->scb_status & 0xf000)
        | (s->cu_status << 8) | (s->rx_status << 4) | (s->lnkst >> 8);
    set_uint16(s->scb, s->scb_status);

    set_uint32(s->scb + 28, s->tx_aborted_errors);
    set_uint32(s->scb + 32, s->tx_collisions);
    set_uint32(s->scb + 36, s->tx_good_frames);

    set_uint32(s->scb + 16, s->crc_err);
    set_uint32(s->scb + 18, s->align_err);
    set_uint32(s->scb + 20, s->resource_err);
    set_uint32(s->scb + 22, s->over_err);
    set_uint32(s->scb + 24, s->rcvdt_err);
    set_uint32(s->scb + 26, s->short_fr_error);
}

static void command_loop(I82596State *s)
{
    uint16_t cmd;
    uint16_t status;

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
            i82596_configure(s, s->cmd_p);
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
            g_assert_not_reached();
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

    s->scb_status &= ~(command & SCB_ACK_MASK);

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

ssize_t i82596_receive_iov(NetClientState *nc, const struct iovec *iov,
                            int iovcnt)
{
    size_t sz = 0;
    uint8_t *buf;
    int i;
    for (i = 0; i < iovcnt; i++) {
        sz += iov[i].iov_len;
    }
    if (sz == 0) {
        return -1;
    }
    buf = g_malloc(sz);
    if (!buf) {
        return -1;
    }
    size_t offset = 0;
    for (i = 0; i < iovcnt; i++) {
        if (iov[i].iov_base == NULL) {
            g_free(buf);
            return -1;
        }
        memcpy(buf + offset, iov[i].iov_base, iov[i].iov_len);
        offset += iov[i].iov_len;
    }
    DBG(PRINT_PKTHDR("Receive IOV:", buf));
    i82596_receive(nc, buf, sz);
    g_free(buf);
    return sz;
}

void i82596_poll(NetClientState *nc, bool enable)
{
    I82596State *s = qemu_get_nic_opaque(nc);

    if (!enable) {
        return;
    }

    if (s->send_irq) {
        qemu_set_irq(s->irq, 1);
    }

    if (s->rx_status == RX_NO_RESOURCES) {
        if (s->cmd_p != I596_NULL) {
            s->rx_status = RX_READY;
            update_scb_status(s);
        }
    }

    if (s->cu_status == CU_ACTIVE && s->cmd_p != I596_NULL) {
        examine_scb(s);
    }
    qemu_set_irq(s->irq, 0);
}

const VMStateDescription vmstate_i82596 = {
    .name = "i82596",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(mode, I82596State),
        VMSTATE_UINT16(t_on, I82596State),
        VMSTATE_UINT16(t_off, I82596State),
        VMSTATE_BOOL(throttle_state, I82596State),
        VMSTATE_UINT32(iscp, I82596State),
        VMSTATE_UINT8(sysbus, I82596State),
        VMSTATE_UINT32(scb, I82596State),
        VMSTATE_UINT32(scb_base, I82596State),
        VMSTATE_UINT16(scb_status, I82596State),
        VMSTATE_UINT8(cu_status, I82596State),
        VMSTATE_UINT8(rx_status, I82596State),
        VMSTATE_UINT16(lnkst, I82596State),
        VMSTATE_UINT32(cmd_p, I82596State),
        VMSTATE_INT32(ca, I82596State),
        VMSTATE_INT32(ca_active, I82596State),
        VMSTATE_INT32(send_irq, I82596State),
        VMSTATE_BUFFER(mult, I82596State),
        VMSTATE_BUFFER(config, I82596State),
        VMSTATE_BUFFER(tx_buffer, I82596State),
        VMSTATE_UINT32(tx_retry_addr, I82596State),
        VMSTATE_INT32(tx_retry_count, I82596State),
        VMSTATE_UINT32(tx_good_frames, I82596State),
        VMSTATE_UINT32(tx_collisions, I82596State),
        VMSTATE_UINT32(tx_aborted_errors, I82596State),
        VMSTATE_UINT32(last_tx_len, I82596State),
        VMSTATE_UINT32(collision_events, I82596State),
        VMSTATE_UINT32(total_collisions, I82596State),
        VMSTATE_UINT32(crc_err, I82596State),
        VMSTATE_UINT32(align_err, I82596State),
        VMSTATE_UINT32(resource_err, I82596State),
        VMSTATE_UINT32(over_err, I82596State),
        VMSTATE_UINT32(rcvdt_err, I82596State),
        VMSTATE_UINT32(short_fr_error, I82596State),
        VMSTATE_UINT32(total_frames, I82596State),
        VMSTATE_UINT32(total_good_frames, I82596State),
        VMSTATE_BUFFER(rx_buffer, I82596State),
        VMSTATE_UINT16(tx_frame_len, I82596State),
        VMSTATE_UINT16(rx_frame_len, I82596State),
        VMSTATE_UINT64(current_tx_desc, I82596State),
        VMSTATE_UINT64(current_rx_desc, I82596State),
        VMSTATE_UINT32(last_good_rfa, I82596State),
        VMSTATE_INT32(queue_head, I82596State),
        VMSTATE_INT32(queue_tail, I82596State),
        VMSTATE_INT32(queue_count, I82596State),
        VMSTATE_BOOL(rnr_signaled, I82596State),
        VMSTATE_BOOL(flushing_queue, I82596State),
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
        if (!s->flush_queue_timer) {
            s->flush_queue_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        i82596_flush_queue_timer, s);
        }
        if (!s->throttle_timer) {
            s->throttle_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        i82596_bus_throttle_timer, s);
        }
    }

    s->lnkst = 0x8000; /* initial link state: up */
}
