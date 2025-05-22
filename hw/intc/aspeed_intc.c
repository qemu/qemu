/*
 * ASPEED INTC Controller
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/intc/aspeed_intc.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/registerfields.h"
#include "qapi/error.h"

/*
 * INTC Registers
 *
 * values below are offset by - 0x1000 from datasheet
 * because its memory region is start at 0x1000
 *
 */
REG32(GICINT128_EN,         0x000)
REG32(GICINT128_STATUS,     0x004)
REG32(GICINT129_EN,         0x100)
REG32(GICINT129_STATUS,     0x104)
REG32(GICINT130_EN,         0x200)
REG32(GICINT130_STATUS,     0x204)
REG32(GICINT131_EN,         0x300)
REG32(GICINT131_STATUS,     0x304)
REG32(GICINT132_EN,         0x400)
REG32(GICINT132_STATUS,     0x404)
REG32(GICINT133_EN,         0x500)
REG32(GICINT133_STATUS,     0x504)
REG32(GICINT134_EN,         0x600)
REG32(GICINT134_STATUS,     0x604)
REG32(GICINT135_EN,         0x700)
REG32(GICINT135_STATUS,     0x704)
REG32(GICINT136_EN,         0x800)
REG32(GICINT136_STATUS,     0x804)
REG32(GICINT192_201_EN,         0xB00)
REG32(GICINT192_201_STATUS,     0xB04)

/*
 * INTCIO Registers
 *
 * values below are offset by - 0x100 from datasheet
 * because its memory region is start at 0x100
 *
 */
REG32(GICINT192_EN,         0x00)
REG32(GICINT192_STATUS,     0x04)
REG32(GICINT193_EN,         0x10)
REG32(GICINT193_STATUS,     0x14)
REG32(GICINT194_EN,         0x20)
REG32(GICINT194_STATUS,     0x24)
REG32(GICINT195_EN,         0x30)
REG32(GICINT195_STATUS,     0x34)
REG32(GICINT196_EN,         0x40)
REG32(GICINT196_STATUS,     0x44)
REG32(GICINT197_EN,         0x50)
REG32(GICINT197_STATUS,     0x54)

/*
 * SSP INTC Registers
 */
REG32(SSPINT128_EN,             0x2000)
REG32(SSPINT128_STATUS,         0x2004)
REG32(SSPINT129_EN,             0x2100)
REG32(SSPINT129_STATUS,         0x2104)
REG32(SSPINT130_EN,             0x2200)
REG32(SSPINT130_STATUS,         0x2204)
REG32(SSPINT131_EN,             0x2300)
REG32(SSPINT131_STATUS,         0x2304)
REG32(SSPINT132_EN,             0x2400)
REG32(SSPINT132_STATUS,         0x2404)
REG32(SSPINT133_EN,             0x2500)
REG32(SSPINT133_STATUS,         0x2504)
REG32(SSPINT134_EN,             0x2600)
REG32(SSPINT134_STATUS,         0x2604)
REG32(SSPINT135_EN,             0x2700)
REG32(SSPINT135_STATUS,         0x2704)
REG32(SSPINT136_EN,             0x2800)
REG32(SSPINT136_STATUS,         0x2804)
REG32(SSPINT137_EN,             0x2900)
REG32(SSPINT137_STATUS,         0x2904)
REG32(SSPINT138_EN,             0x2A00)
REG32(SSPINT138_STATUS,         0x2A04)
REG32(SSPINT160_169_EN,         0x2B00)
REG32(SSPINT160_169_STATUS,     0x2B04)

/*
 * SSP INTCIO Registers
 */
REG32(SSPINT160_EN,         0x180)
REG32(SSPINT160_STATUS,     0x184)
REG32(SSPINT161_EN,         0x190)
REG32(SSPINT161_STATUS,     0x194)
REG32(SSPINT162_EN,         0x1A0)
REG32(SSPINT162_STATUS,     0x1A4)
REG32(SSPINT163_EN,         0x1B0)
REG32(SSPINT163_STATUS,     0x1B4)
REG32(SSPINT164_EN,         0x1C0)
REG32(SSPINT164_STATUS,     0x1C4)
REG32(SSPINT165_EN,         0x1D0)
REG32(SSPINT165_STATUS,     0x1D4)

/*
 * TSP INTC Registers
 */
