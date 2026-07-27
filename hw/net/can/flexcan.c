/*
 * QEMU model of the NXP FLEXCAN device.
 *
 * This implementation is based on the following reference manual:
 * i.MX 6Dual/6Quad Applications Processor Reference Manual
 * Document Number: IMX6DQRM, Rev. 6, 05/2020
 *
 * Copyright (c) 2025 Matyas Bobek <matyas.bobek@gmail.com>
 *
 * Based on CTU CAN FD emulation implemented by Jan Charvat.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"
#include "hw/core/qdev-properties.h"
#include "trace.h"

#include "hw/net/flexcan.h"
#include "flexcan_regs.h"
#include "qemu/timer.h"

/*
 * Indicates MB w/ received frame has not been serviced yet
 * This is an emulator-only flag in position of unused (reserved) bit
 * of message buffer control register
 */
#define FLEXCAN_MB_CNT_NOT_SRV          BIT(23)
/**
 * if no MB is locked, FlexcanState.locked_mb
 * is set to FLEXCAN_NO_MB_LOCKED
 */
#define FLEXCAN_NO_MB_LOCKED            -1
/**
 * if no frame is waiting in the SMB, FlexcanState.smb_target_mbid
 * is set to FLEXCAN_SMB_EMPTY
 */
#define FLEXCAN_SMB_EMPTY               -1
/**
 * When the module is disabled or in freeze mode,
 * the timer is not running. That is indicated by setting
 * FlexcanState.timer_start to FLEXCAN_TIMER_STOPPED.
 */
#define FLEXCAN_TIMER_STOPPED           -1

/* These constants are returned by flexcan_fifo_rx() and flexcan_mb_rx(), */
enum FlexcanRx {
/* Retry the other receiving mechanism (ie. message bufer or mailbox). */
    FLEXCAN_RX_SEARCH_RETRY,
/* The frame was received and stored. */
    FLEXCAN_RX_SEARCH_ACCEPT,
/* The frame was filtered out and dropped. */
    FLEXCAN_RX_SEARCH_DROPPED,
};

/*
 * These constants are returned by flexcan_mb_rx_check_mb().
 * See flexcan_mb_rx_check_mb() kerneldoc for details.
 */
enum FlexcanCheck {
    FLEXCAN_CHECK_MB_NIL = 0,
    FLEXCAN_CHECK_MB_MATCH = 3,
    FLEXCAN_CHECK_MB_MATCH_NON_FREE = 1,
    FLEXCAN_CHECK_MB_MATCH_LOCKED = 5,
};

static const FlexcanRegs flexcan_regs_write_mask = {
    .mcr = 0xF6EB337F,
    .ctrl = 0xFFFFFFFF,
    .timer = 0xFFFFFFFF,
    .tcr = 0xFFFFFFFF,
    .rxmgmask = 0xFFFFFFFF,
    .rx14mask = 0xFFFFFFFF,
    .rx15mask = 0xFFFFFFFF,
    .ecr = 0xFFFFFFFF,
    .esr = 0xFFFFFFFF,
    .imask2 = 0xFFFFFFFF,
    .imask1 = 0xFFFFFFFF,
    .iflag2 = 0,
    .iflag1 = 0,
    .ctrl2 = 0xFFFFFFFF,
    .esr2 = 0,
    .imeur = 0,
    .lrfr = 0,
    .crcr = 0,
    .rxfgmask = 0xFFFFFFFF,
    .rxfir = 0,
    .cbt = 0,
    ._reserved2 = 0,
    .dbg1 = 0,
    .dbg2 = 0,
    .mbs = { [0 ... 63] = {
        .can_ctrl = 0xFFFFFFFF & ~FLEXCAN_MB_CNT_NOT_SRV,
        .can_id = 0xFFFFFFFF,
        .data = { 0xFFFFFFFF, 0xFFFFFFFF },
    } },
    ._reserved4 = {0},
    .rximr = { [0 ... 63] = 0xFFFFFFFF },
    ._reserved5 = {0},
    .gfwr_mx6 = 0xFFFFFFFF,
    ._reserved6 = {0},
    ._reserved8 = {0},
    .rx_smb0 = {
        .can_ctrl = 0,
        .can_id = 0,
        .data = { 0, 0 },
    },
    .rx_smb1 = {0, 0, 0, 0},
};
static const FlexcanRegs flexcan_regs_reset_mask = {
    .mcr = 0x80000000,
    .ctrl = 0xFFFFFFFF,
    .timer = 0,
    .tcr = 0,
    .rxmgmask = 0xFFFFFFFF,
    .rx14mask = 0xFFFFFFFF,
    .rx15mask = 0xFFFFFFFF,
    .ecr = 0,
    .esr = 0,
    .imask2 = 0,
    .imask1 = 0,
    .iflag2 = 0,
    .iflag1 = 0,
    .ctrl2 = 0xFFFFFFFF,
    .esr2 = 0,
    .imeur = 0,
    .lrfr = 0,
    .crcr = 0,
    .rxfgmask = 0xFFFFFFFF,
    .rxfir = 0xFFFFFFFF,
    .cbt = 0,
    ._reserved2 = 0,
    .dbg1 = 0,
    .dbg2 = 0,
    .mbs = { [0 ... FLEXCAN_MAILBOX_COUNT - 1] = {
        .can_ctrl = 0xFFFFFFFF,
        .can_id = 0xFFFFFFFF,
        .data = { 0xFFFFFFFF, 0xFFFFFFFF },
    } },
    ._reserved4 = {0},
    .rximr = { [0 ... 63] = 0xFFFFFFFF },
    ._reserved5 = {0},
    .gfwr_mx6 = 0,
    ._reserved6 = {0},
    ._reserved8 = {0},
    .rx_smb0 = {
        .can_ctrl = 0,
        .can_id = 0,
        .data = { 0, 0 },
    },
    .rx_smb1 = {0, 0, 0, 0},
};

/* length of buffer used to format register names in trace output */
#define FLEXCAN_DBG_BUF_LEN 16

/**
 * flexcan_dbg_mb_code_strs - Readable names for CODE field codes
 *
 * Readable names for possible values of CODE field in message buffer
 * control word.
 */
static const char *flexcan_dbg_mb_code_strs[16] = {
    "INACTIVE_RX",
    "FULL",
    "EMPTY",
    "OVERRUN",
    "INACTIVE_TX",
    "RANSWER",
    "DATA",
    "TANSWER"
};

/**
 * flexcan_dbg_mb_code() - Get the string representation of a mailbox code
 * @mb_ctrl: The mailbox control register value
 * @buf: The buffer to store the string representation
 *
 * Return: Either constant string or string formatted into @buf
 */
