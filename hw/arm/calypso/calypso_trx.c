/*
 * calypso_trx.c — Calypso DSP/TPU/TRX bridge for virtual GSM
 *
 * This module provides the missing peripherals between OsmocomBB L1 firmware
 * and a TRX UDP endpoint (e.g. osmo-bts-trx or a virtual radio bridge).
 *
 * Architecture:
 *
 *   OsmocomBB TRX firmware (in QEMU)
 *       │ writes TX bursts to DSP API RAM
 *       │ programs TPU scenario
 *       │ enables TPU
 *       ▼
 *   calypso_trx.c (this file)
 *       │ intercepts TPU enable → extracts burst from API RAM
 *       │ sends via TRX UDP socket
 *       │ receives RX bursts from TRX UDP
 *       │ injects into API RAM → fires IRQ_API
 *       │ TDMA timer fires IRQ_TPU_FRAME every 4.615 ms
 *       │
 *       │ ★ NEW: ARFCN sync simulation ★
 *       │ Monitors DSP tasks (FB/SB) and simulates:
 *       │   - FCCH detection (frequency burst found)
 *       │   - SCH decode (sync burst with BSIC + FN)
 *       │   - Power measurements
 *       │   - TDMA lock to virtual reference cell
 *       ▼
 *   TRX UDP endpoint (osmo-bts-trx / virtual radio)
 *
 * QEMU 9.2 compatible.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "exec/address-spaces.h"
#include "hw/irq.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#include "calypso_trx.h"

/* =====================================================================
 * Debug logging
 * ===================================================================== */

#define TRX_LOG(fmt, ...) \
    fprintf(stderr, "[calypso-trx] " fmt "\n", ##__VA_ARGS__)

/*
 * Set to 1 for verbose per-access logging.
 * TRX_DEBUG_DSP is the most useful for tuning NDB offsets —
 * it prints every DSP RAM read/write with byte offset and value.
 */
#define TRX_DEBUG_DSP    0
#define TRX_DEBUG_TPU    0
#define TRX_DEBUG_TSP    0
#define TRX_DEBUG_ULPD   0
#define TRX_DEBUG_TDMA   0
#define TRX_DEBUG_SYNC   1   /* FCCH/SCH sync logging (recommended on) */

/* =====================================================================
 * TRX state
 * ===================================================================== */

typedef struct CalypsoTRX {
    /* IRQ lines (borrowed from INTH) */
    qemu_irq *irqs;

    /* ----- DSP API RAM ----- */
    MemoryRegion dsp_iomem;
    uint16_t     dsp_ram[CALYPSO_DSP_SIZE / 2];  /* 64K as 16-bit words */
    uint8_t      dsp_page;                        /* Current DSP page (0/1) */

    /* ----- TPU ----- */
    MemoryRegion tpu_iomem;
    uint16_t     tpu_regs[CALYPSO_TPU_SIZE / 2];
    uint16_t     tpu_ram[1024];                   /* TPU instruction RAM */
    bool         tpu_enabled;

    /* ----- TSP ----- */
    MemoryRegion tsp_iomem;
    uint16_t     tsp_regs[CALYPSO_TSP_SIZE / 2];

    /* ----- ULPD ----- */
    MemoryRegion ulpd_iomem;
    uint16_t     ulpd_regs[CALYPSO_ULPD_SIZE / 2];
    uint32_t     ulpd_counter;

    /* ----- TDMA frame timing ----- */
    QEMUTimer   *tdma_timer;
    uint32_t     fn;              /* GSM frame number */
    bool         tdma_running;

    /* ----- DSP task completion timer ----- */
    QEMUTimer   *dsp_timer;

    /* ----- TRX UDP socket ----- */
    int          trx_fd;          /* Data socket fd (-1 if disabled) */
    int          trx_port;
    struct sockaddr_in trx_remote;
    bool         trx_connected;

    /* ----- Burst buffer ----- */
    uint8_t      tx_burst[GSM_BURST_BITS];
    uint8_t      rx_burst[GSM_BURST_BITS];
    bool         rx_pending;
    uint8_t      rx_tn;
    int8_t       rx_rssi;
    int16_t      rx_toa;

    /* ----- ARFCN Sync state machine ----- */
    SyncState    sync_state;
    uint32_t     sync_fb_countdown;   /* Frames until FB detection */
    uint32_t     sync_sb_countdown;   /* Frames until SB decode */
    uint16_t     sync_arfcn;          /* Reference ARFCN */
    uint8_t      sync_bsic;           /* Fake BSIC */
    int8_t       sync_rssi;           /* Fake RSSI (dBm) */
    uint32_t     sync_ref_fn;         /* Reference FN at lock time */
    uint32_t     sync_task_count;     /* Total tasks seen */
    uint32_t     sync_fb_tasks;       /* FB tasks counted */
    uint32_t     sync_sb_tasks;       /* SB tasks counted */
    bool         sync_dsp_booted;     /* DSP boot sequence complete */
    uint32_t     sync_boot_frame;     /* Frame when boot status polled */
} CalypsoTRX;

static CalypsoTRX *g_trx;  /* Global for timer callbacks */

/* Forward declarations */
static void calypso_dsp_done(void *opaque);
static void calypso_sync_tick(CalypsoTRX *s);

/* =====================================================================
 * DSP API RAM — shared memory between ARM and (virtual) DSP
 *
 * All OsmocomBB firmware variants access DSP through this 64KB window.
 * We intercept reads/writes to simulate DSP behavior.
 * ===================================================================== */

static uint64_t calypso_dsp_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;
    uint64_t val;

    if (offset >= CALYPSO_DSP_SIZE) {
        return 0;
    }

    if (size == 2) {
        val = s->dsp_ram[offset / 2];
    } else if (size == 4) {
        val = s->dsp_ram[offset / 2] |
              ((uint32_t)s->dsp_ram[offset / 2 + 1] << 16);
    } else {
        val = ((uint8_t *)s->dsp_ram)[offset];
    }

    /*
     * DSP boot status polling detection:
     * The firmware polls word 0 (byte 0x0000) waiting for 0x0001 then 0x0002.
     * If we see repeated reads of offset 0, progress the boot state.
     */
    if (offset == DSP_DL_STATUS_ADDR && !s->sync_dsp_booted) {
        s->sync_boot_frame++;
        if (s->sync_boot_frame > 3 &&
            s->dsp_ram[DSP_DL_STATUS_ADDR / 2] == DSP_DL_STATUS_BOOT) {
            /* Firmware has polled enough — transition to READY */
            s->dsp_ram[DSP_DL_STATUS_ADDR / 2] = DSP_DL_STATUS_READY;
            s->dsp_ram[DSP_API_VER_ADDR / 2]   = DSP_API_VERSION;
            s->dsp_ram[DSP_API_VER2_ADDR / 2]  = 0x0000;
            s->sync_dsp_booted = true;
            TRX_LOG("DSP boot: status → 0x0002 (READY), version=0x%04x",
                    DSP_API_VERSION);
            val = DSP_DL_STATUS_READY;
        }
    }

#if TRX_DEBUG_DSP
    TRX_LOG("DSP read  [0x%04x] = 0x%04x (size=%d)", (unsigned)offset,
            (unsigned)val, size);
#endif
    return val;
}

