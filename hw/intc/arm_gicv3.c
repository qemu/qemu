/*
 * ARM Generic Interrupt Controller v3
 *
 * Copyright (c) 2015 Huawei.
 * Copyright (c) 2016 Linaro Limited
 * Written by Shlomo Pongratz, Peter Maydell
 *
 * This code is licensed under the GPL, version 2 or (at your option)
 * any later version.
 */

/* This file contains implementation code for an interrupt controller
 * which implements the GICv3 architecture. Specifically this is where
 * the device class itself and the functions for handling interrupts
 * coming in and going out live.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/intc/arm_gicv3.h"
#include "gicv3_internal.h"

static bool irqbetter(GICv3CPUState *cs, int irq, uint8_t prio)
{
    /* Return true if this IRQ at this priority should take
     * precedence over the current recorded highest priority
     * pending interrupt for this CPU. We also return true if
     * the current recorded highest priority pending interrupt
     * is the same as this one (a property which the calling code
     * relies on).
     */
    if (prio < cs->hppi.prio) {
        return true;
    }
    /* If multiple pending interrupts have the same priority then it is an
     * IMPDEF choice which of them to signal to the CPU. We choose to
     * signal the one with the lowest interrupt number.
     */
    if (prio == cs->hppi.prio && irq <= cs->hppi.irq) {
        return true;
    }
    return false;
}

static uint32_t gicd_int_pending(GICv3State *s, int irq)
{
    /* Recalculate which distributor interrupts are actually pending
     * in the group of 32 interrupts starting at irq (which should be a multiple
     * of 32), and return a 32-bit integer which has a bit set for each
     * interrupt that is eligible to be signaled to the CPU interface.
     *
     * An interrupt is pending if:
     *  + the PENDING latch is set OR it is level triggered and the input is 1
     *  + its ENABLE bit is set
     *  + the GICD enable bit for its group is set
     *  + its ACTIVE bit is not set (otherwise it would be Active+Pending)
     * Conveniently we can bulk-calculate this with bitwise operations.
     */
    uint32_t pend, grpmask;
    uint32_t pending = *gic_bmp_ptr32(s->pending, irq);
    uint32_t edge_trigger = *gic_bmp_ptr32(s->edge_trigger, irq);
    uint32_t level = *gic_bmp_ptr32(s->level, irq);
    uint32_t group = *gic_bmp_ptr32(s->group, irq);
    uint32_t grpmod = *gic_bmp_ptr32(s->grpmod, irq);
    uint32_t enable = *gic_bmp_ptr32(s->enabled, irq);
    uint32_t active = *gic_bmp_ptr32(s->active, irq);

    pend = pending | (~edge_trigger & level);
    pend &= enable;
    pend &= ~active;

    if (s->gicd_ctlr & GICD_CTLR_DS) {
        grpmod = 0;
    }

    grpmask = 0;
    if (s->gicd_ctlr & GICD_CTLR_EN_GRP1NS) {
        grpmask |= group;
    }
    if (s->gicd_ctlr & GICD_CTLR_EN_GRP1S) {
        grpmask |= (~group & grpmod);
    }
    if (s->gicd_ctlr & GICD_CTLR_EN_GRP0) {
        grpmask |= (~group & ~grpmod);
    }
    pend &= grpmask;

    return pend;
}

static uint32_t gicr_int_pending(GICv3CPUState *cs)
{
    /* Recalculate which redistributor interrupts are actually pending,
     * and return a 32-bit integer which has a bit set for each interrupt
     * that is eligible to be signaled to the CPU interface.
     *
     * An interrupt is pending if:
     *  + the PENDING latch is set OR it is level triggered and the input is 1
     *  + its ENABLE bit is set
     *  + the GICD enable bit for its group is set
     *  + its ACTIVE bit is not set (otherwise it would be Active+Pending)
     * Conveniently we can bulk-calculate this with bitwise operations.
     */
    uint32_t pend, grpmask, grpmod;

    pend = cs->gicr_ipendr0 | (~cs->edge_trigger & cs->level);
    pend &= cs->gicr_ienabler0;
    pend &= ~cs->gicr_iactiver0;

    if (cs->gic->gicd_ctlr & GICD_CTLR_DS) {
        grpmod = 0;
    } else {
        grpmod = cs->gicr_igrpmodr0;
    }

    grpmask = 0;
    if (cs->gic->gicd_ctlr & GICD_CTLR_EN_GRP1NS) {
        grpmask |= cs->gicr_igroupr0;
    }
    if (cs->gic->gicd_ctlr & GICD_CTLR_EN_GRP1S) {
        grpmask |= (~cs->gicr_igroupr0 & grpmod);
    }
    if (cs->gic->gicd_ctlr & GICD_CTLR_EN_GRP0) {
        grpmask |= (~cs->gicr_igroupr0 & ~grpmod);
    }
    pend &= grpmask;

    return pend;
}

