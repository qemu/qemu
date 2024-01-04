/*
 * Arm SSE Subsystem System Counter
 *
 * Copyright (c) 2020 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/*
 * This is a model of the "System counter" which is documented in
 * the Arm SSE-123 Example Subsystem Technical Reference Manual:
 * https://developer.arm.com/documentation/101370/latest/
 *
 * The system counter is a non-stop 64-bit up-counter. It provides
 * this count value to other devices like the SSE system timer,
 * which are driven by this system timestamp rather than directly
 * from a clock. Internally to the counter the count is actually
 * 88-bit precision (64.24 fixed point), with a programmable scale factor.
 *
 * The hardware has the optional feature that it supports dynamic
 * clock switching, where two clock inputs are connected, and which
 * one is used is selected via a CLKSEL input signal. Since the
 * users of this device in QEMU don't use this feature, we only model
 * the HWCLKSW=0 configuration.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/timer/sse-counter.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/clock.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"

/* Registers in the control frame */
REG32(CNTCR, 0x0)
    FIELD(CNTCR, EN, 0, 1)
    FIELD(CNTCR, HDBG, 1, 1)
    FIELD(CNTCR, SCEN, 2, 1)
    FIELD(CNTCR, INTRMASK, 3, 1)
    FIELD(CNTCR, PSLVERRDIS, 4, 1)
    FIELD(CNTCR, INTRCLR, 5, 1)
/*
 * Although CNTCR defines interrupt-related bits, the counter doesn't
 * appear to actually have an interrupt output. So INTRCLR is
 * effectively a RAZ/WI bit, as are the reserved bits [31:6].
 */
#define CNTCR_VALID_MASK (R_CNTCR_EN_MASK | R_CNTCR_HDBG_MASK | \
                          R_CNTCR_SCEN_MASK | R_CNTCR_INTRMASK_MASK | \
                          R_CNTCR_PSLVERRDIS_MASK)
REG32(CNTSR, 0x4)
REG32(CNTCV_LO, 0x8)
REG32(CNTCV_HI, 0xc)
REG32(CNTSCR, 0x10) /* Aliased with CNTSCR0 */
REG32(CNTID, 0x1c)
    FIELD(CNTID, CNTSC, 0, 4)
    FIELD(CNTID, CNTCS, 16, 1)
    FIELD(CNTID, CNTSELCLK, 17, 2)
    FIELD(CNTID, CNTSCR_OVR, 19, 1)
REG32(CNTSCR0, 0xd0)
REG32(CNTSCR1, 0xd4)

/* Registers in the status frame */
REG32(STATUS_CNTCV_LO, 0x0)
REG32(STATUS_CNTCV_HI, 0x4)

/* Standard ID registers, present in both frames */
REG32(PID4, 0xFD0)
REG32(PID5, 0xFD4)
REG32(PID6, 0xFD8)
REG32(PID7, 0xFDC)
REG32(PID0, 0xFE0)
REG32(PID1, 0xFE4)
REG32(PID2, 0xFE8)
REG32(PID3, 0xFEC)
REG32(CID0, 0xFF0)
REG32(CID1, 0xFF4)
REG32(CID2, 0xFF8)
REG32(CID3, 0xFFC)

/* PID/CID values */
static const int control_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0xba, 0xb0, 0x0b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static const int status_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0xbb, 0xb0, 0x0b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static void sse_counter_notify_users(SSECounter *s)
{
    /*
     * Notify users of the count timestamp that they may
     * need to recalculate.
     */
    notifier_list_notify(&s->notifier_list, NULL);
}

static bool sse_counter_enabled(SSECounter *s)
{
    return (s->cntcr & R_CNTCR_EN_MASK) != 0;
}

uint64_t sse_counter_tick_to_time(SSECounter *s, uint64_t tick)
{
    if (!sse_counter_enabled(s)) {
        return UINT64_MAX;
    }

    tick -= s->ticks_then;

    if (s->cntcr & R_CNTCR_SCEN_MASK) {
        /* Adjust the tick count to account for the scale factor */
        tick = muldiv64(tick, 0x01000000, s->cntscr0);
    }

    return s->ns_then + clock_ticks_to_ns(s->clk, tick);
}

void sse_counter_register_consumer(SSECounter *s, Notifier *notifier)
{
    /*
     * For the moment we assume that both we and the devices
     * which consume us last for the life of the simulation,
     * and so there is no mechanism for removing a notifier.
     */
    notifier_list_add(&s->notifier_list, notifier);
}