static const char *flexcan_dbg_mb_code(uint32_t mb_ctrl, char *buf)
{
    uint32_t code = mb_ctrl & FLEXCAN_MB_CODE_MASK;
    uint32_t code_idx = code >> 24;
    if (code == FLEXCAN_MB_CODE_TX_ABORT) {
        return "ABORT";
    } else {
        const char *code_str = flexcan_dbg_mb_code_strs[code_idx >> 1];
        if (code_idx & 1) {
            g_snprintf(buf, FLEXCAN_DBG_BUF_LEN, "%s+BUSY", code_str);
            return buf;
        }

        return code_str;
    }
}

static const char *flexcan_dbg_reg_name_fixed(hwaddr addr)
{
    switch (addr) {
    case offsetof(FlexcanRegs, mcr):
        return "MCR";
    case offsetof(FlexcanRegs, ctrl):
        return "CTRL";
    case offsetof(FlexcanRegs, timer):
        return "TIMER";
    case offsetof(FlexcanRegs, esr):
        return "ESR";
    case offsetof(FlexcanRegs, rxmgmask):
        return "RXMGMASK";
    case offsetof(FlexcanRegs, rx14mask):
        return "RX14MASK";
    case offsetof(FlexcanRegs, rx15mask):
        return "RX15MASK";
    case offsetof(FlexcanRegs, rxfgmask):
        return "RXFGMASK";
    case offsetof(FlexcanRegs, ecr):
        return "ECR";
    case offsetof(FlexcanRegs, ctrl2):
        return "CTRL2";
    case offsetof(FlexcanRegs, imask2):
        return "IMASK2";
    case offsetof(FlexcanRegs, imask1):
        return "IMASK1";
    case offsetof(FlexcanRegs, iflag2):
        return "IFLAG2";
    case offsetof(FlexcanRegs, iflag1):
        return "IFLAG1";
    }
    return NULL;
}

static inline void flexcan_trace_mem_op(FlexcanState *s, hwaddr addr,
                                        uint32_t value, int size, bool is_wr)
{
    if (trace_event_get_state_backends(TRACE_FLEXCAN_MEM_OP)) {
        const char *reg_name = "unknown";
        char reg_name_buf[FLEXCAN_DBG_BUF_LEN] = { 0 };
        const char *reg_name_fixed = flexcan_dbg_reg_name_fixed(addr);
        const char *op_string = is_wr ? "write" : "read";

        if (reg_name_fixed) {
            reg_name = reg_name_fixed;
        } else if (addr >= 0x80 && addr < 0x480) {
            int mbidx = (addr - 0x80) / 16;
            g_snprintf(reg_name_buf, sizeof(reg_name_buf), "MB%i", mbidx);
            reg_name = reg_name_buf;
        } else if (addr >= 0x880 && addr < 0x9e0) {
            int id = (addr - 0x880) / 4;
            g_snprintf(reg_name_buf, sizeof(reg_name_buf), "RXIMR%i", id);
            reg_name = reg_name_buf;
        }

        trace_flexcan_mem_op(DEVICE(s)->canonical_path, op_string, value, addr,
                             reg_name, size);
    }
}

static enum FlexcanRx flexcan_mb_rx(FlexcanState *s,
                                    const qemu_can_frame *frame);
static void flexcan_mb_unlock(FlexcanState *s);

/* ========== Mailbox Utils ========== */

/**
 * flexcan_mailbox_count() - Get number of enabled mailboxes
 * @s: FlexCAN device pointer
 *
 * Count is based on MCR[MAXMB] field. Note that some of those mailboxes
 * might be part of queue or queue ID filters or ordinary message buffers.
 */
static inline int flexcan_enabled_mailbox_count(const FlexcanState *s)
{
    return MIN((s->regs.mcr & FLEXCAN_MCR_MAXMB(UINT32_MAX)) + 1,
               FLEXCAN_MAILBOX_COUNT);
}

/**
 * flexcan_get_first_message_buffer() - Get pointer to first message buffer
 * @s: FlexCAN device pointer
 *
 * In context of this function, message buffer means a mailbox which is not
 * a queue element nor a queue filter. Note this function does not take
 * MCR[MAXMB] into account, meaning that the returned mailbox
 * might be disabled.
 */
static FlexcanRegsMessageBuffer *flexcan_get_first_message_buffer(
                                 FlexcanState *s)
{
    if (s->regs.mcr & FLEXCAN_MCR_FEN) {
        int rffn = (s->regs.ctrl2 & FLEXCAN_CTRL2_RFFN(UINT32_MAX)) >> 24;
        return s->regs.mbs + 8 + 2 * rffn;
    }

    return s->regs.mbs;
}

/**
 * flexcan_get_last_enabled_mailbox() - Get pointer to last enabled mailbox.
 * @s: FlexCAN device pointer
 *
 * When used with flexcan_get_first_message_buffer(), all mailboxes *ptr in
 * range `first_message_buffer() <= ptr <= last_enabled_mailbox` are valid
 * message buffer mailboxes.
 *
 * Return: Last enabled mailbox in MCR[MAXMB] sense. The mailbox might be
 * of any type.
 */
static inline FlexcanRegsMessageBuffer *flexcan_get_last_enabled_mailbox(
                                 FlexcanState *s)
{
    return s->regs.mbs + flexcan_enabled_mailbox_count(s);
}

