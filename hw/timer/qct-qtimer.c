/*
 * Qualcomm QCT QTimer
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/timer/qct-qtimer.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "trace.h"

#define QTIMER_MEM_SIZE_BYTES 0x1000
#define QTIMER_DEFAULT_FREQ_HZ 19200000ULL

#define QCT_QTIMER_TIMER_FRAME_ELTS (16)
#define QCT_QTIMER_TIMER_VIEW_ELTS (2)

#define QCT_QTIMER_AC_CNTFRQ (0x000)
#define QCT_QTIMER_AC_CNTSR (0x004)
#define QCT_QTIMER_AC_CNTTID_0 (0x08)
#define QCT_QTIMER_AC_CNTACR_START (0x40)
#define QCT_QTIMER_AC_CNTACR_END (0x5c)
#define QCT_QTIMER_AC_CNTTID_1 (0x108)
#define QCT_QTIMER_AC_CNTACR_RWPT (1 << 5) /* R/W of CNTP_* regs */
#define QCT_QTIMER_AC_CNTACR_RWVT (1 << 4) /* R/W of CNTV_* regs */
#define QCT_QTIMER_AC_CNTACR_RVOFF (1 << 3) /* R/W of CNTVOFF register */
#define QCT_QTIMER_AC_CNTACR_RFRQ (1 << 2) /* R/W of CNTFRQ register */
#define QCT_QTIMER_AC_CNTACR_RPVCT (1 << 1) /* R/W of CNTVCT register */
#define QCT_QTIMER_AC_CNTACR_RPCT (1 << 0) /* R/W of CNTPCT register */
#define QCT_QTIMER_VERSION (0x0fd0)

#define QCT_QTIMER_CNTPCT_LO (0x000)
#define QCT_QTIMER_CNTPCT_HI (0x004)
#define QCT_QTIMER_CNT_FREQ (0x010)
#define QCT_QTIMER_CNTPL0ACR (0x014)
#define QCT_QTIMER_CNTPL0ACR_PL0CTEN (1 << 9)
#define QCT_QTIMER_CNTPL0ACR_PL0TVEN (1 << 8)
#define QCT_QTIMER_CNTPL0ACR_PL0VCTEN (1 << 1)
#define QCT_QTIMER_CNTPL0ACR_PL0PCTEN (1 << 0)
#define QCT_QTIMER_CNTP_CVAL_LO (0x020)
#define QCT_QTIMER_CNTP_CVAL_HI (0x024)
#define QCT_QTIMER_CNT_MASK 0x00ffffffffffffffULL
#define QCT_QTIMER_CNT_HI_BITS 24
#define QCT_QTIMER_CNTP_TVAL (0x028)
#define QCT_QTIMER_CNTP_CTL (0x02c)
#define QCT_QTIMER_CNTP_CTL_ENABLE (1 << 0)
#define QCT_QTIMER_CNTP_CTL_INTEN (1 << 1)
#define QCT_QTIMER_CNTP_CTL_ISTAT (1 << 2)

OBJECT_DECLARE_SIMPLE_TYPE(QCTQtimerState, QCT_QTIMER)

typedef struct QCTHextimerState {
    QCTQtimerState *qtimer;
    QEMUTimer *timer;       /* one-shot deadline timer */
    int64_t offset_ns;      /* QEMU_CLOCK_VIRTUAL ns at which cntpct == 0 */
    uint64_t cntval;        /* 64-bit physical timer compare value */
    uint32_t control;
    uint32_t cnt_ctrl;
    uint32_t cntpl0acr;
    uint32_t int_level;
    qemu_irq irq;
} QCTHextimerState;

struct QCTQtimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion view_iomem;
    uint32_t secure;
    QCTHextimerState timer[QCT_QTIMER_TIMER_FRAME_ELTS];
    uint32_t freq_hz;
    uint32_t nr_frames;
    uint32_t nr_views;
    uint32_t frame_stride;
    uint32_t freq_scale;
};