/* Update the interrupt status after state in a redistributor
 * or CPU interface has changed, but don't tell the CPU i/f.
 */
static void gicv3_redist_update_noirqset(GICv3CPUState *cs)
{
    /* Find the highest priority pending interrupt among the
     * redistributor interrupts (SGIs and PPIs).
     */
    bool seenbetter = false;
    uint8_t prio;
    int i;
    uint32_t pend;

    /* Find out which redistributor interrupts are eligible to be
     * signaled to the CPU interface.
     */
    pend = gicr_int_pending(cs);

    if (pend) {
        for (i = 0; i < GIC_INTERNAL; i++) {
            if (!(pend & (1 << i))) {
                continue;
            }
            prio = cs->gicr_ipriorityr[i];
            if (irqbetter(cs, i, prio)) {
                cs->hppi.irq = i;
                cs->hppi.prio = prio;
                seenbetter = true;
            }
        }
    }

    if (seenbetter) {
        cs->hppi.grp = gicv3_irq_group(cs->gic, cs, cs->hppi.irq);
    }

    /* If the best interrupt we just found would preempt whatever
     * was the previous best interrupt before this update, then
     * we know it's definitely the best one now.
     * If we didn't find an interrupt that would preempt the previous
     * best, and the previous best is outside our range (or there was no
     * previous pending interrupt at all), then that is still valid, and
     * we leave it as the best.
     * Otherwise, we need to do a full update (because the previous best
     * interrupt has reduced in priority and any other interrupt could
     * now be the new best one).
     */
    if (!seenbetter && cs->hppi.prio != 0xff && cs->hppi.irq < GIC_INTERNAL) {
        gicv3_full_update_noirqset(cs->gic);
    }
}

/* Update the GIC status after state in a redistributor or
 * CPU interface has changed, and inform the CPU i/f of
 * its new highest priority pending interrupt.
 */
void gicv3_redist_update(GICv3CPUState *cs)
{
    gicv3_redist_update_noirqset(cs);
    gicv3_cpuif_update(cs);
}

/* Update the GIC status after state in the distributor has
 * changed affecting @len interrupts starting at @start,
 * but don't tell the CPU i/f.
 */
static void gicv3_update_noirqset(GICv3State *s, int start, int len)
{
    int i;
    uint8_t prio;
    uint32_t pend = 0;

    assert(start >= GIC_INTERNAL);
    assert(len > 0);

    for (i = 0; i < s->num_cpu; i++) {
        s->cpu[i].seenbetter = false;
    }

    /* Find the highest priority pending interrupt in this range. */
    for (i = start; i < start + len; i++) {
        GICv3CPUState *cs;

        if (i == start || (i & 0x1f) == 0) {
            /* Calculate the next 32 bits worth of pending status */
            pend = gicd_int_pending(s, i & ~0x1f);
        }

        if (!(pend & (1 << (i & 0x1f)))) {
            continue;
        }
        cs = s->gicd_irouter_target[i];
        if (!cs) {
            /* Interrupts targeting no implemented CPU should remain pending
             * and not be forwarded to any CPU.
             */
            continue;
        }
        prio = s->gicd_ipriority[i];
        if (irqbetter(cs, i, prio)) {
            cs->hppi.irq = i;
            cs->hppi.prio = prio;
            cs->seenbetter = true;
        }
    }

    /* If the best interrupt we just found would preempt whatever
     * was the previous best interrupt before this update, then
     * we know it's definitely the best one now.
     * If we didn't find an interrupt that would preempt the previous
     * best, and the previous best is outside our range (or there was
     * no previous pending interrupt at all), then that
     * is still valid, and we leave it as the best.
     * Otherwise, we need to do a full update (because the previous best
     * interrupt has reduced in priority and any other interrupt could
     * now be the new best one).
     */
    for (i = 0; i < s->num_cpu; i++) {
        GICv3CPUState *cs = &s->cpu[i];

        if (cs->seenbetter) {
            cs->hppi.grp = gicv3_irq_group(cs->gic, cs, cs->hppi.irq);
        }

        if (!cs->seenbetter && cs->hppi.prio != 0xff &&
            cs->hppi.irq >= start && cs->hppi.irq < start + len) {
            gicv3_full_update_noirqset(s);
            break;
        }
    }
}