static void calypso_dsp_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;

    if (offset >= CALYPSO_DSP_SIZE) {
        return;
    }

#if TRX_DEBUG_DSP
    TRX_LOG("DSP write [0x%04x] = 0x%04x (size=%d)", (unsigned)offset,
            (unsigned)value, size);
#endif

    if (size == 2) {
        s->dsp_ram[offset / 2] = (uint16_t)value;
    } else if (size == 4) {
        s->dsp_ram[offset / 2] = (uint16_t)value;
        s->dsp_ram[offset / 2 + 1] = (uint16_t)(value >> 16);
    } else {
        ((uint8_t *)s->dsp_ram)[offset] = (uint8_t)value;
    }

    /* Track DSP page changes in NDB area */
    if (offset == DSP_API_NDB + NDB_W_D_DSP_PAGE * 2) {
        s->dsp_page = value & 1;
    }

    /*
     * Detect DSP boot sequence writes:
     * Firmware writes to PARAM area or specific NDB fields during DSP init.
     * When it writes to the download trigger location, advance boot status.
     */
    if (offset == DSP_DL_STATUS_ADDR && value == DSP_DL_STATUS_BOOT) {
        /* Firmware acknowledging boot — we'll transition to READY on next read */
        s->sync_boot_frame = 0;
    }
}

static const MemoryRegionOps calypso_dsp_ops = {
    .read = calypso_dsp_read,
    .write = calypso_dsp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* =====================================================================
 * TRX UDP — send TX burst, receive RX burst
 * ===================================================================== */

static void trx_send_burst(CalypsoTRX *s, uint8_t tn, uint32_t fn,
                            uint8_t *bits, int nbits)
{
    uint8_t pkt[TRX_PKT_LEN_TX];

    if (s->trx_fd < 0 || !s->trx_connected) {
        return;
    }

    pkt[0] = tn;
    pkt[1] = (fn >> 24) & 0xFF;
    pkt[2] = (fn >> 16) & 0xFF;
    pkt[3] = (fn >> 8)  & 0xFF;
    pkt[4] = fn & 0xFF;
    pkt[5] = 0;  /* TX power attenuation */

    int copy = (nbits > GSM_BURST_BITS) ? GSM_BURST_BITS : nbits;
    memcpy(&pkt[TRX_HDR_LEN_TX], bits, copy);
    if (copy < GSM_BURST_BITS) {
        memset(&pkt[TRX_HDR_LEN_TX + copy], 0, GSM_BURST_BITS - copy);
    }

    sendto(s->trx_fd, pkt, TRX_PKT_LEN_TX, 0,
           (struct sockaddr *)&s->trx_remote, sizeof(s->trx_remote));
}

static void trx_receive_cb(void *opaque)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;
    uint8_t buf[512];
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);

    ssize_t n = recvfrom(s->trx_fd, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &slen);
    if (n < TRX_HDR_LEN_RX + 1) {
        return;
    }

    /* Remember remote for TX responses */
    if (!s->trx_connected) {
        s->trx_remote = src;
        s->trx_connected = true;
        TRX_LOG("TRX connected from %s:%d",
                inet_ntoa(src.sin_addr), ntohs(src.sin_port));
    }

    /* Parse RX burst (downlink to phone) */
    s->rx_tn   = buf[0];
    s->rx_rssi = (int8_t)buf[5];
    s->rx_toa  = (int16_t)((buf[6] << 8) | buf[7]);

    int burst_len = n - TRX_HDR_LEN_RX;
    if (burst_len > GSM_BURST_BITS) burst_len = GSM_BURST_BITS;
    memcpy(s->rx_burst, &buf[TRX_HDR_LEN_RX], burst_len);
    s->rx_pending = true;