/* ========== Free-running Timer ========== */
static inline int64_t flexcan_get_time(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

/**
 * flexcan_get_bitrate() - Calculate CAN bitrate (in Hz)
 * @s: FlexCAN device pointer
 *
 * The bitrate is determined by FlexCAN configuration in CTRL1 register,
 * and CCM co
 */
static uint32_t flexcan_get_bitrate(FlexcanState *s)
{
    uint32_t conf_presdiv = (s->regs.ctrl & FLEXCAN_CTRL_PRESDIV_MASK) >> 24;
    uint32_t conf_pseg1 = (s->regs.ctrl & FLEXCAN_CTRL_PSEG1_MASK) >> 19;
    uint32_t conf_pseg2 = (s->regs.ctrl & FLEXCAN_CTRL_PSEG2_MASK) >> 16;
    uint32_t conf_propseg = s->regs.ctrl & FLEXCAN_CTRL_PROPSEG_MASK;

    /* N of time quanta for segments */
    uint32_t tseg1 = 2 + conf_pseg1 + conf_propseg;
    uint32_t tseg2 = 1 + conf_pseg2;
    uint32_t total_qpb = 1 + tseg1 + tseg2;

    uint32_t pe_freq, s_freq, bitrate;

    /* s_freq: CAN clock from CCM divided by the prescaler */
    pe_freq = imx_ccm_get_clock_frequency(s->ccm, CLK_CAN);
    s_freq = pe_freq / (1 + conf_presdiv);
    bitrate = s_freq / total_qpb;

    trace_flexcan_get_bitrate(DEVICE(s)->canonical_path, pe_freq,
                              1 + conf_presdiv, s_freq, tseg1, tseg2, total_qpb,
                              bitrate);
    return bitrate;
}

/**
 * int128_mul_6464() - Multiply two 64-bit integers into a 128-bit one
 */
static Int128 int128_muls_6464(int64_t ai, int64_t bi)
{
    uint64_t l, h;

    muls64(&l, &h, ai, bi);
    return int128_make128(l, h);
}

/**
 * flexcan_get_timestamp() - Get current value of the 16-bit free-running timer
 * @s: FlexCAN device pointer
 * @mk_unique: if true, make the timestamp unique by incrementing it if needed
 */
static uint32_t flexcan_get_timestamp(FlexcanState *s, bool mk_unique)
{
    const Int128 nanoseconds_in_second = int128_makes64((int64_t)1e9);
    Int128 ncycles, cycles128;
    int64_t current_time, elapsed_time_ns;
    uint64_t cycles;
    uint32_t rv, shift = 0;

    if (s->timer_start == FLEXCAN_TIMER_STOPPED) {
        /* timer is not running, return last value */
        trace_flexcan_get_timestamp(DEVICE(s)->canonical_path, -1, 0, 0, 0,
                                    s->regs.timer);
        return s->regs.timer;
    }

    current_time = flexcan_get_time();
    elapsed_time_ns = current_time - s->timer_start;
    if (elapsed_time_ns < 0) {
        trace_flexcan_timer_overflow(DEVICE(s)->canonical_path, current_time,
                                     s->timer_start, elapsed_time_ns);
        return 0xFFFF;
    }

    ncycles = int128_muls_6464(s->timer_freq, elapsed_time_ns);
    cycles128 = int128_divs(ncycles, nanoseconds_in_second);
    /* 64 bits hold for over 50k years at 10MHz */
    cycles = int128_getlo(cycles128);

    if (mk_unique && cycles <= s->last_rx_timer_cycles) {
        shift = 1;
        cycles = s->last_rx_timer_cycles + shift;
    }

    s->last_rx_timer_cycles = cycles;
    rv = (uint32_t)cycles & 0xFFFF;

    trace_flexcan_get_timestamp(DEVICE(s)->canonical_path,
                                elapsed_time_ns / (uint32_t)1e6,
                                s->timer_freq, cycles, shift, rv);
    return rv;
}

/**
 * flexcan_timer_start() - Start the free-running timer
 * @s: FlexCAN device pointer
 *
 * This should be called when the module leaves freeze mode.
 */
static void flexcan_timer_start(FlexcanState *s)
{
    s->timer_freq = flexcan_get_bitrate(s);
    s->timer_start = flexcan_get_time();
    s->last_rx_timer_cycles = 0;

    trace_flexcan_timer_start(DEVICE(s)->canonical_path, s->timer_freq,
                              s->regs.timer);
}

/**
 * flexcan_timer_stop() - Stop the free-running timer
 * @s: FlexCAN device pointer
 *
 * This should be called when the module enters freeze mode.
 * Stores the current timestamp in the TIMER register.
 */
static void flexcan_timer_stop(FlexcanState *s)
{
    s->regs.timer = flexcan_get_timestamp(s, false);
    s->timer_start = FLEXCAN_TIMER_STOPPED;

    trace_flexcan_timer_stop(DEVICE(s)->canonical_path, s->timer_freq,
                             s->regs.timer);
}

/* ========== IRQ handling ========== */
/**
 * flexcan_irq_update() - Update qemu_irq line based on interrupt registers
 * @s: FlexCAN device pointer
 */
static void flexcan_irq_update(FlexcanState *s)
{
    uint32_t mb_irqs[2];
    int irq_pending;
    /* these are all interrupt sources from FlexCAN */
    /* mailbox interrupt sources */
    mb_irqs[0] = s->regs.iflag1 & s->regs.imask1;
    mb_irqs[1] = s->regs.iflag2 & s->regs.imask2;

    /**
     * these interrupts aren't currently used and they can never be raised
     *
     * bool irq_wake_up = (s->regs.mcr & FLEXCAN_MCR_WAK_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_WAK_INT);
     * bool irq_bus_off = (s->regs.ctrl & FLEXCAN_CTRL_BOFF_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_BOFF_INT);
     * bool irq_error = (s->regs.ctrl & FLEXCAN_CTRL_ERR_MSK) &&
     *                  (s->regs.ecr & FLEXCAN_ESR_ERR_INT);
     * bool irq_tx_warn = (s->regs.ctrl & FLEXCAN_CTRL_TWRN_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_TWRN_INT);
     * bool irq_rx_warn = (s->regs.ctrl & FLEXCAN_CTRL_RWRN_MSK) &&
     *                    (s->regs.ecr & FLEXCAN_ESR_RWRN_INT);
     */

    irq_pending = (mb_irqs[0] || mb_irqs[1]) ? 1 : 0;
    trace_flexcan_irq_update(DEVICE(s)->canonical_path, mb_irqs[0], mb_irqs[1],
                             irq_pending);

    qemu_set_irq(s->irq, irq_pending);
}

/**
 * flexcan_irq_iflag_set() - Set IFLAG bit corresponding to MB mbidx
 * @s: FlexCAN device pointer
 * @mbidx: mailbox index
 */
static void flexcan_irq_iflag_set(FlexcanState *s, int mbidx)
{
    if (mbidx < 32) {
        s->regs.iflag1 |= BIT(mbidx);
    } else {
        s->regs.iflag2 |= BIT(mbidx - 32);
    }
}

/**
 * flexcan_irq_iflag_clear() - Clear IFLAG bit corresponding to MB mbidx
 * @s: FlexCAN device pointer
 * @mbidx: mailbox index
 */
static void flexcan_irq_iflag_clear(FlexcanState *s, int mbidx)
{
    if (mbidx < 32) {
        s->regs.iflag1 &= ~BIT(mbidx);
    } else {
        s->regs.iflag2 &= ~BIT(mbidx - 32);
    }
}

/* ========== RESET ========== */
static void flexcan_reset_local_state(FlexcanState *s)
{
    uint32_t *reset_mask = (uint32_t *)&flexcan_regs_reset_mask;
    for (int i = 0; i < (sizeof(FlexcanRegs) / 4); i++) {
        s->regs_raw[i] &= reset_mask[i];
    }

    s->regs.mcr |= 0x5980000F;
    s->locked_mbidx = FLEXCAN_NO_MB_LOCKED;
    s->smb_target_mbidx = FLEXCAN_SMB_EMPTY;
    s->timer_start = FLEXCAN_TIMER_STOPPED;

    trace_flexcan_reset(DEVICE(s)->canonical_path);
}

static void flexcan_reset_enter(Object *obj, ResetType type)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    memset(&s->regs, 0, sizeof(s->regs));
    flexcan_reset_local_state(s);
}