REG32(TSPINT128_EN,             0x3000)
REG32(TSPINT128_STATUS,         0x3004)
REG32(TSPINT129_EN,             0x3100)
REG32(TSPINT129_STATUS,         0x3104)
REG32(TSPINT130_EN,             0x3200)
REG32(TSPINT130_STATUS,         0x3204)
REG32(TSPINT131_EN,             0x3300)
REG32(TSPINT131_STATUS,         0x3304)
REG32(TSPINT132_EN,             0x3400)
REG32(TSPINT132_STATUS,         0x3404)
REG32(TSPINT133_EN,             0x3500)
REG32(TSPINT133_STATUS,         0x3504)
REG32(TSPINT134_EN,             0x3600)
REG32(TSPINT134_STATUS,         0x3604)
REG32(TSPINT135_EN,             0x3700)
REG32(TSPINT135_STATUS,         0x3704)
REG32(TSPINT136_EN,             0x3800)
REG32(TSPINT136_STATUS,         0x3804)
REG32(TSPINT137_EN,             0x3900)
REG32(TSPINT137_STATUS,         0x3904)
REG32(TSPINT138_EN,             0x3A00)
REG32(TSPINT138_STATUS,         0x3A04)
REG32(TSPINT160_169_EN,         0x3B00)
REG32(TSPINT160_169_STATUS,     0x3B04)

/*
 * TSP INTCIO Registers
 */

REG32(TSPINT160_EN,         0x200)
REG32(TSPINT160_STATUS,     0x204)
REG32(TSPINT161_EN,         0x210)
REG32(TSPINT161_STATUS,     0x214)
REG32(TSPINT162_EN,         0x220)
REG32(TSPINT162_STATUS,     0x224)
REG32(TSPINT163_EN,         0x230)
REG32(TSPINT163_STATUS,     0x234)
REG32(TSPINT164_EN,         0x240)
REG32(TSPINT164_STATUS,     0x244)
REG32(TSPINT165_EN,         0x250)
REG32(TSPINT165_STATUS,     0x254)

static const AspeedINTCIRQ *aspeed_intc_get_irq(AspeedINTCClass *aic,
                                                uint32_t reg)
{
    int i;

    for (i = 0; i < aic->irq_table_count; i++) {
        if (aic->irq_table[i].enable_reg == reg ||
            aic->irq_table[i].status_reg == reg) {
            return &aic->irq_table[i];
        }
    }

    /*
     * Invalid reg.
     */
    g_assert_not_reached();
}

/*
 * Update the state of an interrupt controller pin by setting
 * the specified output pin to the given level.
 * The input pin index should be between 0 and the number of input pins.
 * The output pin index should be between 0 and the number of output pins.
 */
static void aspeed_intc_update(AspeedINTCState *s, int inpin_idx,
                               int outpin_idx, int level)
{
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    const char *name = object_get_typename(OBJECT(s));

    assert((outpin_idx < aic->num_outpins) && (inpin_idx < aic->num_inpins));

    trace_aspeed_intc_update_irq(name, inpin_idx, outpin_idx, level);
    qemu_set_irq(s->output_pins[outpin_idx], level);
}

static void aspeed_intc_set_irq_handler(AspeedINTCState *s,
                                        const AspeedINTCIRQ *intc_irq,
                                        uint32_t select)
{
    const char *name = object_get_typename(OBJECT(s));
    uint32_t status_reg;
    int outpin_idx;
    int inpin_idx;

    status_reg = intc_irq->status_reg;
    outpin_idx = intc_irq->outpin_idx;
    inpin_idx = intc_irq->inpin_idx;

    if ((s->mask[inpin_idx] & select) || (s->regs[status_reg] & select)) {
        /*
         * a. mask is not 0 means in ISR mode
         * sources interrupt routine are executing.
         * b. status register value is not 0 means previous
         * source interrupt does not be executed, yet.
         *
         * save source interrupt to pending variable.
         */
        s->pending[inpin_idx] |= select;
        trace_aspeed_intc_pending_irq(name, inpin_idx, s->pending[inpin_idx]);
    } else {
        /*
         * notify firmware which source interrupt are coming
         * by setting status register
         */
        s->regs[status_reg] = select;
        trace_aspeed_intc_trigger_irq(name, inpin_idx, outpin_idx,
                                      s->regs[status_reg]);
        aspeed_intc_update(s, inpin_idx, outpin_idx, 1);
    }
}

static void aspeed_intc_set_irq_handler_multi_outpins(AspeedINTCState *s,
                                 const AspeedINTCIRQ *intc_irq, uint32_t select)
{
    const char *name = object_get_typename(OBJECT(s));
    uint32_t status_reg;
    int num_outpins;
    int outpin_idx;
    int inpin_idx;
    int i;

    num_outpins = intc_irq->num_outpins;
    status_reg = intc_irq->status_reg;
    outpin_idx = intc_irq->outpin_idx;
    inpin_idx = intc_irq->inpin_idx;

    for (i = 0; i < num_outpins; i++) {
        if (select & BIT(i)) {
            if (s->mask[inpin_idx] & BIT(i) ||
                s->regs[status_reg] & BIT(i)) {
                /*
                 * a. mask bit is not 0 means in ISR mode sources interrupt
                 * routine are executing.
                 * b. status bit is not 0 means previous source interrupt
                 * does not be executed, yet.
                 *
                 * save source interrupt to pending bit.
                 */
                 s->pending[inpin_idx] |= BIT(i);
                 trace_aspeed_intc_pending_irq(name, inpin_idx,
                                               s->pending[inpin_idx]);
            } else {
                /*
                 * notify firmware which source interrupt are coming
                 * by setting status bit
                 */
                s->regs[status_reg] |= BIT(i);
                trace_aspeed_intc_trigger_irq(name, inpin_idx, outpin_idx + i,
                                              s->regs[status_reg]);
                aspeed_intc_update(s, inpin_idx, outpin_idx + i, 1);
            }
        }
    }
}