#if TRX_DEBUG_TDMA
    TRX_LOG("TRX RX burst TN=%d RSSI=%d len=%d", s->rx_tn, s->rx_rssi,
            burst_len);
#endif
}

static void trx_socket_init(CalypsoTRX *s, int port)
{
    struct sockaddr_in addr;

    s->trx_port = port;
    s->trx_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (s->trx_fd < 0) {
        TRX_LOG("WARNING: Cannot create TRX socket: %s", strerror(errno));
        return;
    }

    int reuse = 1;
    setsockopt(s->trx_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(s->trx_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        TRX_LOG("WARNING: Cannot bind TRX port %d: %s", port, strerror(errno));
        close(s->trx_fd);
        s->trx_fd = -1;
        return;
    }

    /* Default remote: localhost:port+100 */
    memset(&s->trx_remote, 0, sizeof(s->trx_remote));
    s->trx_remote.sin_family = AF_INET;
    s->trx_remote.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    s->trx_remote.sin_port = htons(port + 100);

    qemu_set_fd_handler(s->trx_fd, trx_receive_cb, NULL, s);
    TRX_LOG("TRX UDP listening on port %d (send to %d)", port, port + 100);
}

/* =====================================================================
 * ARFCN sync simulation — FCCH / SCH state machine
 *
 * This is the core addition for making TRX firmware sync work.
 *
 * Flow on real hardware:
 *   1) Firmware sends L1CTL_FBSB_REQ → tunes to ARFCN
 *   2) L1 programs DSP for FB task (d_task_d = FB code)
 *   3) DSP searches for FCCH over up to 12 frames
 *   4) If found: d_fb_det=1, a_cd[TOA,PM,ANGLE,SNR] filled
 *   5) L1 programs DSP for SB task (d_task_d = SB code)
 *   6) DSP decodes SCH: a_sch26[5] filled with BSIC+FN
 *   7) Firmware achieves TDMA lock
 *
 * We simulate this by watching d_task_d writes and injecting
 * results into the NDB after appropriate delays.
 * ===================================================================== */

/*
 * Inject FB detection result into NDB.
 * Called when sync_state transitions to FCCH_FOUND.
 */
static void sync_inject_fb_result(CalypsoTRX *s)
{
    uint16_t *ndb = &s->dsp_ram[DSP_API_NDB / 2];

    /* d_fb_det = 1 → firmware sees "frequency burst found" */
    ndb[NDB_W_D_FB_DET] = 1;

    /* a_cd[]: carrier demod results */
    ndb[NDB_W_A_CD_TOA]   = 384;     /* TOA in quarter-bits (typical) */
    ndb[NDB_W_A_CD_PM]    = (uint16_t)((s->sync_rssi + 110) * 64);
                                       /* PM in 1/64 dBm, biased */
    ndb[NDB_W_A_CD_ANGLE] = 500;     /* Small freq offset (Hz) */
    ndb[NDB_W_A_CD_SNR]   = 2048;    /* ~2 dB SNR in fx6.10 */

#if TRX_DEBUG_SYNC
    TRX_LOG("SYNC: FB detected! TOA=%d PM=%d ANGLE=%d SNR=%d (FN=%u)",
            ndb[NDB_W_A_CD_TOA], ndb[NDB_W_A_CD_PM],
            ndb[NDB_W_A_CD_ANGLE], ndb[NDB_W_A_CD_SNR], s->fn);
#endif
}

/*
 * Inject SCH decode result into NDB.
 * Called when sync_state transitions to LOCKED.
 */
static void sync_inject_sb_result(CalypsoTRX *s)
{
    uint16_t *ndb = &s->dsp_ram[DSP_API_NDB / 2];

    /* Save reference FN at the moment of lock */
    s->sync_ref_fn = s->fn;

    /* Encode SCH data: BSIC + frame number → a_sch26[5] */
    uint16_t sch26[5];
    sch_encode(sch26, s->sync_bsic, s->fn);

    for (int i = 0; i < NDB_W_A_SCH26_LEN; i++) {
        ndb[NDB_W_A_SCH26 + i] = sch26[i];
    }

    /* Update a_cd with SB-specific results */
    ndb[NDB_W_A_CD_TOA]   = 27;       /* Fine TOA (quarter-bits) */
    ndb[NDB_W_A_CD_PM]    = (uint16_t)((s->sync_rssi + 110) * 64);
    ndb[NDB_W_A_CD_ANGLE] = 431;      /* Residual freq offset */
    ndb[NDB_W_A_CD_SNR]   = 4096;     /* Better SNR for SB */

    /* Decode for debug logging */
    uint32_t t1  = s->fn / (26 * 51);
    uint32_t t2  = s->fn % 26;
    uint32_t t3  = s->fn % 51;

#if TRX_DEBUG_SYNC
    TRX_LOG("SYNC: SCH decoded! BSIC=%d(NCC=%d,BCC=%d) FN=%u "
            "T1=%u T2=%u T3=%u",
            s->sync_bsic, (s->sync_bsic >> 3) & 7, s->sync_bsic & 7,
            s->fn, t1, t2, t3);
    TRX_LOG("SYNC: a_sch26 = [0x%04x 0x%04x 0x%04x 0x%04x 0x%04x]",
            sch26[0], sch26[1], sch26[2], sch26[3], sch26[4]);
#endif
}

/*
 * calypso_sync_tick() — called every TDMA frame to advance sync state.
 *
 * The state machine monitors DSP tasks written by the firmware
 * and injects appropriate results after configured delays.
 */
static void calypso_sync_tick(CalypsoTRX *s)
{
    uint16_t *ndb = &s->dsp_ram[DSP_API_NDB / 2];

    switch (s->sync_state) {

    case SYNC_IDLE:
        /*
         * Check if firmware is requesting FB search.
         * We detect this by looking for non-zero d_task_d in the
         * active write page.  The firmware writes the task code
         * and then enables TPU.
         */
        break;

    case SYNC_FCCH_SEARCH:
        /* Count down frames until we "detect" the FCCH */
        if (s->sync_fb_countdown > 0) {
            s->sync_fb_countdown--;
#if TRX_DEBUG_SYNC
            if (s->sync_fb_countdown == 0) {
                TRX_LOG("SYNC: FCCH countdown reached zero → injecting FB");
            }
#endif
        }
        if (s->sync_fb_countdown == 0) {
            /* Inject FB detection result */
            sync_inject_fb_result(s);
            s->sync_state = SYNC_FCCH_FOUND;
            TRX_LOG("SYNC: state → FCCH_FOUND (FN=%u)", s->fn);
        }
        break;

    case SYNC_FCCH_FOUND:
        /*
         * FB was detected.  Firmware should now read d_fb_det,
         * then program an SB task to decode the SCH.
         * We wait for the SB task to appear.
         */
        break;

    case SYNC_SCH_SEARCH:
        /* Count down frames until we "decode" the SCH */
        if (s->sync_sb_countdown > 0) {
            s->sync_sb_countdown--;
        }
        if (s->sync_sb_countdown == 0) {
            /* Inject SCH decode result */
            sync_inject_sb_result(s);
            s->sync_state = SYNC_LOCKED;
            TRX_LOG("SYNC: ★ TDMA LOCKED ★ ARFCN=%d BSIC=%d FN=%u",
                    s->sync_arfcn, s->sync_bsic, s->fn);
        }
        break;

    case SYNC_LOCKED:
        /*
         * Maintain lock: update FN in NDB, provide PM results.
         * The firmware reads d_fn to track time.
         */
        ndb[NDB_W_D_FN] = (uint16_t)(s->fn & 0xFFFF);
        break;
    }
}

/*
 * Detect DSP task type from d_task_d value.
 *
 * The Calypso DSP task encoding varies, but the OsmocomBB firmware
 * uses these identifiers (from tdma_sched.h):
 *
 *   Task code | Type
 *   ----------+------------------
 *   0         | No task
 *   1-3       | TCH (traffic)
 *   4         | FB (frequency burst) ← FCCH detection
 *   5         | SB (sync burst) ← SCH decode
 *   6-7       | TCH_FB, TCH_SB (dedicated)
 *   8         | RACH
 *   9         | EXT
 *   10        | NB (normal burst)
 *   11        | ALLC
 *   12-14     | FB26, SB26, NB26
 *   15        | DDL
 *
 * The d_task_d word in the DB write page typically contains the
 * task code in the lower bits, plus tsc/flags in upper bits.
 * We check bits [3:0] for the basic task type.
 *
 * NOTE: The actual numeric values depend on the firmware build.
 * If sync doesn't work, enable TRX_DEBUG_DSP and check what values
 * your firmware writes to d_task_d.
 */

typedef enum {
    TASK_NONE = 0,
    TASK_FB,        /* Frequency burst (FCCH) search */
    TASK_SB,        /* Sync burst (SCH) decode */
    TASK_NB,        /* Normal burst */
    TASK_RACH,      /* Random access */
    TASK_OTHER,     /* Anything else */
} TaskType;

static TaskType detect_task_type(uint16_t task_d)
{
    if (task_d == 0) return TASK_NONE;

    /*
     * Heuristic detection based on known OsmocomBB task codes.
     *
     * The standard mapping uses the task ID as an index.
     * But the actual d_task_d value written to DSP may encode it
     * differently.  We try several common patterns:
     *
     * Pattern 1: Direct task ID in lower nibble
     *   FB=4, SB=5, NB=10
     *
     * Pattern 2: Bit-field encoding
     *   d_task_d = (tsc << 5) | (bcch_freq << 3) | task_code
     *   where task_code: FB=1, SB=2, NB=5, etc.
     *
     * Pattern 3: The "d_task_d" field actually contains the
     *   DSP task command directly (0x000D for FB, 0x001C for SB, etc.)
     *
     * We check all patterns and also look at the raw value.
     */

    uint8_t lo_nib = task_d & 0x0F;
    uint8_t lo3    = task_d & 0x07;

    /* Pattern 1: Standard task IDs */
    if (lo_nib == 4 || lo_nib == 12)  return TASK_FB;  /* FB_TASK or FB26_TASK */
    if (lo_nib == 5 || lo_nib == 13)  return TASK_SB;  /* SB_TASK or SB26_TASK */
    if (lo_nib == 10 || lo_nib == 14) return TASK_NB;  /* NB_TASK or NB26_TASK */
    if (lo_nib == 8)                  return TASK_RACH;

    /* Pattern 2: Lower 3-bit encoding */
    if (lo3 == 1 && task_d < 0x20) return TASK_FB;
    if (lo3 == 2 && task_d < 0x20) return TASK_SB;

    /* Pattern 3: Known DSP command values */
    if (task_d == 0x000D) return TASK_FB;
    if (task_d == 0x000E) return TASK_FB;  /* FB1 (confirm) */
    if (task_d == 0x001C) return TASK_SB;

    /* Default: non-zero = some active task */
    return TASK_OTHER;
}

/* =====================================================================
 * DSP task processing — extract/inject bursts + sync handling
 * ===================================================================== */

static void calypso_dsp_process(CalypsoTRX *s)
{
    uint16_t *w_page, *r_page, *ndb;
    uint16_t task_d, task_u;

    /* Determine active pages */
    if (s->dsp_page == 0) {
        w_page = &s->dsp_ram[DSP_API_W_PAGE0 / 2];
        r_page = &s->dsp_ram[DSP_API_R_PAGE0 / 2];
    } else {
        w_page = &s->dsp_ram[DSP_API_W_PAGE1 / 2];
        r_page = &s->dsp_ram[DSP_API_R_PAGE1 / 2];
    }
    ndb = &s->dsp_ram[DSP_API_NDB / 2];

    /* Read task words from write page header */
    task_d = w_page[DB_W_D_TASK_D];
    task_u = w_page[DB_W_D_TASK_U];

    if (task_d != 0 || task_u != 0) {
        s->sync_task_count++;
    }

    /* ---- Classify the DL task for sync handling ---- */
    TaskType ttype = detect_task_type(task_d);

    switch (ttype) {
    case TASK_FB:
        s->sync_fb_tasks++;
#if TRX_DEBUG_SYNC
        TRX_LOG("SYNC: FB task detected (d_task_d=0x%04x, count=%u, "
                "state=%d, FN=%u)",
                task_d, s->sync_fb_tasks, s->sync_state, s->fn);
#endif
        if (s->sync_state == SYNC_IDLE ||
            s->sync_state == SYNC_FCCH_SEARCH) {
            if (s->sync_state == SYNC_IDLE) {
                /* First FB task — start FCCH search */
                s->sync_fb_countdown = SYNC_FB_DETECT_DELAY;
                s->sync_state = SYNC_FCCH_SEARCH;
                TRX_LOG("SYNC: state → FCCH_SEARCH "
                        "(will detect in %d frames)",
                        SYNC_FB_DETECT_DELAY);
            }
            /* else: already searching, countdown handled in sync_tick */
        }
        break;

    case TASK_SB:
        s->sync_sb_tasks++;
#if TRX_DEBUG_SYNC
        TRX_LOG("SYNC: SB task detected (d_task_d=0x%04x, count=%u, "
                "state=%d, FN=%u)",
                task_d, s->sync_sb_tasks, s->sync_state, s->fn);
#endif
        if (s->sync_state == SYNC_FCCH_FOUND) {
            /* FB was found, now searching for SB */
            s->sync_sb_countdown = SYNC_SB_DECODE_DELAY;
            s->sync_state = SYNC_SCH_SEARCH;
            TRX_LOG("SYNC: state → SCH_SEARCH "
                    "(will decode in %d frames)",
                    SYNC_SB_DECODE_DELAY);
        }
        break;

    case TASK_NB:
        /* Normal burst — handle TX/RX when locked */
        if (s->sync_state == SYNC_LOCKED) {
            /* RX: inject burst from TRX UDP or silence */
            if (s->rx_pending) {
                uint16_t *burst_r = &r_page[0x19];
                for (int i = 0; i < GSM_BURST_BITS; i++) {
                    burst_r[i] = s->rx_burst[i];
                }
                r_page[0] = 1;   /* d_bursttype: normal */
                r_page[1] = 0;   /* d_result: OK */
                s->rx_pending = false;
            } else {
                /* Provide noise/empty burst */
                uint16_t *burst_r = &r_page[0x19];
                for (int i = 0; i < GSM_BURST_BITS; i++) {
                    burst_r[i] = 128;  /* erasure */
                }
                r_page[0] = 0;
                r_page[1] = 0;
            }
        }
        break;

    default:
        break;
    }

    /* ---- Handle TX (uplink) burst ---- */
    if (task_u != 0 && s->sync_state == SYNC_LOCKED) {
        uint16_t *burst_w = &w_page[0x19];
        uint8_t bits[GSM_BURST_BITS];

        for (int i = 0; i < GSM_BURST_BITS && i < GSM_BURST_WORDS * 2; i++) {
            if (i < 78) {
                bits[i] = burst_w[i] & 1;
            } else {
                bits[i] = burst_w[78 + (i - 78)] & 1;
            }
        }

        trx_send_burst(s, 0, s->fn, bits, GSM_BURST_BITS);

#if TRX_DEBUG_TDMA
        TRX_LOG("TX burst FN=%u task_u=0x%04x", s->fn, task_u);
#endif
    }

    /* Clear task words (DSP "consumed" them) */
    w_page[DB_W_D_TASK_D] = 0;
    w_page[DB_W_D_TASK_U] = 0;

    /* Write frame number to NDB */
    ndb[NDB_W_D_FN] = (uint16_t)(s->fn & 0xFFFF);
}

/* DSP completion timer callback */
static void calypso_dsp_done(void *opaque)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;

    /* Fire DSP API interrupt — wakes up firmware to read results */
    qemu_irq_pulse(s->irqs[CALYPSO_IRQ_API]);
}