/*
 * QTimer version register:
 *
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Major |         Minor         |           Step                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define QCT_QTIMER_VERSION_VALUE 0x20020000

static uint32_t qct_qtimer_cnttid(QCTQtimerState *s, unsigned int half)
{
    uint32_t nibble = 0x1 | (s->nr_views > 1 ? 0x4 : 0x0);
    uint32_t base = half * 8;
    uint32_t cnttid = 0;
    unsigned int i;

    for (i = 0; i < 8; i++) {
        if (base + i < s->nr_frames) {
            cnttid |= nibble << (i * 4);
        }
    }
    return cnttid;
}

/* Counter value derived on-demand from QEMU_CLOCK_VIRTUAL. */
static uint64_t hex_timer_now(QCTHextimerState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t scale;
    uint64_t scaled_elapsed;

    if (now <= s->offset_ns) {
        return 0;
    }
    scale = MAX(s->qtimer->freq_scale, 1u);
    scaled_elapsed = (uint64_t)(now - s->offset_ns) / scale;
    return muldiv64(scaled_elapsed, s->qtimer->freq_hz,
                    NANOSECONDS_PER_SECOND) &
           QCT_QTIMER_CNT_MASK;
}

/* Arm (or disarm) the one-shot deadline timer. */
static void hex_timer_rearm(QCTHextimerState *s)
{
    uint32_t scale;
    uint64_t base_ns;
    int64_t deadline_ns;

    if (!(s->control & QCT_QTIMER_CNTP_CTL_ENABLE)) {
        timer_del(s->timer);
        return;
    }

    scale = MAX(s->qtimer->freq_scale, 1u);
    /*
     * Round the ticks-to-ns conversion up so that hex_timer_now(), which
     * truncates when it divides elapsed ns by scale, is guaranteed to
     * report >= cntval once this deadline fires. A truncating conversion
     * here could re-arm at the same deadline forever when scale > 1.
     */
    base_ns = muldiv64_round_up(s->cntval, NANOSECONDS_PER_SECOND,
                                s->qtimer->freq_hz);
    if (base_ns >
        ((uint64_t)INT64_MAX - (uint64_t)s->offset_ns) / scale) {
        timer_del(s->timer);
        return;
    }
    deadline_ns = s->offset_ns + (int64_t)(base_ns * scale);
    timer_mod(s->timer, deadline_ns);
}

static void hex_timer_update(QCTHextimerState *s)
{
    int level = s->int_level &&
                (s->control & QCT_QTIMER_CNTP_CTL_ENABLE) &&
                !(s->control & QCT_QTIMER_CNTP_CTL_INTEN);

    trace_qtimer_interrupt();
    qemu_set_irq(s->irq, level);
}

/*
 * Access-control (AC) region: offsets below 0x1000, gates CNTFRQ/CNTSR/
 * CNTTID/CNTACR per frame plus the shared VERSION register.
 */
static uint64_t qct_qtimer_ac_read(void *opaque, hwaddr offset, unsigned size)
{
    QCTQtimerState *s = opaque;
    uint32_t frame;

    switch (offset) {
    case QCT_QTIMER_AC_CNTFRQ:
        return s->freq_hz;
    case QCT_QTIMER_AC_CNTSR:
        return s->secure;
    case QCT_QTIMER_AC_CNTTID_0:
        return qct_qtimer_cnttid(s, 0);
    case QCT_QTIMER_AC_CNTTID_1:
        return qct_qtimer_cnttid(s, 1);
    case QCT_QTIMER_AC_CNTACR_START ... QCT_QTIMER_AC_CNTACR_END:
        frame = (offset - QCT_QTIMER_AC_CNTACR_START) / 4;
        if (frame >= s->nr_frames) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: bad CNTACR offset 0x%x\n",
                          __func__, (int)offset);
            return 0;
        }
        return s->timer[frame].cnt_ctrl;
    case QCT_QTIMER_VERSION:
        return QCT_QTIMER_VERSION_VALUE;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%x\n", __func__,
                      (int)offset);
        return 0;
    }
}