uint64_t sse_counter_for_timestamp(SSECounter *s, uint64_t now)
{
    /* Return the CNTCV value for a particular timestamp (clock ns value). */
    uint64_t ticks;

    if (!sse_counter_enabled(s)) {
        /* Counter is disabled and does not increment */
        return s->ticks_then;
    }

    ticks = clock_ns_to_ticks(s->clk, now - s->ns_then);
    if (s->cntcr & R_CNTCR_SCEN_MASK) {
        /*
         * Scaling is enabled. The CNTSCR value is the amount added to
         * the underlying 88-bit counter for every tick of the
         * underlying clock; CNTCV is the top 64 bits of that full
         * 88-bit value. Multiplying the tick count by CNTSCR tells us
         * how much the full 88-bit counter has moved on; we then
         * divide that by 0x01000000 to find out how much the 64-bit
         * visible portion has advanced. muldiv64() gives us the
         * necessary at-least-88-bit precision for the intermediate
         * result.
         */
        ticks = muldiv64(ticks, s->cntscr0, 0x01000000);
    }
    return s->ticks_then + ticks;
}

static uint64_t sse_cntcv(SSECounter *s)
{
    /* Return the CNTCV value for the current time */
    return sse_counter_for_timestamp(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

static void sse_write_cntcv(SSECounter *s, uint32_t value, unsigned startbit)
{
    /*
     * Write one 32-bit half of the counter value; startbit is the
     * bit position of this half in the 64-bit word, either 0 or 32.
     */
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t cntcv = sse_counter_for_timestamp(s, now);

    cntcv = deposit64(cntcv, startbit, 32, value);
    s->ticks_then = cntcv;
    s->ns_then = now;
    sse_counter_notify_users(s);
}

static uint64_t sse_counter_control_read(void *opaque, hwaddr offset,
                                         unsigned size)
{
    SSECounter *s = SSE_COUNTER(opaque);
    uint64_t r;

    switch (offset) {
    case A_CNTCR:
        r = s->cntcr;
        break;
    case A_CNTSR:
        /*
         * The only bit here is DBGH, indicating that the counter has been
         * halted via the Halt-on-Debug signal. We don't implement halting
         * debug, so the whole register always reads as zero.
         */
        r = 0;
        break;
    case A_CNTCV_LO:
        r = extract64(sse_cntcv(s), 0, 32);
        break;
    case A_CNTCV_HI:
        r = extract64(sse_cntcv(s), 32, 32);
        break;
    case A_CNTID:
        /*
         * For our implementation:
         *  - CNTSCR can only be written when CNTCR.EN == 0
         *  - HWCLKSW=0, so selected clock is always CLK0
         *  - counter scaling is implemented
         */
        r = (1 << R_CNTID_CNTSELCLK_SHIFT) | (1 << R_CNTID_CNTSC_SHIFT);
        break;
    case A_CNTSCR:
    case A_CNTSCR0:
        r = s->cntscr0;
        break;
    case A_CNTSCR1:
        /* If HWCLKSW == 0, CNTSCR1 is RAZ/WI */
        r = 0;
        break;
    case A_PID4 ... A_CID3:
        r = control_id[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE System Counter control frame read: bad offset 0x%x",
                      (unsigned)offset);
        r = 0;
        break;
    }

    trace_sse_counter_control_read(offset, r, size);
    return r;
}

static void sse_counter_control_write(void *opaque, hwaddr offset,
                                      uint64_t value, unsigned size)
{
    SSECounter *s = SSE_COUNTER(opaque);

    trace_sse_counter_control_write(offset, value, size);

    switch (offset) {
    case A_CNTCR:
        /*
         * Although CNTCR defines interrupt-related bits, the counter doesn't
         * appear to actually have an interrupt output. So INTRCLR is
         * effectively a RAZ/WI bit, as are the reserved bits [31:6].
         * The documentation does not explicitly say so, but we assume
         * that changing the scale factor while the counter is enabled
         * by toggling CNTCR.SCEN has the same behaviour (making the counter
         * value UNKNOWN) as changing it by writing to CNTSCR, and so we
         * don't need to try to recalculate for that case.
         */
        value &= CNTCR_VALID_MASK;
        if ((value ^ s->cntcr) & R_CNTCR_EN_MASK) {
            /*
             * Whether the counter is being enabled or disabled, the
             * required action is the same: sync the (ns_then, ticks_then)
             * tuple.
             */
            uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            s->ticks_then = sse_counter_for_timestamp(s, now);
            s->ns_then = now;
            sse_counter_notify_users(s);
        }
        s->cntcr = value;
        break;
    case A_CNTCV_LO:
        sse_write_cntcv(s, value, 0);
        break;
    case A_CNTCV_HI:
        sse_write_cntcv(s, value, 32);
        break;
    case A_CNTSCR:
    case A_CNTSCR0:
        /*
         * If the scale registers are changed when the counter is enabled,
         * the count value becomes UNKNOWN. So we don't try to recalculate
         * anything here but only do it on a write to CNTCR.EN.
         */
        s->cntscr0 = value;
        break;
    case A_CNTSCR1:
        /* If HWCLKSW == 0, CNTSCR1 is RAZ/WI */
        break;
    case A_CNTSR:
    case A_CNTID:
    case A_PID4 ... A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE System Counter control frame: write to RO offset 0x%x\n",
                      (unsigned)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE System Counter control frame: write to bad offset 0x%x\n",
                      (unsigned)offset);
        break;
    }
}

static uint64_t sse_counter_status_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    SSECounter *s = SSE_COUNTER(opaque);
    uint64_t r;

    switch (offset) {
    case A_STATUS_CNTCV_LO:
        r = extract64(sse_cntcv(s), 0, 32);
        break;
    case A_STATUS_CNTCV_HI:
        r = extract64(sse_cntcv(s), 32, 32);
        break;
    case A_PID4 ... A_CID3:
        r = status_id[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE System Counter status frame read: bad offset 0x%x",
                      (unsigned)offset);
        r = 0;
        break;
    }

    trace_sse_counter_status_read(offset, r, size);
    return r;
}