/* =====================================================================
 * TPU — Time Processing Unit
 * ===================================================================== */

static uint64_t calypso_tpu_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;
    uint64_t val = 0;

    switch (offset) {
    case TPU_CTRL:
        val = s->tpu_regs[TPU_CTRL / 2];
        break;
    case TPU_IDLE:
        val = 1;  /* Always idle (processing is instant) */
        break;
    case TPU_INT_CTRL:
        val = s->tpu_regs[TPU_INT_CTRL / 2];
        break;
    case TPU_INT_STAT:
        val = 0;
        break;
    case TPU_DSP_PAGE:
        val = s->dsp_page;
        break;
    case TPU_FRAME:
        val = (uint16_t)(s->fn % 2715648);
        break;
    case TPU_OFFSET:
        val = s->tpu_regs[TPU_OFFSET / 2];
        break;
    case TPU_SYNCHRO:
        val = s->tpu_regs[TPU_SYNCHRO / 2];
        break;
    default:
        if (offset >= TPU_RAM_BASE &&
            offset < TPU_RAM_BASE + sizeof(s->tpu_ram)) {
            val = s->tpu_ram[(offset - TPU_RAM_BASE) / 2];
        } else if (offset / 2 < CALYPSO_TPU_SIZE / 2) {
            val = s->tpu_regs[offset / 2];
        }
        break;
    }