static void qct_qtimer_ac_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    QCTQtimerState *s = opaque;
    uint32_t frame;

    switch (offset) {
    case QCT_QTIMER_AC_CNTFRQ:
        if (value == 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: bad CNTFRQ value 0\n",
                          __func__);
            return;
        }
        s->freq_hz = value;
        return;
    case QCT_QTIMER_AC_CNTSR:
        if (value > 0xff) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: bad CNTSR value 0x%x\n",
                          __func__, (int)value);
            return;
        }
        s->secure = value;
        return;
    case QCT_QTIMER_AC_CNTACR_START ... QCT_QTIMER_AC_CNTACR_END:
        frame = (offset - QCT_QTIMER_AC_CNTACR_START) / 4;
        if (frame >= s->nr_frames) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: bad CNTACR offset 0x%x\n",
                          __func__, (int)offset);
            return;
        }
        s->timer[frame].cnt_ctrl = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%x\n", __func__,
                      (int)offset);
        return;
    }
}

static const MemoryRegionOps qct_qtimer_ac_ops = {
    .read = qct_qtimer_ac_read,
    .write = qct_qtimer_ac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * View region: a flat array of (frame, view) slots, each frame_stride
 * bytes wide, holding the per-frame CNTPCT/CNTP_CVAL/CNTP_TVAL/CNTP_CTL
 * register set.
 */
static QCTHextimerState *qct_qtimer_demux(QCTQtimerState *s, hwaddr offset,
                                          uint32_t *reg_offset,
                                          uint32_t *view)
{
    uint32_t stride = s->frame_stride;
    uint32_t stride_shift = ctz32(stride);
    uint32_t slot_nr = offset >> stride_shift;
    uint32_t frame = slot_nr / s->nr_views;

    *reg_offset = offset & (stride - 1);
    *view = slot_nr % s->nr_views;
    if (frame >= s->nr_frames) {
        return NULL;
    }
    return &s->timer[frame];
}

/* Frames 8+ are described by CNTTID_1; each frame's 2nd view is a gated bit. */
static bool qct_qtimer_view_visible(QCTQtimerState *s, uint32_t frame,
                                    uint32_t view)
{
    uint32_t cnttid = qct_qtimer_cnttid(s, frame < 8 ? 0 : 1);
    uint32_t frame_idx = frame < 8 ? frame : frame - 8;

    return !view || (cnttid & (0x4 << (frame_idx * 4)));
}

static bool access_ok(QCTHextimerState *s, uint32_t reg_offset, uint32_t view)
{
    uint32_t acr;
    uint32_t pl0acr;

    switch (reg_offset) {
    case QCT_QTIMER_CNT_FREQ:
        acr = QCT_QTIMER_AC_CNTACR_RFRQ;
        pl0acr = QCT_QTIMER_CNTPL0ACR_PL0PCTEN |
                 QCT_QTIMER_CNTPL0ACR_PL0VCTEN;
        break;
    case QCT_QTIMER_CNTPCT_LO:
    case QCT_QTIMER_CNTPCT_HI:
        acr = QCT_QTIMER_AC_CNTACR_RPCT;
        pl0acr = QCT_QTIMER_CNTPL0ACR_PL0PCTEN;
        break;
    case QCT_QTIMER_CNTP_CVAL_LO:
    case QCT_QTIMER_CNTP_CVAL_HI:
    case QCT_QTIMER_CNTP_TVAL:
    case QCT_QTIMER_CNTP_CTL:
        acr = QCT_QTIMER_AC_CNTACR_RWPT;
        pl0acr = QCT_QTIMER_CNTPL0ACR_PL0CTEN;
        break;
    default:
        /* CNTPL0ACR and VERSION are ungated. */
        return true;
    }

    if (!(s->cnt_ctrl & acr)) {
        return false;
    }
    return !view || (s->cntpl0acr & pl0acr);
}

static MemTxResult hex_timer_read(void *opaque, hwaddr offset, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    QCTQtimerState *qs = opaque;
    uint32_t reg_offset;
    uint32_t view;
    QCTHextimerState *s = qct_qtimer_demux(qs, offset, &reg_offset, &view);
    uint32_t frame;

    if (!s) {
        *data = 0;
        return MEMTX_ACCESS_ERROR;
    }
    frame = s - qs->timer;

    trace_qtimer_read(offset);

    if (!qct_qtimer_view_visible(qs, frame, view)) {
        *data = 0;
        return MEMTX_OK;
    }

    if (!access_ok(s, reg_offset, view)) {
        return MEMTX_ACCESS_ERROR;
    }

    switch (reg_offset) {
    case QCT_QTIMER_CNT_FREQ:
        *data = s->qtimer->freq_hz;
        return MEMTX_OK;
    case QCT_QTIMER_CNTP_CVAL_LO:
        *data = extract64(s->cntval, 0, 32);
        return MEMTX_OK;
    case QCT_QTIMER_CNTP_CVAL_HI:
        /* HI half is 24-bit per TRM; bits [31:24] are reserved. */
        *data = extract64(s->cntval, 32, QCT_QTIMER_CNT_HI_BITS);
        return MEMTX_OK;
    case QCT_QTIMER_CNTPCT_LO:
        *data = extract64(hex_timer_now(s), 0, 32);
        return MEMTX_OK;
    case QCT_QTIMER_CNTPCT_HI:
        *data = extract64(hex_timer_now(s), 32, QCT_QTIMER_CNT_HI_BITS);
        return MEMTX_OK;
    case QCT_QTIMER_CNTP_TVAL:
        *data = (uint32_t)(int32_t)(int64_t)(s->cntval - hex_timer_now(s));
        return MEMTX_OK;
    case QCT_QTIMER_CNTP_CTL:
        /*
         * CNTP_CTL: bit 0 EN, bit 1 IMASK, bit 2 ISTAT (interrupt
         * pending). ISTAT tracks int_level and is read-only.
         */
        *data = s->control | ((s->int_level & 0x1) << 2);
        return MEMTX_OK;
    case QCT_QTIMER_CNTPL0ACR:
        *data = view ? 0 : s->cntpl0acr;
        return MEMTX_OK;
    case QCT_QTIMER_VERSION:
        *data = QCT_QTIMER_VERSION_VALUE;
        return MEMTX_OK;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%x\n", __func__,
                      (int)offset);
        *data = 0;
        return MEMTX_ACCESS_ERROR;
    }
}

