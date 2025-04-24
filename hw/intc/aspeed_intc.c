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


static const MemoryRegionOps aspeed_intc_ops = {
    .read = aspeed_intc_read,
    .write = aspeed_intc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps aspeed_intcio_ops = {
    .read = aspeed_intcio_read,
    .write = aspeed_intcio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
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

static void aspeed_intc_class_init(ObjectClass *klass, void *data)
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

static void aspeed_2700_intc_class_init(ObjectClass *klass, void *data)
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

static void aspeed_2700_intcio_class_init(ObjectClass *klass, void *data)
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

static void aspeed_intc_register_types(void)
{
    type_register_static(&aspeed_intc_info);
    type_register_static(&aspeed_2700_intc_info);
    type_register_static(&aspeed_2700_intcio_info);
}

type_init(aspeed_intc_register_types);