static void flexcan_reset_hold(Object *obj, ResetType type)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    flexcan_irq_update(s);
}


/* ========== Operation mode control ========== */
/**
 * flexcan_update_esr() - Update ESR based on mode and CAN bus connection state
 * @s: FlexCAN device pointer
 */
static void flexcan_update_esr(FlexcanState *s)
{
    bool is_running = (s->regs.mcr & FLEXCAN_MCR_NOT_RDY) == 0;
    /* potentially, there could be other influences on ESR[SYNCH] */

    if (is_running && s->canbus) {
        s->regs.esr |= FLEXCAN_ESR_SYNCH | FLEXCAN_ESR_IDLE;
    } else {
        s->regs.esr &= ~(FLEXCAN_ESR_SYNCH | FLEXCAN_ESR_IDLE);
    }
}

/**
 * flexcan_update_esr() - Process MCR write
 * @s: FlexCAN device pointer
 * @pv: previously set MCR value
 *
 * This function expects the new MCR value to be already written in s->regs.mcr.
 */
static void flexcan_set_mcr(FlexcanState *s, const uint32_t pv)
{
    uint32_t cv = s->regs.mcr;

    /* -- module disable mode -- */
    if (!(pv & FLEXCAN_MCR_MDIS) && (cv & FLEXCAN_MCR_MDIS)) {
        /* transition to Module Disable mode */
        cv |= FLEXCAN_MCR_LPM_ACK;
    } else if ((pv & FLEXCAN_MCR_MDIS) && !(cv & FLEXCAN_MCR_MDIS)) {
        /* transition from Module Disable mode */
        cv &= ~FLEXCAN_MCR_LPM_ACK;
    }

    /* -- soft reset -- */
    if (!(cv & FLEXCAN_MCR_LPM_ACK) && (cv & FLEXCAN_MCR_SOFTRST)) {
        if (s->regs.mcr & FLEXCAN_MCR_LPM_ACK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid soft reset request in low-power mode",
                          DEVICE(s)->canonical_path);
        }

        flexcan_reset_local_state(s);
        cv = s->regs.mcr;
    }

    /* -- freeze mode -- */
    if (!(cv & FLEXCAN_MCR_LPM_ACK) &&
        (cv & FLEXCAN_MCR_FRZ) &&
        (cv & FLEXCAN_MCR_HALT)) {
        cv |= FLEXCAN_MCR_FRZ_ACK;
    } else {
        cv &= ~FLEXCAN_MCR_FRZ_ACK;
    }

    /* -- fifo mode -- */
    if (
        ((pv & FLEXCAN_MCR_FEN) && !(cv & FLEXCAN_MCR_FEN)) ||
        (!(pv & FLEXCAN_MCR_FEN) && (cv & FLEXCAN_MCR_FEN))
    ) {
        /* clear iflags used by fifo */
        s->regs.iflag1 &= ~(
            FLEXCAN_IFLAG_RX_FIFO_AVAILABLE |
            FLEXCAN_IFLAG_RX_FIFO_OVERFLOW |
            FLEXCAN_IFLAG_RX_FIFO_WARN
        );
    }
    if (!(pv & FLEXCAN_MCR_FEN) && (cv & FLEXCAN_MCR_FEN)) {
        /* zero out fifo region, we rely on zeroed can_ctrl for empty slots */
        memset(s->regs.mbs, 0,
               FLEXCAN_FIFO_DEPTH * sizeof(FlexcanRegsMessageBuffer));
    }

    /*
     * assert NOT_RDY bit if in disable,
     * stop (not implemented) or freeze mode
     */
    if ((cv & FLEXCAN_MCR_LPM_ACK) || (cv & FLEXCAN_MCR_FRZ_ACK)) {
        cv |= FLEXCAN_MCR_NOT_RDY;
    } else {
        cv &= ~FLEXCAN_MCR_NOT_RDY;
    }

    if ((pv & FLEXCAN_MCR_NOT_RDY) && !(cv & FLEXCAN_MCR_NOT_RDY)) {
        /* module went up, start the timer */
        flexcan_timer_start(s);
    } else if (!(pv & FLEXCAN_MCR_NOT_RDY) && (cv & FLEXCAN_MCR_NOT_RDY)) {
        /* module went down, store the current timer value */
        flexcan_timer_stop(s);
    }

    s->regs.mcr = cv;
    flexcan_update_esr(s);
    trace_flexcan_set_mcr(
        DEVICE(s)->canonical_path,
        cv & FLEXCAN_MCR_LPM_ACK ? "DISABLED" : "ENABLED",
        (cv & FLEXCAN_MCR_FRZ_ACK || cv & FLEXCAN_MCR_LPM_ACK) ?
            "FROZEN" : "RUNNING",
        cv & FLEXCAN_MCR_FEN ? "FIFO" : "MAILBOX",
        cv & FLEXCAN_MCR_NOT_RDY ? "NOT_RDY" : "RDY",
        s->regs.esr & FLEXCAN_ESR_SYNCH ? "SYNC" : "NOSYNC"
    );
}

/* ========== TX ========== */
static void flexcan_transmit(FlexcanState *s, int mbidx)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbidx];
    qemu_can_frame frame = {
        .flags = 0,
    };
    uint32_t *frame_data = (uint32_t *)&frame.data;
    uint32_t timestamp = flexcan_get_timestamp(s, true);

    if ((s->regs.ctrl & FLEXCAN_CTRL_LOM) ||
        (s->regs.mcr & FLEXCAN_MCR_NOT_RDY)) {
        /* no transmiting in listen-only, freeze or low-power mode */
        return;
    }

    if (mb->can_ctrl & FLEXCAN_MB_CNT_IDE) {
        /* 29b ID stored in bits [0, 29) */
        uint32_t id = mb->can_id & 0x1FFFFFFF;
        frame.can_id = id | QEMU_CAN_EFF_FLAG;
    } else {
        /* 11b ID stored in bits [18, 29) */
        uint32_t id = (mb->can_id & (0x7FF << 18)) >> 18;
        frame.can_id = id;
    }

    frame.can_dlc = (mb->can_ctrl & (0xF << 16)) >> 16;

    for (int i = 0; i < 2; i++) {
        stl_be_p(&frame_data[i], mb->data[i]);
    }

    if (!(s->regs.mcr & FLEXCAN_MCR_SRX_DIS)) {
        /* self-reception */
        flexcan_mb_rx(s, &frame);
    }
    if (!(s->regs.ctrl & FLEXCAN_CTRL_LPB)) {
        /* send to bus if not in loopback mode */
        if (s->canbus) {
            can_bus_client_send(&s->bus_client, &frame, 1);
        } else {
            /* todo: raise error (no ack) */
        }
    }

    mb->can_ctrl &= ~(FLEXCAN_MB_CODE_MASK | FLEXCAN_MB_CNT_TIMESTAMP_MASK);
    mb->can_ctrl |= FLEXCAN_MB_CODE_TX_INACTIVE |
                    FLEXCAN_MB_CNT_TIMESTAMP(timestamp);

    /* todo: compute the CRC */
    s->regs.crcr = FLEXCAN_CRCR_TXCRC(0) | FLEXCAN_CRCR_MBCRC(mbidx);

    flexcan_irq_iflag_set(s, mbidx);
}