/*
 * GICINT192_201 maps 1:10 to input IRQ 0 and output IRQs 0 to 9.
 * GICINT128 to GICINT136 map 1:1 to input IRQs 1 to 9 and output
 * IRQs 10 to 18. The value of input IRQ should be between 0 and
 * the number of input pins.
 */
static void aspeed_intc_set_irq(void *opaque, int irq, int level)
{
    AspeedINTCState *s = (AspeedINTCState *)opaque;
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    const char *name = object_get_typename(OBJECT(s));
    const AspeedINTCIRQ *intc_irq;
    uint32_t select = 0;
    uint32_t enable;
    int num_outpins;
    int inpin_idx;
    int i;

    assert(irq < aic->num_inpins);

    intc_irq = &aic->irq_table[irq];
    num_outpins = intc_irq->num_outpins;
    inpin_idx = intc_irq->inpin_idx;
    trace_aspeed_intc_set_irq(name, inpin_idx, level);
    enable = s->enable[inpin_idx];

    if (!level) {
        return;
    }

    for (i = 0; i < aic->num_lines; i++) {
        if (s->orgates[inpin_idx].levels[i]) {
            if (enable & BIT(i)) {
                select |= BIT(i);
            }
        }
    }

    if (!select) {
        return;
    }

    trace_aspeed_intc_select(name, select);
    if (num_outpins > 1) {
        aspeed_intc_set_irq_handler_multi_outpins(s, intc_irq, select);
    } else {
        aspeed_intc_set_irq_handler(s, intc_irq, select);
    }
}

static void aspeed_intc_enable_handler(AspeedINTCState *s, hwaddr offset,
                                       uint64_t data)
{
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    const char *name = object_get_typename(OBJECT(s));
    const AspeedINTCIRQ *intc_irq;
    uint32_t reg = offset >> 2;
    uint32_t old_enable;
    uint32_t change;
    int inpin_idx;

    intc_irq = aspeed_intc_get_irq(aic, reg);
    inpin_idx = intc_irq->inpin_idx;

    assert(inpin_idx < aic->num_inpins);

    /*
     * The enable registers are used to enable source interrupts.
     * They also handle masking and unmasking of source interrupts
     * during the execution of the source ISR.
     */

    /* disable all source interrupt */
    if (!data && !s->enable[inpin_idx]) {
        s->regs[reg] = data;
        return;
    }

    old_enable = s->enable[inpin_idx];
    s->enable[inpin_idx] |= data;

    /* enable new source interrupt */
    if (old_enable != s->enable[inpin_idx]) {
        trace_aspeed_intc_enable(name, s->enable[inpin_idx]);
        s->regs[reg] = data;
        return;
    }

    /* mask and unmask source interrupt */
    change = s->regs[reg] ^ data;
    if (change & data) {
        s->mask[inpin_idx] &= ~change;
        trace_aspeed_intc_unmask(name, change, s->mask[inpin_idx]);
    } else {
        s->mask[inpin_idx] |= change;
        trace_aspeed_intc_mask(name, change, s->mask[inpin_idx]);
    }

    s->regs[reg] = data;
}

static void aspeed_intc_status_handler(AspeedINTCState *s, hwaddr offset,
                                       uint64_t data)
{
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    const char *name = object_get_typename(OBJECT(s));
    const AspeedINTCIRQ *intc_irq;
    uint32_t reg = offset >> 2;
    int outpin_idx;
    int inpin_idx;

    if (!data) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid data 0\n", __func__);
        return;
    }

    intc_irq = aspeed_intc_get_irq(aic, reg);
    outpin_idx = intc_irq->outpin_idx;
    inpin_idx = intc_irq->inpin_idx;

    assert(inpin_idx < aic->num_inpins);

    /* clear status */
    s->regs[reg] &= ~data;

    /*
     * These status registers are used for notify sources ISR are executed.
     * If one source ISR is executed, it will clear one bit.
     * If it clear all bits, it means to initialize this register status
     * rather than sources ISR are executed.
     */
    if (data == 0xffffffff) {
        return;
    }

    /* All source ISR execution are done */
    if (!s->regs[reg]) {
        trace_aspeed_intc_all_isr_done(name, inpin_idx);
        if (s->pending[inpin_idx]) {
            /*
             * handle pending source interrupt
             * notify firmware which source interrupt are pending
             * by setting status register
             */
            s->regs[reg] = s->pending[inpin_idx];
            s->pending[inpin_idx] = 0;
            trace_aspeed_intc_trigger_irq(name, inpin_idx, outpin_idx,
                                          s->regs[reg]);
            aspeed_intc_update(s, inpin_idx, outpin_idx, 1);
        } else {
            /* clear irq */
            trace_aspeed_intc_clear_irq(name, inpin_idx, outpin_idx, 0);
            aspeed_intc_update(s, inpin_idx, outpin_idx, 0);
        }
    }
}