#if TRX_DEBUG_TPU
    TRX_LOG("TPU read  [0x%04x] = 0x%04x", (unsigned)offset, (unsigned)val);
#endif
    return val;
}

static void calypso_tpu_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;

#if TRX_DEBUG_TPU
    TRX_LOG("TPU write [0x%04x] = 0x%04x", (unsigned)offset, (unsigned)value);
#endif

    /* Store register */
    if (offset / 2 < CALYPSO_TPU_SIZE / 2) {
        s->tpu_regs[offset / 2] = (uint16_t)value;
    }

    /* TPU instruction RAM */
    if (offset >= TPU_RAM_BASE &&
        offset < TPU_RAM_BASE + sizeof(s->tpu_ram)) {
        s->tpu_ram[(offset - TPU_RAM_BASE) / 2] = (uint16_t)value;
        return;
    }

    switch (offset) {
    case TPU_CTRL:
        if ((value & TPU_CTRL_ENABLE) && !s->tpu_enabled) {
            /* TPU enabled — firmware triggered DSP processing */
            s->tpu_enabled = true;

            /* Process DSP tasks (sync detection + burst handling) */
            calypso_dsp_process(s);

            /* Schedule DSP completion IRQ after small delay (10 µs) */
            timer_mod_ns(s->dsp_timer,
                         qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 10000);
        }
        if (value & TPU_CTRL_RESET) {
            s->tpu_enabled = false;
        }
        break;

    case TPU_OFFSET:
    case TPU_SYNCHRO:
        break;

    case TPU_INT_CTRL:
        break;

    case TPU_DSP_PAGE:
        s->dsp_page = value & 1;
        break;
    }
}

