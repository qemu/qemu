/*
 * Qualcomm QCT QTimer
 *
 * Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/timer/qct-qtimer.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"

/* Common timer implementation.  */

#define QTIMER_MEM_SIZE_BYTES 0x1000
#define QTIMER_MEM_REGION_SIZE_BYTES 0x1000
#define QTIMER_DEFAULT_FREQ_HZ 19200000ULL
#define QTMR_TIMER_INDEX_MASK (0xf000)
#define HIGH_32(val) (0x0ffffffffULL & (val >> 32))
#define LOW_32(val) (0x0ffffffffULL & val)

/*
 * QTimer version reg:
 *
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Major |         Minor         |           Step                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
static unsigned int TIMER_VERSION = 0x20020000;

/*
 * qct_qtimer_read/write:
 * if offset < 0x1000 read restricted registers:
 * QCT_QTIMER_AC_CNTFREQ/CNTSR/CNTTID/CNTACR/CNTOFF_(LO/HI)/QCT_QTIMER_VERSION
 */
static uint64_t qct_qtimer_read(void *opaque, hwaddr offset, unsigned size)
{
    QCTQtimerState *s = (QCTQtimerState *)opaque;
    uint32_t frame = 0;

    switch (offset) {
    case QCT_QTIMER_AC_CNTFRQ:
        return s->freq;
    case QCT_QTIMER_AC_CNTSR:
        return s->secure;
    case QCT_QTIMER_AC_CNTTID:
        return s->cnttid;
    case QCT_QTIMER_AC_CNTACR_START ... QCT_QTIMER_AC_CNTACR_END:
        frame = (offset - 0x40) / 0x4;
        if (frame >= s->nr_frames) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: QCT_QTIMER_AC_CNT: Bad offset %x\n", __func__,
                          (int)offset);
            return 0x0;
        }
        return s->timer[frame].cnt_ctrl;
    case QCT_QTIMER_VERSION:
        return TIMER_VERSION;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: QCT_QTIMER_AC_CNT: Bad offset %x\n",
                      __func__, (int)offset);
        return 0x0;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%x\n", __func__,
                  (int)offset);
    return 0;
}

static void qct_qtimer_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    QCTQtimerState *s = (QCTQtimerState *)opaque;
    uint32_t frame = 0;

    if (offset < 0x1000) {
        switch (offset) {
        case QCT_QTIMER_AC_CNTFRQ:
            s->freq = value;
            return;
        case QCT_QTIMER_AC_CNTSR:
            if (value > 0xFF)
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: QCT_QTIMER_AC_CNTSR: Bad value %x\n",
                              __func__, (int)value);
            else
                s->secure = value;
            return;
        case QCT_QTIMER_AC_CNTACR_START ... QCT_QTIMER_AC_CNTACR_END:
            frame = (offset - QCT_QTIMER_AC_CNTACR_START) / 0x4;
            if (frame >= s->nr_frames) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: QCT_QTIMER_AC_CNT: Bad offset %x\n",
                              __func__, (int)offset);
                return;
            }
            s->timer[frame].cnt_ctrl = value;
            return;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: QCT_QTIMER_AC_CNT: Bad offset %x\n", __func__,
                          (int)offset);
            return;
        }
    } else
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %x\n", __func__,
                      (int)offset);
}