static MemTxResult hex_timer_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size,
                                   MemTxAttrs attrs)
{
    QCTQtimerState *qs = opaque;
    uint32_t reg_offset;
    uint32_t view;
    QCTHextimerState *s = qct_qtimer_demux(qs, offset, &reg_offset, &view);
    uint32_t frame;

    if (!s) {
        return MEMTX_ACCESS_ERROR;
    }
    frame = s - qs->timer;

    trace_qtimer_write(offset, value);

    if (!qct_qtimer_view_visible(qs, frame, view)) {
        return MEMTX_OK;
    }

    if (!access_ok(s, reg_offset, view)) {
        return MEMTX_ACCESS_ERROR;
    }

    switch (reg_offset) {
    case QCT_QTIMER_CNTP_CVAL_LO:
        s->int_level = 0;
        s->cntval = deposit64(s->cntval, 0, 32, value);
        hex_timer_rearm(s);
        break;
    case QCT_QTIMER_CNTP_CVAL_HI:
        s->int_level = 0;
        /* HI half is 24-bit per TRM; bits [31:24] are reserved. */
        s->cntval = deposit64(s->cntval, 32, QCT_QTIMER_CNT_HI_BITS, value) &
                    QCT_QTIMER_CNT_MASK;
        hex_timer_rearm(s);
        break;
    case QCT_QTIMER_CNTP_CTL:
        /* ISTAT (bit 2) is read-only; keep SW writes from polluting it. */
        s->control = value & ~QCT_QTIMER_CNTP_CTL_ISTAT;
        hex_timer_rearm(s);
        break;
    case QCT_QTIMER_CNTP_TVAL:
        /* TVAL write: CVAL = CNTPCT + TVAL (TVAL is signed 32-bit). */
        s->int_level = 0;
        s->cntval = (hex_timer_now(s) + (int64_t)(int32_t)value) &
                    QCT_QTIMER_CNT_MASK;
        hex_timer_rearm(s);
        break;
    case QCT_QTIMER_CNTPL0ACR:
        if (!view) {
            s->cntpl0acr = value;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%x\n", __func__,
                      (int)offset);
        return MEMTX_ACCESS_ERROR;
    }
    hex_timer_update(s);
    return MEMTX_OK;
}