static void aspeed_intc_status_handler_multi_outpins(AspeedINTCState *s,
                                                hwaddr offset, uint64_t data)
{
    const char *name = object_get_typename(OBJECT(s));
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    const AspeedINTCIRQ *intc_irq;
    uint32_t reg = offset >> 2;
    int num_outpins;
    int outpin_idx;
    int inpin_idx;
    int i;

    if (!data) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid data 0\n", __func__);
        return;
    }

    intc_irq = aspeed_intc_get_irq(aic, reg);
    num_outpins = intc_irq->num_outpins;
    outpin_idx = intc_irq->outpin_idx;
    inpin_idx = intc_irq->inpin_idx;
    assert(inpin_idx < aic->num_inpins);

    /* clear status */
    s->regs[reg] &= ~data;

    /*
     * The status registers are used for notify sources ISR are executed.
     * If one source ISR is executed, it will clear one bit.
     * If it clear all bits, it means to initialize this register status
     * rather than sources ISR are executed.
     */
    if (data == 0xffffffff) {
        return;
    }

    for (i = 0; i < num_outpins; i++) {
        /* All source ISR executions are done from a specific bit */
        if (data & BIT(i)) {
            trace_aspeed_intc_all_isr_done_bit(name, inpin_idx, i);
            if (s->pending[inpin_idx] & BIT(i)) {
                /*
                 * Handle pending source interrupt.
                 * Notify firmware which source interrupt is pending
                 * by setting the status bit.
                 */
                s->regs[reg] |= BIT(i);
                s->pending[inpin_idx] &= ~BIT(i);
                trace_aspeed_intc_trigger_irq(name, inpin_idx, outpin_idx + i,
                                              s->regs[reg]);
                aspeed_intc_update(s, inpin_idx, outpin_idx + i, 1);
            } else {
                /* clear irq for the specific bit */
                trace_aspeed_intc_clear_irq(name, inpin_idx, outpin_idx + i, 0);
                aspeed_intc_update(s, inpin_idx, outpin_idx + i, 0);
            }
        }
    }
}

static uint64_t aspeed_intc_read(void *opaque, hwaddr offset, unsigned int size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;
    uint32_t value = 0;

    value = s->regs[reg];
    trace_aspeed_intc_read(name, offset, size, value);

    return value;
}