static const MemoryRegionOps calypso_tpu_ops = {
    .read = calypso_tpu_read,
    .write = calypso_tpu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

/* =====================================================================
 * TSP — Time Serial Port (RF transceiver control)
 * ===================================================================== */

static uint64_t calypso_tsp_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;
    uint64_t val = 0;

    switch (offset) {
    case TSP_RX_REG:
        val = 0xFFFF;
        break;
    case TSP_CTRL1:
        val = s->tsp_regs[TSP_CTRL1 / 2];
        break;
    default:
        if (offset / 2 < CALYPSO_TSP_SIZE / 2) {
            val = s->tsp_regs[offset / 2];
        }
        break;
    }

#if TRX_DEBUG_TSP
    TRX_LOG("TSP read  [0x%02x] = 0x%04x", (unsigned)offset, (unsigned)val);
#endif
    return val;
}

static void calypso_tsp_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;

#if TRX_DEBUG_TSP
    TRX_LOG("TSP write [0x%02x] = 0x%04x", (unsigned)offset, (unsigned)value);
#endif

    if (offset / 2 < CALYPSO_TSP_SIZE / 2) {
        s->tsp_regs[offset / 2] = (uint16_t)value;
    }
}

static const MemoryRegionOps calypso_tsp_ops = {
    .read = calypso_tsp_read,
    .write = calypso_tsp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

/* =====================================================================
 * ULPD — Ultra Low Power Down (clocks, gauging, GSM timer)
 * ===================================================================== */

static uint64_t calypso_ulpd_read(void *opaque, hwaddr offset, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;
    uint64_t val = 0;

    switch (offset) {
    case ULPD_SETUP_CLK13:
        val = 0x2003;  /* CLK13: enabled, stable */
        break;
    case ULPD_SETUP_SLICER:
    case ULPD_SETUP_VTCXO:
        val = 0;
        break;
    case ULPD_COUNTER_HI:
        s->ulpd_counter += 100;
        val = (s->ulpd_counter >> 16) & 0xFFFF;
        break;
    case ULPD_COUNTER_LO:
        val = s->ulpd_counter & 0xFFFF;
        break;
    case ULPD_GAUGING_CTRL:
        val = 0x0001;  /* Gauging complete */
        break;
    case ULPD_GSM_TIMER:
        val = (uint16_t)(s->fn & 0xFFFF);
        break;
    default:
        if (offset / 2 < CALYPSO_ULPD_SIZE / 2) {
            val = s->ulpd_regs[offset / 2];
        }
        break;
    }

#if TRX_DEBUG_ULPD
    TRX_LOG("ULPD read  [0x%02x] = 0x%04x", (unsigned)offset, (unsigned)val);
#endif
    return val;
}

static void calypso_ulpd_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;

#if TRX_DEBUG_ULPD
    TRX_LOG("ULPD write [0x%02x] = 0x%04x", (unsigned)offset, (unsigned)value);
#endif

    if (offset / 2 < CALYPSO_ULPD_SIZE / 2) {
        s->ulpd_regs[offset / 2] = (uint16_t)value;
    }
}