void gicv3_update(GICv3State *s, int start, int len)
{
    int i;

    gicv3_update_noirqset(s, start, len);
    for (i = 0; i < s->num_cpu; i++) {
        gicv3_cpuif_update(&s->cpu[i]);
    }
}

void gicv3_full_update_noirqset(GICv3State *s)
{
    /* Completely recalculate the GIC status from scratch, but
     * don't update any outbound IRQ lines.
     */
    int i;

    for (i = 0; i < s->num_cpu; i++) {
        s->cpu[i].hppi.prio = 0xff;
    }

    /* Note that we can guarantee that these functions will not
     * recursively call back into gicv3_full_update(), because
     * at each point the "previous best" is always outside the
     * range we ask them to update.
     */
    gicv3_update_noirqset(s, GIC_INTERNAL, s->num_irq - GIC_INTERNAL);

    for (i = 0; i < s->num_cpu; i++) {
        gicv3_redist_update_noirqset(&s->cpu[i]);
    }
}

void gicv3_full_update(GICv3State *s)
{
    /* Completely recalculate the GIC status from scratch, including
     * updating outbound IRQ lines.
     */
    int i;

    gicv3_full_update_noirqset(s);
    for (i = 0; i < s->num_cpu; i++) {
        gicv3_cpuif_update(&s->cpu[i]);
    }
}

/* Process a change in an external IRQ input. */
static void gicv3_set_irq(void *opaque, int irq, int level)
{
    /* Meaning of the 'irq' parameter:
     *  [0..N-1] : external interrupts
     *  [N..N+31] : PPI (internal) interrupts for CPU 0
     *  [N+32..N+63] : PPI (internal interrupts for CPU 1
     *  ...
     */
    GICv3State *s = opaque;

    if (irq < (s->num_irq - GIC_INTERNAL)) {
        /* external interrupt (SPI) */
        gicv3_dist_set_irq(s, irq + GIC_INTERNAL, level);
    } else {
        /* per-cpu interrupt (PPI) */
        int cpu;

        irq -= (s->num_irq - GIC_INTERNAL);
        cpu = irq / GIC_INTERNAL;
        irq %= GIC_INTERNAL;
        assert(cpu < s->num_cpu);
        /* Raising SGIs via this function would be a bug in how the board
         * model wires up interrupts.
         */
        assert(irq >= GIC_NR_SGIS);
        gicv3_redist_set_irq(&s->cpu[cpu], irq, level);
    }
}

static void arm_gicv3_post_load(GICv3State *s)
{
    /* Recalculate our cached idea of the current highest priority
     * pending interrupt, but don't set IRQ or FIQ lines.
     */
    gicv3_full_update_noirqset(s);
    /* Repopulate the cache of GICv3CPUState pointers for target CPUs */
    gicv3_cache_all_target_cpustates(s);
}

static const MemoryRegionOps gic_ops[] = {
    {
        .read_with_attrs = gicv3_dist_read,
        .write_with_attrs = gicv3_dist_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
    },
    {
        .read_with_attrs = gicv3_redist_read,
        .write_with_attrs = gicv3_redist_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
    }
};

static void arm_gic_realize(DeviceState *dev, Error **errp)
{
    /* Device instance realize function for the GIC sysbus device */
    GICv3State *s = ARM_GICV3(dev);
    ARMGICv3Class *agc = ARM_GICV3_GET_CLASS(s);
    Error *local_err = NULL;

    agc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->nb_redist_regions != 1) {
        error_setg(errp, "VGICv3 redist region number(%d) not equal to 1",
                   s->nb_redist_regions);
        return;
    }

    gicv3_init_irqs_and_mmio(s, gicv3_set_irq, gic_ops, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    gicv3_init_cpuif(s);
}

static void arm_gicv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_CLASS(klass);
    ARMGICv3Class *agc = ARM_GICV3_CLASS(klass);

    agcc->post_load = arm_gicv3_post_load;
    device_class_set_parent_realize(dc, arm_gic_realize, &agc->parent_realize);
}

static const TypeInfo arm_gicv3_info = {
    .name = TYPE_ARM_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = arm_gicv3_class_init,
    .class_size = sizeof(ARMGICv3Class),
};

static void arm_gicv3_register_types(void)
{
    type_register_static(&arm_gicv3_info);
}

type_init(arm_gicv3_register_types)