static void aspeed_intc_write(void *opaque, hwaddr offset, uint64_t data,
                                        unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;

    trace_aspeed_intc_write(name, offset, size, data);

    switch (reg) {
    case R_GICINT128_EN:
    case R_GICINT129_EN:
    case R_GICINT130_EN:
    case R_GICINT131_EN:
    case R_GICINT132_EN:
    case R_GICINT133_EN:
    case R_GICINT134_EN:
    case R_GICINT135_EN:
    case R_GICINT136_EN:
    case R_GICINT192_201_EN:
        aspeed_intc_enable_handler(s, offset, data);
        break;
    case R_GICINT128_STATUS:
    case R_GICINT129_STATUS:
    case R_GICINT130_STATUS:
    case R_GICINT131_STATUS:
    case R_GICINT132_STATUS:
    case R_GICINT133_STATUS:
    case R_GICINT134_STATUS:
    case R_GICINT135_STATUS:
    case R_GICINT136_STATUS:
        aspeed_intc_status_handler(s, offset, data);
        break;
    case R_GICINT192_201_STATUS:
        aspeed_intc_status_handler_multi_outpins(s, offset, data);
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static void aspeed_ssp_intc_write(void *opaque, hwaddr offset, uint64_t data,
                                        unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;

    trace_aspeed_intc_write(name, offset, size, data);

    switch (reg) {
    case R_SSPINT128_EN:
    case R_SSPINT129_EN:
    case R_SSPINT130_EN:
    case R_SSPINT131_EN:
    case R_SSPINT132_EN:
    case R_SSPINT133_EN:
    case R_SSPINT134_EN:
    case R_SSPINT135_EN:
    case R_SSPINT136_EN:
    case R_SSPINT160_169_EN:
        aspeed_intc_enable_handler(s, offset, data);
        break;
    case R_SSPINT128_STATUS:
    case R_SSPINT129_STATUS:
    case R_SSPINT130_STATUS:
    case R_SSPINT131_STATUS:
    case R_SSPINT132_STATUS:
    case R_SSPINT133_STATUS:
    case R_SSPINT134_STATUS:
    case R_SSPINT135_STATUS:
    case R_SSPINT136_STATUS:
        aspeed_intc_status_handler(s, offset, data);
        break;
    case R_SSPINT160_169_STATUS:
        aspeed_intc_status_handler_multi_outpins(s, offset, data);
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static void aspeed_tsp_intc_write(void *opaque, hwaddr offset, uint64_t data,
                                        unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;

    trace_aspeed_intc_write(name, offset, size, data);

    switch (reg) {
    case R_TSPINT128_EN:
    case R_TSPINT129_EN:
    case R_TSPINT130_EN:
    case R_TSPINT131_EN:
    case R_TSPINT132_EN:
    case R_TSPINT133_EN:
    case R_TSPINT134_EN:
    case R_TSPINT135_EN:
    case R_TSPINT136_EN:
    case R_TSPINT160_169_EN:
        aspeed_intc_enable_handler(s, offset, data);
        break;
    case R_TSPINT128_STATUS:
    case R_TSPINT129_STATUS:
    case R_TSPINT130_STATUS:
    case R_TSPINT131_STATUS:
    case R_TSPINT132_STATUS:
    case R_TSPINT133_STATUS:
    case R_TSPINT134_STATUS:
    case R_TSPINT135_STATUS:
    case R_TSPINT136_STATUS:
        aspeed_intc_status_handler(s, offset, data);
        break;
    case R_TSPINT160_169_STATUS:
        aspeed_intc_status_handler_multi_outpins(s, offset, data);
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static uint64_t aspeed_intcio_read(void *opaque, hwaddr offset,
                                   unsigned int size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;
    uint32_t value = 0;

    value = s->regs[reg];
    trace_aspeed_intc_read(name, offset, size, value);

    return value;
}

static void aspeed_intcio_write(void *opaque, hwaddr offset, uint64_t data,
                                unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;

    trace_aspeed_intc_write(name, offset, size, data);

    switch (reg) {
    case R_GICINT192_EN:
    case R_GICINT193_EN:
    case R_GICINT194_EN:
    case R_GICINT195_EN:
    case R_GICINT196_EN:
    case R_GICINT197_EN:
        aspeed_intc_enable_handler(s, offset, data);
        break;
    case R_GICINT192_STATUS:
    case R_GICINT193_STATUS:
    case R_GICINT194_STATUS:
    case R_GICINT195_STATUS:
    case R_GICINT196_STATUS:
    case R_GICINT197_STATUS:
        aspeed_intc_status_handler(s, offset, data);
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static void aspeed_ssp_intcio_write(void *opaque, hwaddr offset, uint64_t data,
                                unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;

    trace_aspeed_intc_write(name, offset, size, data);

    switch (reg) {
    case R_SSPINT160_EN:
    case R_SSPINT161_EN:
    case R_SSPINT162_EN:
    case R_SSPINT163_EN:
    case R_SSPINT164_EN:
    case R_SSPINT165_EN:
        aspeed_intc_enable_handler(s, offset, data);
        break;
    case R_SSPINT160_STATUS:
    case R_SSPINT161_STATUS:
    case R_SSPINT162_STATUS:
    case R_SSPINT163_STATUS:
    case R_SSPINT164_STATUS:
    case R_SSPINT165_STATUS:
        aspeed_intc_status_handler(s, offset, data);
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static void aspeed_tsp_intcio_write(void *opaque, hwaddr offset, uint64_t data,
                                unsigned size)
{
    AspeedINTCState *s = ASPEED_INTC(opaque);
    const char *name = object_get_typename(OBJECT(s));
    uint32_t reg = offset >> 2;

    trace_aspeed_intc_write(name, offset, size, data);

    switch (reg) {
    case R_TSPINT160_EN:
    case R_TSPINT161_EN:
    case R_TSPINT162_EN:
    case R_TSPINT163_EN:
    case R_TSPINT164_EN:
    case R_TSPINT165_EN:
        aspeed_intc_enable_handler(s, offset, data);
        break;
    case R_TSPINT160_STATUS:
    case R_TSPINT161_STATUS:
    case R_TSPINT162_STATUS:
    case R_TSPINT163_STATUS:
    case R_TSPINT164_STATUS:
    case R_TSPINT165_STATUS:
        aspeed_intc_status_handler(s, offset, data);
        break;
    default:
        s->regs[reg] = data;
        break;
    }
}

static const MemoryRegionOps aspeed_intc_ops = {
    .read = aspeed_intc_read,
    .write = aspeed_intc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps aspeed_intcio_ops = {
    .read = aspeed_intcio_read,
    .write = aspeed_intcio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps aspeed_ssp_intc_ops = {
    .read = aspeed_intc_read,
    .write = aspeed_ssp_intc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps aspeed_ssp_intcio_ops = {
    .read = aspeed_intcio_read,
    .write = aspeed_ssp_intcio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps aspeed_tsp_intc_ops = {
    .read = aspeed_intc_read,
    .write = aspeed_tsp_intc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps aspeed_tsp_intcio_ops = {
    .read = aspeed_intcio_read,
    .write = aspeed_tsp_intcio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void aspeed_intc_instance_init(Object *obj)
{
    AspeedINTCState *s = ASPEED_INTC(obj);
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    int i;

    assert(aic->num_inpins <= ASPEED_INTC_MAX_INPINS);
    for (i = 0; i < aic->num_inpins; i++) {
        object_initialize_child(obj, "intc-orgates[*]", &s->orgates[i],
                                TYPE_OR_IRQ);
        object_property_set_int(OBJECT(&s->orgates[i]), "num-lines",
                                aic->num_lines, &error_abort);
    }
}

static void aspeed_intc_reset(DeviceState *dev)
{
    AspeedINTCState *s = ASPEED_INTC(dev);
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);

    memset(s->regs, 0, aic->nr_regs << 2);
    memset(s->enable, 0, sizeof(s->enable));
    memset(s->mask, 0, sizeof(s->mask));
    memset(s->pending, 0, sizeof(s->pending));
}

static void aspeed_intc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedINTCState *s = ASPEED_INTC(dev);
    AspeedINTCClass *aic = ASPEED_INTC_GET_CLASS(s);
    int i;

    memory_region_init(&s->iomem_container, OBJECT(s),
            TYPE_ASPEED_INTC ".container", aic->mem_size);

    sysbus_init_mmio(sbd, &s->iomem_container);

    s->regs = g_new(uint32_t, aic->nr_regs);
    memory_region_init_io(&s->iomem, OBJECT(s), aic->reg_ops, s,
                          TYPE_ASPEED_INTC ".regs", aic->nr_regs << 2);

    memory_region_add_subregion(&s->iomem_container, aic->reg_offset,
                                &s->iomem);

    qdev_init_gpio_in(dev, aspeed_intc_set_irq, aic->num_inpins);

    for (i = 0; i < aic->num_inpins; i++) {
        if (!qdev_realize(DEVICE(&s->orgates[i]), NULL, errp)) {
            return;
        }
    }

    for (i = 0; i < aic->num_outpins; i++) {
        sysbus_init_irq(sbd, &s->output_pins[i]);
    }
}

static void aspeed_intc_unrealize(DeviceState *dev)
{
    AspeedINTCState *s = ASPEED_INTC(dev);

    g_free(s->regs);
    s->regs = NULL;
}

static void aspeed_intc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED INTC Controller";
    dc->realize = aspeed_intc_realize;
    dc->unrealize = aspeed_intc_unrealize;
    device_class_set_legacy_reset(dc, aspeed_intc_reset);
    dc->vmsd = NULL;

    aic->reg_ops = &aspeed_intc_ops;
}

static const TypeInfo aspeed_intc_info = {
    .name = TYPE_ASPEED_INTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = aspeed_intc_instance_init,
    .instance_size = sizeof(AspeedINTCState),
    .class_init = aspeed_intc_class_init,
    .class_size = sizeof(AspeedINTCClass),
    .abstract = true,
};

static AspeedINTCIRQ aspeed_2700_intc_irqs[ASPEED_INTC_MAX_INPINS] = {
    {0, 0, 10, R_GICINT192_201_EN, R_GICINT192_201_STATUS},
    {1, 10, 1, R_GICINT128_EN, R_GICINT128_STATUS},
    {2, 11, 1, R_GICINT129_EN, R_GICINT129_STATUS},
    {3, 12, 1, R_GICINT130_EN, R_GICINT130_STATUS},
    {4, 13, 1, R_GICINT131_EN, R_GICINT131_STATUS},
    {5, 14, 1, R_GICINT132_EN, R_GICINT132_STATUS},
    {6, 15, 1, R_GICINT133_EN, R_GICINT133_STATUS},
    {7, 16, 1, R_GICINT134_EN, R_GICINT134_STATUS},
    {8, 17, 1, R_GICINT135_EN, R_GICINT135_STATUS},
    {9, 18, 1, R_GICINT136_EN, R_GICINT136_STATUS},
};

static void aspeed_2700_intc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED 2700 INTC Controller";
    aic->num_lines = 32;
    aic->num_inpins = 10;
    aic->num_outpins = 19;
    aic->mem_size = 0x4000;
    aic->nr_regs = 0xB08 >> 2;
    aic->reg_offset = 0x1000;
    aic->irq_table = aspeed_2700_intc_irqs;
    aic->irq_table_count = ARRAY_SIZE(aspeed_2700_intc_irqs);
}

static const TypeInfo aspeed_2700_intc_info = {
    .name = TYPE_ASPEED_2700_INTC,
    .parent = TYPE_ASPEED_INTC,
    .class_init = aspeed_2700_intc_class_init,
};

static AspeedINTCIRQ aspeed_2700_intcio_irqs[ASPEED_INTC_MAX_INPINS] = {
    {0, 0, 1, R_GICINT192_EN, R_GICINT192_STATUS},
    {1, 1, 1, R_GICINT193_EN, R_GICINT193_STATUS},
    {2, 2, 1, R_GICINT194_EN, R_GICINT194_STATUS},
    {3, 3, 1, R_GICINT195_EN, R_GICINT195_STATUS},
    {4, 4, 1, R_GICINT196_EN, R_GICINT196_STATUS},
    {5, 5, 1, R_GICINT197_EN, R_GICINT197_STATUS},
};

static void aspeed_2700_intcio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED 2700 INTC IO Controller";
    aic->num_lines = 32;
    aic->num_inpins = 6;
    aic->num_outpins = 6;
    aic->mem_size = 0x400;
    aic->nr_regs = 0x58 >> 2;
    aic->reg_offset = 0x100;
    aic->reg_ops = &aspeed_intcio_ops;
    aic->irq_table = aspeed_2700_intcio_irqs;
    aic->irq_table_count = ARRAY_SIZE(aspeed_2700_intcio_irqs);
}

static const TypeInfo aspeed_2700_intcio_info = {
    .name = TYPE_ASPEED_2700_INTCIO,
    .parent = TYPE_ASPEED_INTC,
    .class_init = aspeed_2700_intcio_class_init,
};

static AspeedINTCIRQ aspeed_2700ssp_intc_irqs[ASPEED_INTC_MAX_INPINS] = {
    {0, 0, 10, R_SSPINT160_169_EN, R_SSPINT160_169_STATUS},
    {1, 10, 1, R_SSPINT128_EN, R_SSPINT128_STATUS},
    {2, 11, 1, R_SSPINT129_EN, R_SSPINT129_STATUS},
    {3, 12, 1, R_SSPINT130_EN, R_SSPINT130_STATUS},
    {4, 13, 1, R_SSPINT131_EN, R_SSPINT131_STATUS},
    {5, 14, 1, R_SSPINT132_EN, R_SSPINT132_STATUS},
    {6, 15, 1, R_SSPINT133_EN, R_SSPINT133_STATUS},
    {7, 16, 1, R_SSPINT134_EN, R_SSPINT134_STATUS},
    {8, 17, 1, R_SSPINT135_EN, R_SSPINT135_STATUS},
    {9, 18, 1, R_SSPINT136_EN, R_SSPINT136_STATUS},
};

static void aspeed_2700ssp_intc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED 2700 SSP INTC Controller";
    aic->num_lines = 32;
    aic->num_inpins = 10;
    aic->num_outpins = 19;
    aic->mem_size = 0x4000;
    aic->nr_regs = 0x2B08 >> 2;
    aic->reg_offset = 0x0;
    aic->reg_ops = &aspeed_ssp_intc_ops;
    aic->irq_table = aspeed_2700ssp_intc_irqs;
    aic->irq_table_count = ARRAY_SIZE(aspeed_2700ssp_intc_irqs);
}

static const TypeInfo aspeed_2700ssp_intc_info = {
    .name = TYPE_ASPEED_2700SSP_INTC,
    .parent = TYPE_ASPEED_INTC,
    .class_init = aspeed_2700ssp_intc_class_init,
};

static AspeedINTCIRQ aspeed_2700ssp_intcio_irqs[ASPEED_INTC_MAX_INPINS] = {
    {0, 0, 1, R_SSPINT160_EN, R_SSPINT160_STATUS},
    {1, 1, 1, R_SSPINT161_EN, R_SSPINT161_STATUS},
    {2, 2, 1, R_SSPINT162_EN, R_SSPINT162_STATUS},
    {3, 3, 1, R_SSPINT163_EN, R_SSPINT163_STATUS},
    {4, 4, 1, R_SSPINT164_EN, R_SSPINT164_STATUS},
    {5, 5, 1, R_SSPINT165_EN, R_SSPINT165_STATUS},
};

static void aspeed_2700ssp_intcio_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED 2700 SSP INTC IO Controller";
    aic->num_lines = 32;
    aic->num_inpins = 6;
    aic->num_outpins = 6;
    aic->mem_size = 0x400;
    aic->nr_regs = 0x1d8 >> 2;
    aic->reg_offset = 0;
    aic->reg_ops = &aspeed_ssp_intcio_ops;
    aic->irq_table = aspeed_2700ssp_intcio_irqs;
    aic->irq_table_count = ARRAY_SIZE(aspeed_2700ssp_intcio_irqs);
}

static const TypeInfo aspeed_2700ssp_intcio_info = {
    .name = TYPE_ASPEED_2700SSP_INTCIO,
    .parent = TYPE_ASPEED_INTC,
    .class_init = aspeed_2700ssp_intcio_class_init,
};

static AspeedINTCIRQ aspeed_2700tsp_intc_irqs[ASPEED_INTC_MAX_INPINS] = {
    {0, 0, 10, R_TSPINT160_169_EN, R_TSPINT160_169_STATUS},
    {1, 10, 1, R_TSPINT128_EN, R_TSPINT128_STATUS},
    {2, 11, 1, R_TSPINT129_EN, R_TSPINT129_STATUS},
    {3, 12, 1, R_TSPINT130_EN, R_TSPINT130_STATUS},
    {4, 13, 1, R_TSPINT131_EN, R_TSPINT131_STATUS},
    {5, 14, 1, R_TSPINT132_EN, R_TSPINT132_STATUS},
    {6, 15, 1, R_TSPINT133_EN, R_TSPINT133_STATUS},
    {7, 16, 1, R_TSPINT134_EN, R_TSPINT134_STATUS},
    {8, 17, 1, R_TSPINT135_EN, R_TSPINT135_STATUS},
    {9, 18, 1, R_TSPINT136_EN, R_TSPINT136_STATUS},
};

static void aspeed_2700tsp_intc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED 2700 TSP INTC Controller";
    aic->num_lines = 32;
    aic->num_inpins = 10;
    aic->num_outpins = 19;
    aic->mem_size = 0x4000;
    aic->nr_regs = 0x3B08 >> 2;
    aic->reg_offset = 0;
    aic->reg_ops = &aspeed_tsp_intc_ops;
    aic->irq_table = aspeed_2700tsp_intc_irqs;
    aic->irq_table_count = ARRAY_SIZE(aspeed_2700tsp_intc_irqs);
}

static const TypeInfo aspeed_2700tsp_intc_info = {
    .name = TYPE_ASPEED_2700TSP_INTC,
    .parent = TYPE_ASPEED_INTC,
    .class_init = aspeed_2700tsp_intc_class_init,
};

static AspeedINTCIRQ aspeed_2700tsp_intcio_irqs[ASPEED_INTC_MAX_INPINS] = {
    {0, 0, 1, R_TSPINT160_EN, R_TSPINT160_STATUS},
    {1, 1, 1, R_TSPINT161_EN, R_TSPINT161_STATUS},
    {2, 2, 1, R_TSPINT162_EN, R_TSPINT162_STATUS},
    {3, 3, 1, R_TSPINT163_EN, R_TSPINT163_STATUS},
    {4, 4, 1, R_TSPINT164_EN, R_TSPINT164_STATUS},
    {5, 5, 1, R_TSPINT165_EN, R_TSPINT165_STATUS},
};

static void aspeed_2700tsp_intcio_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedINTCClass *aic = ASPEED_INTC_CLASS(klass);

    dc->desc = "ASPEED 2700 TSP INTC IO Controller";
    aic->num_lines = 32;
    aic->num_inpins = 6;
    aic->num_outpins = 6;
    aic->mem_size = 0x400;
    aic->nr_regs = 0x258 >> 2;
    aic->reg_offset = 0x0;
    aic->reg_ops = &aspeed_tsp_intcio_ops;
    aic->irq_table = aspeed_2700tsp_intcio_irqs;
    aic->irq_table_count = ARRAY_SIZE(aspeed_2700tsp_intcio_irqs);
}

static const TypeInfo aspeed_2700tsp_intcio_info = {
    .name = TYPE_ASPEED_2700TSP_INTCIO,
    .parent = TYPE_ASPEED_INTC,
    .class_init = aspeed_2700tsp_intcio_class_init,
};

static void aspeed_intc_register_types(void)
{
    type_register_static(&aspeed_intc_info);
    type_register_static(&aspeed_2700_intc_info);
    type_register_static(&aspeed_2700_intcio_info);
    type_register_static(&aspeed_2700ssp_intc_info);
    type_register_static(&aspeed_2700ssp_intcio_info);
    type_register_static(&aspeed_2700tsp_intc_info);
    type_register_static(&aspeed_2700tsp_intcio_info);
}

type_init(aspeed_intc_register_types);