static const MemoryRegionOps qct_qtimer_ops = {
    .read = qct_qtimer_read,
    .write = qct_qtimer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_qct_qtimer = {
    .name = "qct-qtimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){ VMSTATE_END_OF_LIST() }
};

static void qct_qtimer_init(Object *obj)
{
    QCTQtimerState *s = QCT_QTIMER(obj);

    object_property_add_uint32_ptr(obj, "secure", &s->secure,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "frame_id", &s->frame_id,
                                   OBJ_PROP_FLAG_READ);
}

static void hex_timer_update(QCTHextimerState *s)
{
    /* Update interrupts.  */
    int level = s->int_level && (s->control & QCT_QTIMER_CNTP_CTL_ENABLE);
    qemu_set_irq(s->irq, level);
}

static MemTxResult hex_timer_read(void *opaque, hwaddr offset, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    QCTQtimerState *qct_s = (QCTQtimerState *)opaque;
    uint32_t slot_nr = (offset & 0xF000) >> 12;
    uint32_t reg_offset = offset & 0xFFF;
    uint32_t view = slot_nr % qct_s->nr_views;
    uint32_t frame = slot_nr / qct_s->nr_views;

    if (frame >= qct_s->nr_frames) {
        *data = 0;
        return MEMTX_ACCESS_ERROR;
    }
    QCTHextimerState *s = &qct_s->timer[frame];


    /*
     * This is the case where we have 2 views, but the second one is not
     * implemented.
     */
    if (view && !(qct_s->cnttid & (0x4 << (frame * 4)))) {
        *data = 0;
        return MEMTX_OK;
    }

    switch (reg_offset) {
    case (QCT_QTIMER_CNT_FREQ): /* Ticks/Second */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RFRQ)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !((s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0PCTEN) ||
                      (s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0VCTEN))) {
            return MEMTX_ACCESS_ERROR;
        }

        *data = s->freq;
        return MEMTX_OK;
    case (QCT_QTIMER_CNTP_CVAL_LO): /* TimerLoad */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        *data = LOW_32((s->cntval));
        return MEMTX_OK;
    case (QCT_QTIMER_CNTP_CVAL_HI): /* TimerLoad */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        *data = HIGH_32((s->cntval));
        return MEMTX_OK;
    case QCT_QTIMER_CNTPCT_LO:
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RPCT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0PCTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        *data = LOW_32((s->cntpct + (ptimer_get_count(s->timer))));
        return MEMTX_OK;
    case QCT_QTIMER_CNTPCT_HI:
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RPCT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0PCTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        *data = HIGH_32((s->cntpct + (ptimer_get_count(s->timer))));
        return MEMTX_OK;
    case (QCT_QTIMER_CNTP_TVAL): /* CVAL - CNTP */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        *data =
            (s->cntval - (HIGH_32((s->cntpct + (ptimer_get_count(s->timer)))) +
                          LOW_32((s->cntpct + (ptimer_get_count(s->timer))))));
        return MEMTX_OK;
    case (QCT_QTIMER_CNTP_CTL): /* TimerMIS */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        *data = s->int_level;
        return MEMTX_OK;
    case QCT_QTIMER_CNTPL0ACR:
        if (view) {
            *data = 0;
        } else {
            *data = s->cntpl0acr;
        }
        return MEMTX_OK;

    case QCT_QTIMER_VERSION:
        *data = TIMER_VERSION;
        return MEMTX_OK;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %x\n", __func__,
                      (int)offset);
        *data = 0;
        return MEMTX_ACCESS_ERROR;
    }
}

/*
 * Reset the timer limit after settings have changed.
 * May only be called from inside a ptimer transaction block.
 */
static void hex_timer_recalibrate(QCTHextimerState *s, int reload)
{
    uint64_t limit;
    /* Periodic.  */
    limit = s->limit;
    ptimer_set_limit(s->timer, limit, reload);
}