static const MemoryRegionOps calypso_ulpd_ops = {
    .read = calypso_ulpd_read,
    .write = calypso_ulpd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

/* =====================================================================
 * TDMA frame timer — drives L1 at 4.615 ms per frame
 * ===================================================================== */

static void calypso_tdma_tick(void *opaque)
{
    CalypsoTRX *s = (CalypsoTRX *)opaque;

    /* Advance frame number */
    s->fn = (s->fn + 1) % GSM_HYPERFRAME;

    /* Reset TPU enabled flag (new frame, new scenario needed) */
    s->tpu_enabled = false;
    s->tpu_regs[TPU_CTRL / 2] &= ~TPU_CTRL_ENABLE;

    /* Run sync state machine */
    calypso_sync_tick(s);

#if TRX_DEBUG_TDMA
    if ((s->fn % 5000) == 0) {
        TRX_LOG("TDMA FN=%u sync=%d tasks=%u fb=%u sb=%u",
                s->fn, s->sync_state, s->sync_task_count,
                s->sync_fb_tasks, s->sync_sb_tasks);
    }
#endif

    /* Fire TPU frame interrupt — this wakes up L1 */
    qemu_irq_pulse(s->irqs[CALYPSO_IRQ_TPU_FRAME]);

    /* Schedule next frame */
    if (s->tdma_running) {
        timer_mod_ns(s->tdma_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + GSM_TDMA_NS);
    }
}

/* =====================================================================
 * Start TDMA and sync
 * ===================================================================== */

static void calypso_tdma_start(CalypsoTRX *s)
{
    if (s->tdma_running) return;
    s->tdma_running = true;
    s->fn = 0;
    TRX_LOG("TDMA started (4.615ms frame timer)");
    timer_mod_ns(s->tdma_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + GSM_TDMA_NS);
}

/* =====================================================================
 * DSP API RAM initialization
 * ===================================================================== */

static void calypso_dsp_api_init(CalypsoTRX *s)
{
    memset(s->dsp_ram, 0, sizeof(s->dsp_ram));

    /*
     * DSP boot status — firmware polls word 0 of API RAM.
     * Start at BOOT (0x0001); we'll transition to READY
     * when the firmware has polled enough times (see dsp_read).
     */
    s->dsp_ram[DSP_DL_STATUS_ADDR / 2] = DSP_DL_STATUS_BOOT;
    s->dsp_ram[DSP_API_VER_ADDR / 2]   = 0x0000;
    s->dsp_ram[DSP_API_VER2_ADDR / 2]  = 0x0000;

    /* NDB: page=0, no tasks, FN=0 */
    s->dsp_page = 0;
    s->dsp_ram[DSP_API_NDB / 2 + NDB_W_D_DSP_PAGE] = 0;
    s->dsp_ram[DSP_API_NDB / 2 + NDB_W_D_FN]       = 0;

    /* d_fb_det = 0 (no FB detected yet) */
    s->dsp_ram[DSP_API_NDB / 2 + NDB_W_D_FB_DET]   = 0;

    TRX_LOG("DSP API RAM initialized (%d KiB at 0x%08x)",
            CALYPSO_DSP_SIZE / 1024, CALYPSO_DSP_BASE);
    TRX_LOG("  Boot status: 0x%04x at byte offset 0x%04x",
            s->dsp_ram[0], DSP_DL_STATUS_ADDR);
}

/* =====================================================================
 * Sync state initialization
 * ===================================================================== */

static void calypso_sync_init(CalypsoTRX *s)
{
    s->sync_state       = SYNC_IDLE;
    s->sync_fb_countdown = 0;
    s->sync_sb_countdown = 0;
    s->sync_arfcn       = SYNC_DEFAULT_ARFCN;
    s->sync_bsic        = SYNC_DEFAULT_BSIC;
    s->sync_rssi        = SYNC_DEFAULT_RSSI;
    s->sync_ref_fn      = 0;
    s->sync_task_count  = 0;
    s->sync_fb_tasks    = 0;
    s->sync_sb_tasks    = 0;
    s->sync_dsp_booted  = false;
    s->sync_boot_frame  = 0;

    TRX_LOG("Sync init: ARFCN=%d BSIC=0x%02x(%d,%d) RSSI=%d dBm",
            s->sync_arfcn, s->sync_bsic,
            (s->sync_bsic >> 3) & 7, s->sync_bsic & 7,
            s->sync_rssi);
    TRX_LOG("  FB detect delay: %d frames", SYNC_FB_DETECT_DELAY);
    TRX_LOG("  SB decode delay: %d frames", SYNC_SB_DECODE_DELAY);
    TRX_LOG("  NDB offsets: d_fb_det=w%d a_cd=w%d-%d a_sch26=w%d-%d",
            NDB_W_D_FB_DET,
            NDB_W_A_CD_TOA, NDB_W_A_CD_SNR,
            NDB_W_A_SCH26, NDB_W_A_SCH26 + NDB_W_A_SCH26_LEN - 1);
}

/* =====================================================================
 * calypso_trx_init() — Main entry point
 * ===================================================================== */

void calypso_trx_init(MemoryRegion *sysmem, qemu_irq *irqs, int trx_port)
{
    CalypsoTRX *s = g_new0(CalypsoTRX, 1);
    g_trx = s;
    s->irqs = irqs;
    s->trx_fd = -1;

    TRX_LOG("=== Calypso TRX bridge init (with ARFCN sync) ===");

    /* ---- DSP API RAM ---- */
    memory_region_init_io(&s->dsp_iomem, NULL, &calypso_dsp_ops, s,
                          "calypso.dsp_api", CALYPSO_DSP_SIZE);
    memory_region_add_subregion(sysmem, CALYPSO_DSP_BASE, &s->dsp_iomem);
    calypso_dsp_api_init(s);

    /* ---- Sync state ---- */
    calypso_sync_init(s);

    /* ---- TPU ---- */
    memory_region_init_io(&s->tpu_iomem, NULL, &calypso_tpu_ops, s,
                          "calypso.tpu", CALYPSO_TPU_SIZE);
    memory_region_add_subregion(sysmem, CALYPSO_TPU_BASE, &s->tpu_iomem);

    /* ---- TSP ---- */
    memory_region_init_io(&s->tsp_iomem, NULL, &calypso_tsp_ops, s,
                          "calypso.tsp", CALYPSO_TSP_SIZE);
    memory_region_add_subregion(sysmem, CALYPSO_TSP_BASE, &s->tsp_iomem);

    /* ---- ULPD ---- */
    memory_region_init_io(&s->ulpd_iomem, NULL, &calypso_ulpd_ops, s,
                          "calypso.ulpd", CALYPSO_ULPD_SIZE);
    memory_region_add_subregion(sysmem, CALYPSO_ULPD_BASE, &s->ulpd_iomem);

    /* ---- TDMA frame timer ---- */
    s->tdma_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, calypso_tdma_tick, s);

    /* ---- DSP completion timer ---- */
    s->dsp_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, calypso_dsp_done, s);

    /* ---- TRX UDP socket ---- */
    if (trx_port > 0) {
        trx_socket_init(s, trx_port);
    } else {
        TRX_LOG("TRX UDP disabled");
    }

    /* ---- Auto-start TDMA ---- */
    calypso_tdma_start(s);

    TRX_LOG("=== TRX bridge ready ===");
    TRX_LOG("  DSP API:  0x%08x (%d KiB)", CALYPSO_DSP_BASE,
            CALYPSO_DSP_SIZE / 1024);
    TRX_LOG("  TPU:      0x%08x", CALYPSO_TPU_BASE);
    TRX_LOG("  TSP:      0x%08x", CALYPSO_TSP_BASE);
    TRX_LOG("  ULPD:     0x%08x", CALYPSO_ULPD_BASE);
    TRX_LOG("  TDMA:     4.615ms → IRQ %d", CALYPSO_IRQ_TPU_FRAME);
    TRX_LOG("  DSP done: → IRQ %d", CALYPSO_IRQ_API);
    TRX_LOG("  Sync:     ARFCN=%d BSIC=%d", s->sync_arfcn, s->sync_bsic);
    if (s->trx_fd >= 0) {
        TRX_LOG("  TRX UDP:  port %d", trx_port);
    }
}