static void flexcan_mb_write(FlexcanState *s, int mbid)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbid];

    bool is_mailbox = (mb <= flexcan_get_last_enabled_mailbox(s)) &&
                     (mb >= flexcan_get_first_message_buffer(s));

    if (trace_event_get_state_backends(TRACE_FLEXCAN_MB_WRITE)) {
        char code_str_buf[FLEXCAN_DBG_BUF_LEN] = { 0 };
        const char *code_str = flexcan_dbg_mb_code(mb->can_ctrl, code_str_buf);
        trace_flexcan_mb_write(DEVICE(s)->canonical_path, mbid, code_str,
                               is_mailbox, mb->can_ctrl, mb->can_id);
    }

    if (!is_mailbox) {
        /**
         * Disabled mailbox or mailbox in region of queue filters
         * was updated. Either way there is nothing to do.
         */
        return;
    }

    /* any write to message buffer clears the not_serviced flag */
    mb->can_ctrl &= ~FLEXCAN_MB_CNT_NOT_SRV;

    /**
     * todo: search for active tx mbs on transition from freeze/disable mode
     */
    switch (mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_TX_INACTIVE:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_INACTIVE:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_EMPTY:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_FULL:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_RANSWER:
        break;

    case FLEXCAN_MB_CODE_TX_DATA:
        flexcan_transmit(s, mbid);
        break;
    case FLEXCAN_MB_CODE_TX_ABORT:
        /*
         * as transmission is instant, it can never be aborted
         * we need to set CODE in C/S back to the previous code
         */
        mb->can_ctrl &= ~FLEXCAN_MB_CODE(1);
        break;
    case FLEXCAN_MB_CODE_TX_TANSWER:
        break;
    default:
        /* prevent setting the busy bit */
        mb->can_ctrl &= ~FLEXCAN_MB_CODE_RX_BUSY_BIT;
        break;
  }

}

/* ========== RX ========== */
static void flexcan_mb_move_in(FlexcanState *s, const qemu_can_frame *frame,
                               FlexcanRegsMessageBuffer *target_mb)
{
    uint32_t frame_len = frame->can_dlc;
    uint32_t *frame_data = (uint32_t *)&frame->data;
    int timestamp = flexcan_get_timestamp(s, true);
    uint32_t new_code = 0;

    memset(target_mb, 0, sizeof(FlexcanRegsMessageBuffer));

    if (frame_len > 8) {
        frame_len = 8;
    }
    for (int i = 0; i < 2; i++) {
        target_mb->data[i] = ldl_be_p(&frame_data[i]);
    }

    switch (target_mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_RX_FULL:
    case FLEXCAN_MB_CODE_RX_OVERRUN:
        if (target_mb->can_ctrl & FLEXCAN_MB_CNT_NOT_SRV) {
            new_code = FLEXCAN_MB_CODE_RX_OVERRUN;
        } else {
            new_code = FLEXCAN_MB_CODE_RX_FULL;
        }
        break;
    case FLEXCAN_MB_CODE_RX_RANSWER:
        assert(s->regs.ctrl2 & FLEXCAN_CTRL2_RRS);
        new_code = FLEXCAN_MB_CODE_TX_TANSWER;
        break;
    default:
        new_code = FLEXCAN_MB_CODE_RX_FULL;
    }

    target_mb->can_ctrl = new_code
        | FLEXCAN_MB_CNT_TIMESTAMP(timestamp)
        | FLEXCAN_MB_CNT_LENGTH(frame_len)
        | FLEXCAN_MB_CNT_NOT_SRV
        | FLEXCAN_MB_CNT_SRR; /* always set for received frames */
    if (frame->can_id & QEMU_CAN_RTR_FLAG) {
        target_mb->can_ctrl |= FLEXCAN_MB_CNT_RTR;
    }

    if (frame->can_id & QEMU_CAN_EFF_FLAG) {
        target_mb->can_ctrl |= FLEXCAN_MB_CNT_IDE;
        target_mb->can_id |= frame->can_id & QEMU_CAN_EFF_MASK;
    } else {
        target_mb->can_id |= (frame->can_id & QEMU_CAN_SFF_MASK) << 18;
    }
}
static void flexcan_mb_lock(FlexcanState *s, int mbidx)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbidx];
    if ((mb > flexcan_get_last_enabled_mailbox(s)) ||
        (mb < flexcan_get_first_message_buffer(s))) {
        return;
    }
    switch (mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_RX_FULL:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_OVERRUN:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_RANSWER:
        /* continue */
        trace_flexcan_mb_lock(DEVICE(s)->canonical_path, mbidx, 1);
        break;
    default:
        trace_flexcan_mb_lock(DEVICE(s)->canonical_path, mbidx, 0);
        return;
    }

    s->locked_mbidx = mbidx;
}

static void flexcan_mb_unlock(FlexcanState *s)
{
    int locked_mbidx = s->locked_mbidx;
    bool has_pending_frame = locked_mbidx == s->smb_target_mbidx;

    if (s->locked_mbidx == FLEXCAN_NO_MB_LOCKED) {
        return;
    }

    assert(locked_mbidx >= 0 && locked_mbidx < FLEXCAN_MAILBOX_COUNT);
    FlexcanRegsMessageBuffer *locked_mb = &s->regs.mbs[locked_mbidx];
    s->locked_mbidx = FLEXCAN_NO_MB_LOCKED;

    if (locked_mb >= flexcan_get_first_message_buffer(s) &&
        locked_mb <= flexcan_get_last_enabled_mailbox(s)
    ) {
        /* mark the message buffer as serviced */
        locked_mb->can_ctrl &= ~FLEXCAN_MB_CNT_NOT_SRV;
    }

    /* try move in from SMB */
    trace_flexcan_mb_unlock(DEVICE(s)->canonical_path, locked_mbidx,
                            has_pending_frame ? " PENDING FRAME IN SMB" : "");

    /* todo: in low-power modes, this should be postponed until exit */
    if (has_pending_frame) {
        FlexcanRegsMessageBuffer *target_mb = &s->regs.mbs[locked_mbidx];
        memcpy(target_mb, &s->regs.rx_smb0, sizeof(FlexcanRegsMessageBuffer));

        memset(&s->regs.rx_smb0, 0, sizeof(FlexcanRegsMessageBuffer));
        s->locked_mbidx = FLEXCAN_SMB_EMPTY;

        flexcan_irq_iflag_set(s, locked_mbidx);
    }
}