static MemTxResult hex_timer_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size, MemTxAttrs attrs)
{
    QCTQtimerState *qct_s = (QCTQtimerState *)opaque;
    uint32_t slot_nr = (offset & 0xF000) >> 12;
    uint32_t reg_offset = offset & 0xFFF;
    uint32_t view = slot_nr % qct_s->nr_views;
    uint32_t frame = slot_nr / qct_s->nr_views;

    if (frame >= qct_s->nr_frames) {
        return MEMTX_ACCESS_ERROR;
    }
    QCTHextimerState *s = &qct_s->timer[frame];

    /*
     * This is the case where we have 2 views, but the second one is not
     * implemented.
     */
    if (view && !(qct_s->cnttid & (0x4 << (frame * 4)))) {
        return MEMTX_OK;
    }

    switch (reg_offset) {
    case (QCT_QTIMER_CNTP_CVAL_LO): /* TimerLoad */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }


        s->int_level = 0;
        s->cntval = value;
        ptimer_transaction_begin(s->timer);
        if (s->control & QCT_QTIMER_CNTP_CTL_ENABLE) {
            /*
             * Pause the timer if it is running.  This may cause some
             * inaccuracy due to rounding, but avoids other issues.
             */
            ptimer_stop(s->timer);
        }
        hex_timer_recalibrate(s, 1);
        if (s->control & QCT_QTIMER_CNTP_CTL_ENABLE) {
            ptimer_run(s->timer, 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case (QCT_QTIMER_CNTP_CVAL_HI):
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        break;
    case (QCT_QTIMER_CNTP_CTL): /* Timer control register */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        ptimer_transaction_begin(s->timer);
        if (s->control & QCT_QTIMER_CNTP_CTL_ENABLE) {
            /*
             * Pause the timer if it is running.  This may cause some
             * inaccuracy due to rounding, but avoids other issues.
             */
            ptimer_stop(s->timer);
        }
        s->control = value;
        hex_timer_recalibrate(s, s->control & QCT_QTIMER_CNTP_CTL_ENABLE);
        ptimer_set_freq(s->timer, s->freq);
        ptimer_set_period(s->timer, 1);
        if (s->control & QCT_QTIMER_CNTP_CTL_ENABLE) {
            ptimer_run(s->timer, 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case (QCT_QTIMER_CNTP_TVAL): /* CVAL - CNTP */
        if (!(s->cnt_ctrl & QCT_QTIMER_AC_CNTACR_RWPT)) {
            return MEMTX_ACCESS_ERROR;
        }

        if (view && !(s->cntpl0acr & QCT_QTIMER_CNTPL0ACR_PL0CTEN)) {
            return MEMTX_ACCESS_ERROR;
        }

        ptimer_transaction_begin(s->timer);
        if (s->control & QCT_QTIMER_CNTP_CTL_ENABLE) {
            /*
             * Pause the timer if it is running.  This may cause some
             * inaccuracy due to rounding, but avoids other issues.
             */
            ptimer_stop(s->timer);
        }
        s->cntval = s->cntpct + value;
        ptimer_set_freq(s->timer, s->freq);
        ptimer_set_period(s->timer, 1);
        if (s->control & QCT_QTIMER_CNTP_CTL_ENABLE) {
            ptimer_run(s->timer, 0);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case QCT_QTIMER_CNTPL0ACR:
        if (view) {
            break;
        }

        s->cntpl0acr = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %x\n", __func__,
                      (int)offset);
        return MEMTX_ACCESS_ERROR;
    }
    hex_timer_update(s);
    return MEMTX_OK;
}

static void hex_timer_tick(void *opaque)
{
    QCTHextimerState *s = (QCTHextimerState *)opaque;
    if ((s->cntpct >= s->cntval) && (s->int_level != 1)) {
        s->int_level = 1;
        hex_timer_update(s);
        return;
    }
    s->cntpct += s->limit;
}

static const MemoryRegionOps hex_timer_ops = {
    .read_with_attrs = hex_timer_read,
    .write_with_attrs = hex_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_hex_timer = {
    .name = "hex_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){ VMSTATE_UINT32(control, QCTHextimerState),
                                VMSTATE_UINT32(cnt_ctrl, QCTHextimerState),
                                VMSTATE_UINT64(cntpct, QCTHextimerState),
                                VMSTATE_UINT64(cntval, QCTHextimerState),
                                VMSTATE_UINT64(limit, QCTHextimerState),
                                VMSTATE_UINT32(int_level, QCTHextimerState),
                                VMSTATE_PTIMER(timer, QCTHextimerState),
                                VMSTATE_END_OF_LIST() }
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

    memory_region_init_io(&s->iomem, OBJECT(sbd), &qct_qtimer_ops, s, "qutimer",
                          QTIMER_MEM_SIZE_BYTES);
    sysbus_init_mmio(sbd, &s->iomem);

    memory_region_init_io(&s->view_iomem, OBJECT(sbd), &hex_timer_ops, s,
                          "qutimer_views",
                          QTIMER_MEM_SIZE_BYTES * s->nr_frames * s->nr_views);
    sysbus_init_mmio(sbd, &s->view_iomem);

    for (i = 0; i < s->nr_frames; i++) {
        s->timer[i].limit = 1;
        s->timer[i].control = QCT_QTIMER_CNTP_CTL_ENABLE;
        s->timer[i].cnt_ctrl =
            (QCT_QTIMER_AC_CNTACR_RWPT | QCT_QTIMER_AC_CNTACR_RWVT |
             QCT_QTIMER_AC_CNTACR_RVOFF | QCT_QTIMER_AC_CNTACR_RFRQ |
             QCT_QTIMER_AC_CNTACR_RPVCT | QCT_QTIMER_AC_CNTACR_RPCT);
        s->timer[i].qtimer = s;
        s->timer[i].freq = QTIMER_DEFAULT_FREQ_HZ;

        s->secure |= (1 << i);

        sysbus_init_irq(sbd, &(s->timer[i].irq));

        (s->timer[i]).timer =
            ptimer_init(hex_timer_tick, &s->timer[i], PTIMER_POLICY_LEGACY);
        vmstate_register(NULL, VMSTATE_INSTANCE_ID_ANY, &vmstate_hex_timer,
                         &s->timer[i]);
    }
}

static const Property qct_qtimer_properties[] = {
    DEFINE_PROP_UINT32("freq", QCTQtimerState, freq, QTIMER_DEFAULT_FREQ_HZ),
    DEFINE_PROP_UINT32("nr_frames", QCTQtimerState, nr_frames, 2),
    DEFINE_PROP_UINT32("nr_views", QCTQtimerState, nr_views, 1),
    DEFINE_PROP_UINT32("cnttid", QCTQtimerState, cnttid, 0x11),
};

static void qct_qtimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    device_class_set_props(k, qct_qtimer_properties);
    k->realize = qct_qtimer_realize;
    k->vmsd = &vmstate_qct_qtimer;
}

static const TypeInfo qct_qtimer_info = {
    .name = TYPE_QCT_QTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QCTQtimerState),
    .instance_init = qct_qtimer_init,
    .class_init = qct_qtimer_class_init,
};

static void qct_qtimer_register_types(void)
{
    type_register_static(&qct_qtimer_info);
}

type_init(qct_qtimer_register_types)