static void sse_counter_status_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    trace_sse_counter_status_write(offset, value, size);

    switch (offset) {
    case A_STATUS_CNTCV_LO:
    case A_STATUS_CNTCV_HI:
    case A_PID4 ... A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE System Counter status frame: write to RO offset 0x%x\n",
                      (unsigned)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE System Counter status frame: write to bad offset 0x%x\n",
                      (unsigned)offset);
        break;
    }
}

static const MemoryRegionOps sse_counter_control_ops = {
    .read = sse_counter_control_read,
    .write = sse_counter_control_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const MemoryRegionOps sse_counter_status_ops = {
    .read = sse_counter_status_read,
    .write = sse_counter_status_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void sse_counter_reset(DeviceState *dev)
{
    SSECounter *s = SSE_COUNTER(dev);

    trace_sse_counter_reset();

    s->cntcr = 0;
    s->cntscr0 = 0x01000000;
    s->ns_then = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->ticks_then = 0;
}

static void sse_clk_callback(void *opaque, ClockEvent event)
{
    SSECounter *s = SSE_COUNTER(opaque);
    uint64_t now;

    switch (event) {
    case ClockPreUpdate:
        /*
         * Before the clock period updates, set (ticks_then, ns_then)
         * to the current time and tick count (as calculated with
         * the old clock period).
         */
        if (sse_counter_enabled(s)) {
            now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            s->ticks_then = sse_counter_for_timestamp(s, now);
            s->ns_then = now;
        }
        break;
    case ClockUpdate:
        sse_counter_notify_users(s);
        break;
    default:
        break;
    }
}

static void sse_counter_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SSECounter *s = SSE_COUNTER(obj);

    notifier_list_init(&s->notifier_list);

    s->clk = qdev_init_clock_in(DEVICE(obj), "CLK", sse_clk_callback, s,
                                ClockPreUpdate | ClockUpdate);
    memory_region_init_io(&s->control_mr, obj, &sse_counter_control_ops,
                          s, "sse-counter-control", 0x1000);
    memory_region_init_io(&s->status_mr, obj, &sse_counter_status_ops,
                          s, "sse-counter-status", 0x1000);
    sysbus_init_mmio(sbd, &s->control_mr);
    sysbus_init_mmio(sbd, &s->status_mr);
}

static void sse_counter_realize(DeviceState *dev, Error **errp)
{
    SSECounter *s = SSE_COUNTER(dev);

    if (!clock_has_source(s->clk)) {
        error_setg(errp, "SSE system counter: CLK must be connected");
        return;
    }
}

static const VMStateDescription sse_counter_vmstate = {
    .name = "sse-counter",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(clk, SSECounter),
        VMSTATE_END_OF_LIST()
    }
};

static void sse_counter_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sse_counter_realize;
    dc->vmsd = &sse_counter_vmstate;
    dc->reset = sse_counter_reset;
}

static const TypeInfo sse_counter_info = {
    .name = TYPE_SSE_COUNTER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SSECounter),
    .instance_init = sse_counter_init,
    .class_init = sse_counter_class_init,
};

static void sse_counter_register_types(void)
{
    type_register_static(&sse_counter_info);
}

type_init(sse_counter_register_types);