static bool flexcan_can_receive(CanBusClientState *client)
{
    FlexcanState *s = container_of(client, FlexcanState, bus_client);
    return !(s->regs.mcr & FLEXCAN_MCR_NOT_RDY);
}

/* --------- RX FIFO ---------- */

/**
 * flexcan_fifo_pop() - Pop message from FIFO and update IRQs
 * @s: FlexCAN device pointer
 *
 * Does not require the queue to be non-empty.
 */
static void flexcan_fifo_pop(FlexcanState *s)
{
    if (s->regs.mbs[0].can_ctrl != 0) {
        /* move queue elements forward */
        memmove(&s->regs.mbs[0], &s->regs.mbs[1],
                sizeof(s->regs.mbs[0]) * (FLEXCAN_FIFO_DEPTH - 1));

        /* clear the first-in slot */
        memset(&s->regs.mbs[FLEXCAN_FIFO_DEPTH - 1], 0,
               sizeof(FlexcanRegsMessageBuffer));

        trace_flexcan_fifo_pop(DEVICE(s)->canonical_path, 1,
                               s->regs.mbs[0].can_ctrl != 0);
    } else {
        trace_flexcan_fifo_pop(DEVICE(s)->canonical_path, 0, 0);
    }

    if (s->regs.mbs[0].can_ctrl != 0) {
        flexcan_irq_iflag_set(s, I_FIFO_AVAILABLE);
    } else {
        flexcan_irq_iflag_clear(s, I_FIFO_AVAILABLE);
    }
}

/**
 * flexcan_fifo_find_free_slot() - Find the first free slot in the FIFO
 * @s: FlexCAN device pointer
 *
 * Return: Pointer to the first free slot in the FIFO,
 *         or NULL if the queue is full.
 */
static FlexcanRegsMessageBuffer *flexcan_fifo_find_free_slot(FlexcanState *s)
{
    for (int i = 0; i < FLEXCAN_FIFO_DEPTH; i++) {
        FlexcanRegsMessageBuffer *mb = &s->regs.mbs[i];
        if (mb->can_ctrl == 0) {
            return mb;
        }
    }
    return NULL;
}

/**
 * flexcan_fifo_push() - Update FIFO IRQs after frame move-in
 * @s: FlexCAN device pointer
 * @slot: Target FIFO slot
 *
 * The usage is as follows:
 * 1. Get free slot pointer using flexcan_fifo_find_free_slot()
 * 2. Move the frame in if not NULL
 * 3. Call flexcan_fifo_push() regardless of the NULL pointer
 */
static void flexcan_fifo_push(FlexcanState *s, FlexcanRegsMessageBuffer *slot)
{
    if (slot) {
        int n_occupied = slot - s->regs.mbs;
        if (n_occupied == 4) { /* 4 means the 5th slot was filled in */
            /*
             * fifo occupancy increased from 4 to 5,
             * raising FIFO_WARN interrupt
             */
            flexcan_irq_iflag_set(s, I_FIFO_WARN);
        }
        flexcan_irq_iflag_set(s, I_FIFO_AVAILABLE);

        trace_flexcan_fifo_push(DEVICE(s)->canonical_path, n_occupied);
    } else {
        flexcan_irq_iflag_set(s, I_FIFO_OVERFLOW);

        trace_flexcan_fifo_push(DEVICE(s)->canonical_path, -1);
    }
}

static enum FlexcanRx flexcan_fifo_rx(FlexcanState *s,
                                      const qemu_can_frame *buf)
{
    /* todo: filtering. return FLEXCAN_FIFO_RX_RETRY if filtered out */
    if ((s->regs.mcr & FLEXCAN_MCR_IDAM_MASK) == FLEXCAN_MCR_IDAM_D) {
        /* all frames rejected */
        return FLEXCAN_RX_SEARCH_RETRY;
    } else {
        /* push message to queue if not full */
        FlexcanRegsMessageBuffer *slot = flexcan_fifo_find_free_slot(s);
        if (slot) {
            flexcan_mb_move_in(s, buf, slot);
        }
        flexcan_fifo_push(s, slot);

        return slot ? FLEXCAN_RX_SEARCH_ACCEPT : FLEXCAN_RX_SEARCH_DROPPED;
    }
}

/* --------- RX message buffer ---------- */

/**
 * flexcan_mb_rx_check_mb() - Check if a mb matches a received frame
 * @s: FlexCAN device pointer
 * @buf: Frame to be received from CAN subsystem
 * @mbid: Target mailbox index. The mailbox must be a valid message buffer.
 *
 * Return: FLEXCAN_CHECK_MB_NIL if the message buffer does not match.
 *         FLEXCAN_CHECK_MB_MATCH if the message buffer matches the received
 *                                frame and is free-to-receive,
 *         FLEXCAN_CHECK_MB_MATCH_LOCKED if the message buffer matches,
 *                                       but is locked,
 *         FLEXCAN_CHECK_MB_MATCH_NON_FREE if the message buffer matches,
 *                                         but is not free-to-receive
 *                                         for some other reason.
 */
static enum FlexcanCheck flexcan_mb_rx_check_mb(FlexcanState *s,
                                                const qemu_can_frame *buf,
                                                int mbid)
{
    FlexcanRegsMessageBuffer *mb = &s->regs.mbs[mbid];
    const bool is_rtr = !!(buf->can_id & QEMU_CAN_RTR_FLAG);
    const bool is_serviced = !(mb->can_ctrl & FLEXCAN_MB_CNT_NOT_SRV);
    const bool is_locked = s->locked_mbidx == mbid;

    bool is_free_to_receive = false;
    bool is_matched = false;

    switch (mb->can_ctrl & FLEXCAN_MB_CODE_MASK) {
    case FLEXCAN_MB_CODE_RX_RANSWER:
        if (is_rtr && !(s->regs.ctrl2 & FLEXCAN_CTRL2_RRS)) {
            /* todo: do the actual matching/filtering and RTR answer */
            is_matched = true;
        }
        break;
    case FLEXCAN_MB_CODE_RX_FULL:
        QEMU_FALLTHROUGH;
    case FLEXCAN_MB_CODE_RX_OVERRUN:
        is_free_to_receive = is_serviced;
        /* todo: do the actual matching/filtering */
        is_matched = true;
        break;
    case FLEXCAN_MB_CODE_RX_EMPTY:
        is_free_to_receive = true;
        /* todo: do the actual matching/filtering */
        is_matched = true;
        break;
    default:
    break;
    }

    if (trace_event_get_state_backends(TRACE_FLEXCAN_MB_RX_CHECK_MB)) {
        char code_str_buf[FLEXCAN_DBG_BUF_LEN] = { 0 };
        const char *code_str = flexcan_dbg_mb_code(mb->can_ctrl, code_str_buf);
        trace_flexcan_mb_rx_check_mb(DEVICE(s)->canonical_path, mbid, code_str,
                                     is_matched, is_free_to_receive,
                                     is_serviced, is_locked);
    }

    if (!is_matched) {
        return FLEXCAN_CHECK_MB_NIL;
    }

    if (is_locked) {
        return FLEXCAN_CHECK_MB_MATCH_LOCKED;
    }

    if (is_free_to_receive) {
        return FLEXCAN_CHECK_MB_MATCH;
    }

    return FLEXCAN_CHECK_MB_MATCH_NON_FREE;
}