static void hex_timer_tick(void *opaque)
{
    QCTHextimerState *s = opaque;
    uint64_t now = hex_timer_now(s);
    uint64_t diff56 = (now - s->cntval) & QCT_QTIMER_CNT_MASK;
    int64_t signed_diff = (int64_t)(diff56 << 8) >> 8;

    if (signed_diff >= 0) {
        s->int_level = 1;
        hex_timer_update(s);
    } else {
        hex_timer_rearm(s);
    }
}

static const MemoryRegionOps hex_timer_ops = {
    .read_with_attrs = hex_timer_read,
    .write_with_attrs = hex_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_qct_hextimer = {
    .name = "qct-hextimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(control, QCTHextimerState),
        VMSTATE_UINT32(cnt_ctrl, QCTHextimerState),
        VMSTATE_INT64(offset_ns, QCTHextimerState),
        VMSTATE_UINT64(cntval, QCTHextimerState),
        VMSTATE_UINT32(cntpl0acr, QCTHextimerState),
        VMSTATE_UINT32(int_level, QCTHextimerState),
        VMSTATE_TIMER_PTR(timer, QCTHextimerState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_qct_qtimer = {
    .name = "qct-qtimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(freq_hz, QCTQtimerState),
        VMSTATE_UINT32(secure, QCTQtimerState),
        VMSTATE_STRUCT_VARRAY_UINT32(timer, QCTQtimerState, nr_frames,
                                    1, vmstate_qct_hextimer, QCTHextimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void qct_qtimer_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    QCTQtimerState *s = QCT_QTIMER(dev);
    unsigned int i;

    if (s->nr_frames > QCT_QTIMER_TIMER_FRAME_ELTS) {
        error_setg(errp, "nr_frames too high");
        return;
    }
    if (s->nr_views > QCT_QTIMER_TIMER_VIEW_ELTS) {
        error_setg(errp, "nr_views too high");
        return;
    }
    if (s->freq_hz == 0) {
        error_setg(errp, "freq-hz must be nonzero");
        return;
    }
    if (s->frame_stride == 0 || !is_power_of_2(s->frame_stride)) {
        error_setg(errp, "frame_stride must be a nonzero power of two");
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &qct_qtimer_ac_ops, s,
                          "qct-qtimer-ac", QTIMER_MEM_SIZE_BYTES);
    sysbus_init_mmio(sbd, &s->iomem);

    memory_region_init_io(&s->view_iomem, OBJECT(s), &hex_timer_ops, s,
                          "qct-qtimer-view",
                          (uint64_t)s->frame_stride * s->nr_frames *
                          s->nr_views);
    sysbus_init_mmio(sbd, &s->view_iomem);

    for (i = 0; i < s->nr_frames; i++) {
        QCTHextimerState *t = &s->timer[i];

        t->qtimer = s;
        s->secure |= (1 << i);

        sysbus_init_irq(sbd, &t->irq);
        t->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, hex_timer_tick, t);
    }
}

static void qct_qtimer_unrealize(DeviceState *dev)
{
    QCTQtimerState *s = QCT_QTIMER(dev);
    unsigned int i;

    for (i = 0; i < s->nr_frames; i++) {
        QCTHextimerState *t = &s->timer[i];

        if (t->timer) {
            timer_free(t->timer);
            t->timer = NULL;
        }
    }
}

static void qct_qtimer_reset_hold(Object *obj, ResetType type)
{
    QCTQtimerState *s = QCT_QTIMER(obj);
    unsigned int i;

    for (i = 0; i < s->nr_frames; i++) {
        QCTHextimerState *t = &s->timer[i];

        /*
         * Per TRM: CTL = 0 (EN=0, IMASK=0, ISTAT=0), CVAL = 0 so that
         * TVAL (= CVAL - CNTPCT) also reads 0 at reset. The QEMUTimer is
         * only armed when SW sets CTL.EN=1, so cntval=0 does not cause a
         * spurious fire before SW programs the compare value.
         */
        t->control = 0;
        t->cnt_ctrl = QCT_QTIMER_AC_CNTACR_RWPT | QCT_QTIMER_AC_CNTACR_RWVT |
                      QCT_QTIMER_AC_CNTACR_RVOFF | QCT_QTIMER_AC_CNTACR_RFRQ |
                      QCT_QTIMER_AC_CNTACR_RPVCT | QCT_QTIMER_AC_CNTACR_RPCT;
        t->cntval = 0;
        t->cntpl0acr = 0;
        t->int_level = 0;
        t->offset_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        timer_del(t->timer);
        qemu_set_irq(t->irq, 0);
    }
}

static const Property qct_qtimer_properties[] = {
    DEFINE_PROP_UINT32("freq-hz", QCTQtimerState, freq_hz,
                       QTIMER_DEFAULT_FREQ_HZ),
    DEFINE_PROP_UINT32("freq-scale", QCTQtimerState, freq_scale, 1),
    DEFINE_PROP_UINT32("nr_frames", QCTQtimerState, nr_frames, 2),
    DEFINE_PROP_UINT32("nr_views", QCTQtimerState, nr_views, 1),
    DEFINE_PROP_UINT32("frame_stride", QCTQtimerState, frame_stride, 0x1000),
};

static void qct_qtimer_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, qct_qtimer_properties);
    dc->realize = qct_qtimer_realize;
    dc->unrealize = qct_qtimer_unrealize;
    dc->vmsd = &vmstate_qct_qtimer;
    rc->phases.hold = qct_qtimer_reset_hold;
}

/* QTimer interface implementation, backing HEX_SREG_TIMERLO/TIMERHI */
static uint32_t qct_qtimer_get_timer_lo_impl(const QctQtimerInterface *obj)
{
    QCTQtimerState *s = QCT_QTIMER((QctQtimerInterface *)obj);

    return s->nr_frames > 0 ? extract64(hex_timer_now(&s->timer[0]), 0, 32)
                            : 0;
}

static uint32_t qct_qtimer_get_timer_hi_impl(const QctQtimerInterface *obj)
{
    QCTQtimerState *s = QCT_QTIMER((QctQtimerInterface *)obj);

    return s->nr_frames > 0 ? extract64(hex_timer_now(&s->timer[0]), 32, 32)
                            : 0;
}

static void qct_qtimer_interface_class_init(ObjectClass *klass,
                                            const void *data)
{
    QctQtimerInterfaceClass *k = QCT_QTIMER_INTERFACE_CLASS(klass);

    k->get_timer_lo = qct_qtimer_get_timer_lo_impl;
    k->get_timer_hi = qct_qtimer_get_timer_hi_impl;
}

static const TypeInfo qct_qtimer_types[] = {
    {
        .name = TYPE_QCT_QTIMER_INTERFACE,
        .parent = TYPE_INTERFACE,
        .class_size = sizeof(QctQtimerInterfaceClass),
        .class_init = qct_qtimer_interface_class_init,
    },
    {
        .name = TYPE_QCT_QTIMER,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(QCTQtimerState),
        .class_init = qct_qtimer_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_QCT_QTIMER_INTERFACE },
            { }
        },
    },
};

DEFINE_TYPES(qct_qtimer_types)