static enum FlexcanRx flexcan_mb_rx(FlexcanState *s, const qemu_can_frame *buf)
{
    int last_not_free_to_receive_mbid = -1;
    bool last_not_free_to_receive_locked = false;

    FlexcanRegsMessageBuffer *first_mb = flexcan_get_first_message_buffer(s);
    FlexcanRegsMessageBuffer *last_mb = flexcan_get_last_enabled_mailbox(s);

    for (FlexcanRegsMessageBuffer *mb = first_mb;
         mb <= last_mb; mb++) {
        int mbid = mb - s->regs.mbs;
        enum FlexcanCheck r = flexcan_mb_rx_check_mb(s, buf, mbid);
        if (r == FLEXCAN_CHECK_MB_MATCH) {
            flexcan_mb_move_in(s, buf, mb);
            flexcan_irq_iflag_set(s, mbid);
            return FLEXCAN_RX_SEARCH_ACCEPT;
        }

        if (r == FLEXCAN_CHECK_MB_MATCH_NON_FREE) {
            last_not_free_to_receive_mbid = mbid;
            last_not_free_to_receive_locked = false;
        } else if (r == FLEXCAN_CHECK_MB_MATCH_LOCKED) {
            /*
             * message buffer is locked,
             * we can move in the message after it's unlocked
             */
            last_not_free_to_receive_mbid = mbid;
            last_not_free_to_receive_locked = true;
        }
    }

    if (last_not_free_to_receive_mbid >= 0) {
        if (last_not_free_to_receive_locked) {
            /*
             * copy to temporary mailbox (SMB)
             * it will be moved in when the mailbox is unlocked
             */
            s->regs.rx_smb0.can_ctrl =
                s->regs.mbs[last_not_free_to_receive_mbid].can_id;
            flexcan_mb_move_in(s, buf, &s->regs.rx_smb0);
            s->smb_target_mbidx = last_not_free_to_receive_mbid;
            return FLEXCAN_RX_SEARCH_ACCEPT;
        }

        if (s->regs.mcr & FLEXCAN_MCR_IRMQ) {
            flexcan_mb_move_in(s, buf,
                               &s->regs.mbs[last_not_free_to_receive_mbid]);
            flexcan_irq_iflag_set(s, last_not_free_to_receive_mbid);
            return FLEXCAN_RX_SEARCH_ACCEPT;
        }
    }

    return FLEXCAN_RX_SEARCH_RETRY;
}

static ssize_t flexcan_receive(CanBusClientState *client,
                               const qemu_can_frame *frames, size_t frames_cnt)
{
    FlexcanState *s = container_of(client, FlexcanState, bus_client);
    trace_flexcan_receive(DEVICE(s)->canonical_path, frames_cnt);

    if (frames_cnt == 0) {
        return 0;
    }

    /* clear the SMB, as it would be overriden in hardware */
    memset(&s->regs.rx_smb0, 0, sizeof(FlexcanRegsMessageBuffer));
    s->smb_target_mbidx = FLEXCAN_SMB_EMPTY;

    for (size_t i = 0; i < frames_cnt; i++) {
        int r;
        const qemu_can_frame *frame = &frames[i];
        if (frame->can_id & QEMU_CAN_ERR_FLAG) {
            /* todo: error frame handling */
            continue;
        }
        if (frame->flags & QEMU_CAN_FRMF_TYPE_FD) {
            /* CAN FD supported only in later FlexCAN version */
            continue;
        }

        /* todo: this order logic is not complete and needs further work */
        if (s->regs.mcr & FLEXCAN_MCR_FEN &&
            s->regs.ctrl2 & FLEXCAN_CTRL2_MRP) {
            r = flexcan_mb_rx(s, frame);
            if (r == FLEXCAN_RX_SEARCH_RETRY) {
                flexcan_fifo_rx(s, frame);
            }
        } else if (s->regs.mcr & FLEXCAN_MCR_FEN) {
            r = flexcan_fifo_rx(s, frame);
            if (r == FLEXCAN_RX_SEARCH_RETRY) {
                flexcan_mb_rx(s, frame);
            }
        } else {
            flexcan_mb_rx(s, frame);
        }
    }

    flexcan_irq_update(s);
    return 1;
}

/* ========== I/O handling ========== */
static void flexcan_mem_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    FlexcanState *s = opaque;
    const int mbid = (addr - offsetof(FlexcanRegs, mbs)) /
        sizeof(s->regs.mbs[0]);
    uint32_t write_mask = ((const uint32_t *)
        &flexcan_regs_write_mask)[addr / 4];
    uint32_t old_value = s->regs_raw[addr / 4];

    /*
     * 0 for bits that can "only be written in Freeze mode as it is blocked
     * by hardware in other modes"
     */
    const uint32_t freeze_mask_mcr = 0xDF54CC80;
    const uint32_t freeze_mask_ctrl1 = 0x0000E740;

    flexcan_trace_mem_op(s, addr, val, size, true);
    switch (addr) {
    case offsetof(FlexcanRegs, mcr):
        if (!(s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
            write_mask &= freeze_mask_mcr;
        }
        s->regs.mcr = (val & write_mask) | (old_value & ~write_mask);
        flexcan_set_mcr(s, old_value);
        break;
    case offsetof(FlexcanRegs, ctrl):
        if (!(s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
            write_mask &= freeze_mask_ctrl1;
        }
        s->regs.ctrl = (val & write_mask) | (old_value & ~write_mask);
        break;
    case offsetof(FlexcanRegs, iflag1):
        s->regs.iflag1 &= ~val;
        if ((s->regs.mcr & FLEXCAN_MCR_FEN) &&
            (val & FLEXCAN_IFLAG_RX_FIFO_AVAILABLE)) {
            flexcan_fifo_pop(s);
        }
        break;
    case offsetof(FlexcanRegs, iflag2):
        s->regs.iflag2 &= ~val;
        break;
    case offsetof(FlexcanRegs, ctrl2):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, ecr):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rxmgmask):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rx14mask):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rx15mask):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rxfgmask):
        QEMU_FALLTHROUGH;
    case offsetof(FlexcanRegs, rximr[0]) ... offsetof(FlexcanRegs, rximr[63]):
        /* these registers can only be written in freeze mode */
        if (!(s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
            break;
        }
        QEMU_FALLTHROUGH;
    default:
        s->regs_raw[addr / 4] = (val & write_mask) | (old_value & ~write_mask);

        if (0 <= mbid && mbid < ARRAY_SIZE(s->regs.mbs)) {
            /* access to mailbox */

            if (s->locked_mbidx == mbid) {
                flexcan_mb_unlock(s);
            }

            /* check for invalid writes into FIFO region */
            if (s->regs.mcr & FLEXCAN_MCR_FEN && mbid < FLEXCAN_FIFO_DEPTH) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Invalid write to Rx-FIFO structure",
                              DEVICE(s)->canonical_path);
                return;
            }

            /* run mailbox processing function on write to control word */
            if ((addr & 0xF) == 0) {
                flexcan_mb_write(s, mbid);
            }
        }
        break;
    }

    flexcan_irq_update(s);
}

static uint64_t flexcan_mem_read(void *opqaue, hwaddr addr, unsigned size)
{
    FlexcanState *s = opqaue;
    const int mbid = (addr - offsetof(FlexcanRegs, mbs)) /
        sizeof(s->regs.mbs[0]);
    uint32_t rv = s->regs_raw[addr >> 2];

    if (0 <= mbid && mbid < ARRAY_SIZE(s->regs.mbs)) {
        /* reading from mailbox */
        if (addr % 16 == 0 && s->locked_mbidx != mbid) {
            /* reading control word locks the mailbox */
            flexcan_mb_unlock(s);
            flexcan_mb_lock(s, mbid);
            flexcan_irq_update(s);
            rv = s->regs.mbs[mbid].can_ctrl & ~FLEXCAN_MB_CNT_NOT_SRV;
        }
    } else if (addr == offsetof(FlexcanRegs, timer)) {
        flexcan_mb_unlock(s);
        flexcan_irq_update(s);
        rv = flexcan_get_timestamp(s, false);
    }

    flexcan_trace_mem_op(s, addr, rv, size, false);
    return rv;
}

static bool flexcan_mem_accepts(void *opaque, hwaddr addr,
                                unsigned size, bool is_write,
                                MemTxAttrs attrs)
{
    FlexcanState *s = opaque;

    if ((s->regs.ctrl2 & FLEXCAN_CTRL2_WRMFRZ) &&
        (s->regs.mcr & FLEXCAN_MCR_FRZ_ACK)) {
        /* unrestricted access to FlexCAN memory in freeze mode */
        return true;
    } else if (attrs.user && (s->regs.mcr & FLEXCAN_MCR_SUPV)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid user-mode access to restricted register",
                      DEVICE(s)->canonical_path);
        return false;
    } else if (attrs.user && is_write && addr < 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid user-mode access to MCR",
                      DEVICE(s)->canonical_path);
        return false;
    }

    return true;
}

static const struct MemoryRegionOps flexcan2_ops = {
    .read = flexcan_mem_read,
    .write = flexcan_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
        .accepts = flexcan_mem_accepts
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
};

static const struct MemoryRegionOps flexcan3_ops = {
    .read = flexcan_mem_read,
    .write = flexcan_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
        .accepts = flexcan_mem_accepts
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false
    },
};

static CanBusClientInfo flexcan_bus_client_info = {
    .can_receive = flexcan_can_receive,
    .receive = flexcan_receive,
};

static int flexcan_connect_to_bus(FlexcanState *s, CanBusState *bus)
{
    s->bus_client.info = &flexcan_bus_client_info;

    if (can_bus_insert_client(bus, &s->bus_client) < 0) {
        return -1;
    }
    return 0;
}

static void flexcan2_init(Object *obj)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    memory_region_init_io(
        &s->iomem, obj, &flexcan2_ops, s, TYPE_CAN_FLEXCAN2,
        offsetof(FlexcanRegs, _reserved6)
    );
}

static void flexcan3_init(Object *obj)
{
    FlexcanState *s = CAN_FLEXCAN(obj);

    memory_region_init_io(
        &s->iomem, obj, &flexcan3_ops, s, TYPE_CAN_FLEXCAN3,
        sizeof(FlexcanRegs)
    );
}

static void flexcan_realize(DeviceState *dev, Error **errp)
{
    FlexcanState *s = CAN_FLEXCAN(dev);

    if (s->canbus) {
        if (flexcan_connect_to_bus(s, s->canbus) < 0) {
            error_setg(errp, "%s: flexcan_connect_to_bus failed",
                       dev->canonical_path);
            return;
        }
    }

    if (!s->ccm) {
        error_setg(errp, "%s 'clock-control-module' link property not set",
                   dev->canonical_path);
        return;
    }

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(SYS_BUS_DEVICE(dev)), &s->irq);
}

static const VMStateDescription vmstate_can = {
    .name = TYPE_CAN_FLEXCAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(timer_start, FlexcanState),
        VMSTATE_UINT32_ARRAY(regs_raw, FlexcanState, sizeof(FlexcanRegs) / 4),
        VMSTATE_INT32(locked_mbidx, FlexcanState),
        VMSTATE_INT32(smb_target_mbidx, FlexcanState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property flexcan_properties[] = {
    DEFINE_PROP_LINK("canbus", FlexcanState, canbus, TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_LINK("clock-control-module", FlexcanState, ccm, TYPE_IMX_CCM,
                     IMXCCMState *),
};

static void flexcan_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = flexcan_reset_enter;
    rc->phases.hold = flexcan_reset_hold;
    dc->realize = flexcan_realize;
    device_class_set_props(dc, flexcan_properties);
    dc->vmsd = &vmstate_can;
}

static void flexcan2_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "i.MX FlexCAN 2 Controller";
}

static void flexcan3_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "i.MX FlexCAN 3 Controller";
}

static const TypeInfo flexcan_types[] = {
    {
        .name          = TYPE_CAN_FLEXCAN,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(FlexcanState),
        .class_init    = flexcan_class_init,
        .abstract      = true,
    },
    {
        .name          = TYPE_CAN_FLEXCAN2,
        .parent        = TYPE_CAN_FLEXCAN,
        .class_init    = flexcan2_class_init,
        .instance_init = flexcan2_init,
    },
    {
        .name          = TYPE_CAN_FLEXCAN3,
        .parent        = TYPE_CAN_FLEXCAN,
        .class_init    = flexcan3_class_init,
        .instance_init = flexcan3_init,
    },
};

DEFINE_TYPES(flexcan_types)
