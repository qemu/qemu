/*
 * ARM Generic Interrupt Controller using KVM in-kernel support
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin
 * Based on vGICv2 code by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "kvm_arm.h"
#include "gicv3_internal.h"
#include "vgic_common.h"
#include "migration/blocker.h"

#ifdef DEBUG_GICV3_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "kvm_gicv3: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define TYPE_KVM_ARM_GICV3 "kvm-arm-gicv3"
#define KVM_ARM_GICV3(obj) \
     OBJECT_CHECK(GICv3State, (obj), TYPE_KVM_ARM_GICV3)
#define KVM_ARM_GICV3_CLASS(klass) \
     OBJECT_CLASS_CHECK(KVMARMGICv3Class, (klass), TYPE_KVM_ARM_GICV3)
#define KVM_ARM_GICV3_GET_CLASS(obj) \
     OBJECT_GET_CLASS(KVMARMGICv3Class, (obj), TYPE_KVM_ARM_GICV3)

#define   KVM_DEV_ARM_VGIC_SYSREG(op0, op1, crn, crm, op2)         \
                             (ARM64_SYS_REG_SHIFT_MASK(op0, OP0) | \
                              ARM64_SYS_REG_SHIFT_MASK(op1, OP1) | \
                              ARM64_SYS_REG_SHIFT_MASK(crn, CRN) | \
                              ARM64_SYS_REG_SHIFT_MASK(crm, CRM) | \
                              ARM64_SYS_REG_SHIFT_MASK(op2, OP2))

#define ICC_PMR_EL1     \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 4, 6, 0)
#define ICC_BPR0_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 8, 3)
#define ICC_AP0R_EL1(n) \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 8, 4 | n)
#define ICC_AP1R_EL1(n) \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 9, n)
#define ICC_BPR1_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 3)
#define ICC_CTLR_EL1    \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 4)
#define ICC_SRE_EL1 \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 5)
#define ICC_IGRPEN0_EL1 \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 6)
#define ICC_IGRPEN1_EL1 \
    KVM_DEV_ARM_VGIC_SYSREG(3, 0, 12, 12, 7)

typedef struct KVMARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    void (*parent_reset)(DeviceState *dev);
} KVMARMGICv3Class;

static void kvm_arm_gicv3_set_irq(void *opaque, int irq, int level)
{
    GICv3State *s = (GICv3State *)opaque;

    kvm_arm_gic_set_irq(s->num_irq, irq, level);
}

#define KVM_VGIC_ATTR(reg, typer) \
    ((typer & KVM_DEV_ARM_VGIC_V3_MPIDR_MASK) | (reg))

static inline void kvm_gicd_access(GICv3State *s, int offset,
                                   uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
                      KVM_VGIC_ATTR(offset, 0),
                      val, write, &error_abort);
}

static inline void kvm_gicr_access(GICv3State *s, int offset, int cpu,
                                   uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_REDIST_REGS,
                      KVM_VGIC_ATTR(offset, s->cpu[cpu].gicr_typer),
                      val, write, &error_abort);
}

static inline void kvm_gicc_access(GICv3State *s, uint64_t reg, int cpu,
                                   uint64_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
                      KVM_VGIC_ATTR(reg, s->cpu[cpu].gicr_typer),
                      val, write, &error_abort);
}

static inline void kvm_gic_line_level_access(GICv3State *s, int irq, int cpu,
                                             uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO,
                      KVM_VGIC_ATTR(irq, s->cpu[cpu].gicr_typer) |
                      (VGIC_LEVEL_INFO_LINE_LEVEL <<
                       KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_SHIFT),
                      val, write, &error_abort);
}

/* Loop through each distributor IRQ related register; since bits
 * corresponding to SPIs and PPIs are RAZ/WI when affinity routing
 * is enabled, we skip those.
 */
#define for_each_dist_irq_reg(_irq, _max, _field_width) \
    for (_irq = GIC_INTERNAL; _irq < _max; _irq += (32 / _field_width))

static void kvm_dist_get_priority(GICv3State *s, uint32_t offset, uint8_t *bmp)
{
    uint32_t reg, *field;
    int irq;

    /* For the KVM GICv3, affinity routing is always enabled, and the first 8
     * GICD_IPRIORITYR<n> registers are always RAZ/WI. The corresponding
     * functionality is replaced by GICR_IPRIORITYR<n>. It doesn't need to
     * sync them. So it needs to skip the field of GIC_INTERNAL irqs in bmp and
     * offset.
     */
    field = (uint32_t *)(bmp + GIC_INTERNAL);
    offset += (GIC_INTERNAL * 8) / 8;
    for_each_dist_irq_reg(irq, s->num_irq, 8) {
        kvm_gicd_access(s, offset, &reg, false);
        *field = reg;
        offset += 4;
        field++;
    }
}

static void kvm_dist_put_priority(GICv3State *s, uint32_t offset, uint8_t *bmp)
{
    uint32_t reg, *field;
    int irq;

    /* For the KVM GICv3, affinity routing is always enabled, and the first 8
     * GICD_IPRIORITYR<n> registers are always RAZ/WI. The corresponding
     * functionality is replaced by GICR_IPRIORITYR<n>. It doesn't need to
     * sync them. So it needs to skip the field of GIC_INTERNAL irqs in bmp and
     * offset.
     */
    field = (uint32_t *)(bmp + GIC_INTERNAL);
    offset += (GIC_INTERNAL * 8) / 8;
    for_each_dist_irq_reg(irq, s->num_irq, 8) {
        reg = *field;
        kvm_gicd_access(s, offset, &reg, true);
        offset += 4;
        field++;
    }
}

static void kvm_dist_get_edge_trigger(GICv3State *s, uint32_t offset,
                                      uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    /* For the KVM GICv3, affinity routing is always enabled, and the first 2
     * GICD_ICFGR<n> registers are always RAZ/WI. The corresponding
     * functionality is replaced by GICR_ICFGR<n>. It doesn't need to sync
     * them. So it should increase the offset to skip GIC_INTERNAL irqs.
     * This matches the for_each_dist_irq_reg() macro which also skips the
     * first GIC_INTERNAL irqs.
     */
    offset += (GIC_INTERNAL * 2) / 8;
    for_each_dist_irq_reg(irq, s->num_irq, 2) {
        kvm_gicd_access(s, offset, &reg, false);
        reg = half_unshuffle32(reg >> 1);
        if (irq % 32 != 0) {
            reg = (reg << 16);
        }
        *gic_bmp_ptr32(bmp, irq) |=  reg;
        offset += 4;
    }
}

static void kvm_dist_put_edge_trigger(GICv3State *s, uint32_t offset,
                                      uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    /* For the KVM GICv3, affinity routing is always enabled, and the first 2
     * GICD_ICFGR<n> registers are always RAZ/WI. The corresponding
     * functionality is replaced by GICR_ICFGR<n>. It doesn't need to sync
     * them. So it should increase the offset to skip GIC_INTERNAL irqs.
     * This matches the for_each_dist_irq_reg() macro which also skips the
     * first GIC_INTERNAL irqs.
     */
    offset += (GIC_INTERNAL * 2) / 8;
    for_each_dist_irq_reg(irq, s->num_irq, 2) {
        reg = *gic_bmp_ptr32(bmp, irq);
        if (irq % 32 != 0) {
            reg = (reg & 0xffff0000) >> 16;
        } else {
            reg = reg & 0xffff;
        }
        reg = half_shuffle32(reg) << 1;
        kvm_gicd_access(s, offset, &reg, true);
        offset += 4;
    }
}

static void kvm_gic_get_line_level_bmp(GICv3State *s, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        kvm_gic_line_level_access(s, irq, 0, &reg, false);
        *gic_bmp_ptr32(bmp, irq) = reg;
    }
}

static void kvm_gic_put_line_level_bmp(GICv3State *s, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        reg = *gic_bmp_ptr32(bmp, irq);
        kvm_gic_line_level_access(s, irq, 0, &reg, true);
    }
}

/* Read a bitmap register group from the kernel VGIC. */
static void kvm_dist_getbmp(GICv3State *s, uint32_t offset, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    /* For the KVM GICv3, affinity routing is always enabled, and the
     * GICD_IGROUPR0/GICD_IGRPMODR0/GICD_ISENABLER0/GICD_ISPENDR0/
     * GICD_ISACTIVER0 registers are always RAZ/WI. The corresponding
     * functionality is replaced by the GICR registers. It doesn't need to sync
     * them. So it should increase the offset to skip GIC_INTERNAL irqs.
     * This matches the for_each_dist_irq_reg() macro which also skips the
     * first GIC_INTERNAL irqs.
     */
    offset += (GIC_INTERNAL * 1) / 8;
    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        kvm_gicd_access(s, offset, &reg, false);
        *gic_bmp_ptr32(bmp, irq) = reg;
        offset += 4;
    }
}

static void kvm_dist_putbmp(GICv3State *s, uint32_t offset,
                            uint32_t clroffset, uint32_t *bmp)
{
    uint32_t reg;
    int irq;

    /* For the KVM GICv3, affinity routing is always enabled, and the
     * GICD_IGROUPR0/GICD_IGRPMODR0/GICD_ISENABLER0/GICD_ISPENDR0/
     * GICD_ISACTIVER0 registers are always RAZ/WI. The corresponding
     * functionality is replaced by the GICR registers. It doesn't need to sync
     * them. So it should increase the offset and clroffset to skip GIC_INTERNAL
     * irqs. This matches the for_each_dist_irq_reg() macro which also skips the
     * first GIC_INTERNAL irqs.
     */
    offset += (GIC_INTERNAL * 1) / 8;
    if (clroffset != 0) {
        clroffset += (GIC_INTERNAL * 1) / 8;
    }

    for_each_dist_irq_reg(irq, s->num_irq, 1) {
        /* If this bitmap is a set/clear register pair, first write to the
         * clear-reg to clear all bits before using the set-reg to write
         * the 1 bits.
         */
        if (clroffset != 0) {
            reg = 0;
            kvm_gicd_access(s, clroffset, &reg, true);
            clroffset += 4;
        }
        reg = *gic_bmp_ptr32(bmp, irq);
        kvm_gicd_access(s, offset, &reg, true);
        offset += 4;
    }
}

static void kvm_arm_gicv3_check(GICv3State *s)
{
    uint32_t reg;
    uint32_t num_irq;

    /* Sanity checking s->num_irq */
    kvm_gicd_access(s, GICD_TYPER, &reg, false);
    num_irq = ((reg & 0x1f) + 1) * 32;

    if (num_irq < s->num_irq) {
        error_report("Model requests %u IRQs, but kernel supports max %u",
                     s->num_irq, num_irq);
        abort();
    }
}

static void kvm_arm_gicv3_put(GICv3State *s)
{
    uint32_t regl, regh, reg;
    uint64_t reg64, redist_typer;
    int ncpu, i;

    kvm_arm_gicv3_check(s);

    kvm_gicr_access(s, GICR_TYPER, 0, &regl, false);
    kvm_gicr_access(s, GICR_TYPER + 4, 0, &regh, false);
    redist_typer = ((uint64_t)regh << 32) | regl;

    reg = s->gicd_ctlr;
    kvm_gicd_access(s, GICD_CTLR, &reg, true);

    if (redist_typer & GICR_TYPER_PLPIS) {
        /*
         * Restore base addresses before LPIs are potentially enabled by
         * GICR_CTLR write
         */
        for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
            GICv3CPUState *c = &s->cpu[ncpu];

            reg64 = c->gicr_propbaser;
            regl = (uint32_t)reg64;
            kvm_gicr_access(s, GICR_PROPBASER, ncpu, &regl, true);
            regh = (uint32_t)(reg64 >> 32);
            kvm_gicr_access(s, GICR_PROPBASER + 4, ncpu, &regh, true);

            reg64 = c->gicr_pendbaser;
            regl = (uint32_t)reg64;
            kvm_gicr_access(s, GICR_PENDBASER, ncpu, &regl, true);
            regh = (uint32_t)(reg64 >> 32);
            kvm_gicr_access(s, GICR_PENDBASER + 4, ncpu, &regh, true);
        }
    }

    /* Redistributor state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        reg = c->gicr_ctlr;
        kvm_gicr_access(s, GICR_CTLR, ncpu, &reg, true);

        reg = c->gicr_statusr[GICV3_NS];
        kvm_gicr_access(s, GICR_STATUSR, ncpu, &reg, true);

        reg = c->gicr_waker;
        kvm_gicr_access(s, GICR_WAKER, ncpu, &reg, true);

        reg = c->gicr_igroupr0;
        kvm_gicr_access(s, GICR_IGROUPR0, ncpu, &reg, true);

        reg = ~0;
        kvm_gicr_access(s, GICR_ICENABLER0, ncpu, &reg, true);
        reg = c->gicr_ienabler0;
        kvm_gicr_access(s, GICR_ISENABLER0, ncpu, &reg, true);

        /* Restore config before pending so we treat level/edge correctly */
        reg = half_shuffle32(c->edge_trigger >> 16) << 1;
        kvm_gicr_access(s, GICR_ICFGR1, ncpu, &reg, true);

        reg = c->level;
        kvm_gic_line_level_access(s, 0, ncpu, &reg, true);

        reg = ~0;
        kvm_gicr_access(s, GICR_ICPENDR0, ncpu, &reg, true);
        reg = c->gicr_ipendr0;
        kvm_gicr_access(s, GICR_ISPENDR0, ncpu, &reg, true);

        reg = ~0;
        kvm_gicr_access(s, GICR_ICACTIVER0, ncpu, &reg, true);
        reg = c->gicr_iactiver0;
        kvm_gicr_access(s, GICR_ISACTIVER0, ncpu, &reg, true);

        for (i = 0; i < GIC_INTERNAL; i += 4) {
            reg = c->gicr_ipriorityr[i] |
                (c->gicr_ipriorityr[i + 1] << 8) |
                (c->gicr_ipriorityr[i + 2] << 16) |
                (c->gicr_ipriorityr[i + 3] << 24);
            kvm_gicr_access(s, GICR_IPRIORITYR + i, ncpu, &reg, true);
        }
    }

    /* Distributor state (shared between all CPUs */
    reg = s->gicd_statusr[GICV3_NS];
    kvm_gicd_access(s, GICD_STATUSR, &reg, true);

    /* s->enable bitmap -> GICD_ISENABLERn */
    kvm_dist_putbmp(s, GICD_ISENABLER, GICD_ICENABLER, s->enabled);

    /* s->group bitmap -> GICD_IGROUPRn */
    kvm_dist_putbmp(s, GICD_IGROUPR, 0, s->group);

    /* Restore targets before pending to ensure the pending state is set on
     * the appropriate CPU interfaces in the kernel
     */

    /* s->gicd_irouter[irq] -> GICD_IROUTERn
     * We can't use kvm_dist_put() here because the registers are 64-bit
     */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {
        uint32_t offset;

        offset = GICD_IROUTER + (sizeof(uint32_t) * i);
        reg = (uint32_t)s->gicd_irouter[i];
        kvm_gicd_access(s, offset, &reg, true);

        offset = GICD_IROUTER + (sizeof(uint32_t) * i) + 4;
        reg = (uint32_t)(s->gicd_irouter[i] >> 32);
        kvm_gicd_access(s, offset, &reg, true);
    }

    /* s->trigger bitmap -> GICD_ICFGRn
     * (restore configuration registers before pending IRQs so we treat
     * level/edge correctly)
     */
    kvm_dist_put_edge_trigger(s, GICD_ICFGR, s->edge_trigger);

    /* s->level bitmap ->  line_level */
    kvm_gic_put_line_level_bmp(s, s->level);

    /* s->pending bitmap -> GICD_ISPENDRn */
    kvm_dist_putbmp(s, GICD_ISPENDR, GICD_ICPENDR, s->pending);

    /* s->active bitmap -> GICD_ISACTIVERn */
    kvm_dist_putbmp(s, GICD_ISACTIVER, GICD_ICACTIVER, s->active);

    /* s->gicd_ipriority[] -> GICD_IPRIORITYRn */
    kvm_dist_put_priority(s, GICD_IPRIORITYR, s->gicd_ipriority);

    /* CPU Interface state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];
        int num_pri_bits;

        kvm_gicc_access(s, ICC_SRE_EL1, ncpu, &c->icc_sre_el1, true);
        kvm_gicc_access(s, ICC_CTLR_EL1, ncpu,
                        &c->icc_ctlr_el1[GICV3_NS], true);
        kvm_gicc_access(s, ICC_IGRPEN0_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G0], true);
        kvm_gicc_access(s, ICC_IGRPEN1_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G1NS], true);
        kvm_gicc_access(s, ICC_PMR_EL1, ncpu, &c->icc_pmr_el1, true);
        kvm_gicc_access(s, ICC_BPR0_EL1, ncpu, &c->icc_bpr[GICV3_G0], true);
        kvm_gicc_access(s, ICC_BPR1_EL1, ncpu, &c->icc_bpr[GICV3_G1NS], true);

        num_pri_bits = ((c->icc_ctlr_el1[GICV3_NS] &
                        ICC_CTLR_EL1_PRIBITS_MASK) >>
                        ICC_CTLR_EL1_PRIBITS_SHIFT) + 1;

        switch (num_pri_bits) {
        case 7:
            reg64 = c->icc_apr[GICV3_G0][3];
            kvm_gicc_access(s, ICC_AP0R_EL1(3), ncpu, &reg64, true);
            reg64 = c->icc_apr[GICV3_G0][2];
            kvm_gicc_access(s, ICC_AP0R_EL1(2), ncpu, &reg64, true);
        case 6:
            reg64 = c->icc_apr[GICV3_G0][1];
            kvm_gicc_access(s, ICC_AP0R_EL1(1), ncpu, &reg64, true);
        default:
            reg64 = c->icc_apr[GICV3_G0][0];
            kvm_gicc_access(s, ICC_AP0R_EL1(0), ncpu, &reg64, true);
        }

        switch (num_pri_bits) {
        case 7:
            reg64 = c->icc_apr[GICV3_G1NS][3];
            kvm_gicc_access(s, ICC_AP1R_EL1(3), ncpu, &reg64, true);
            reg64 = c->icc_apr[GICV3_G1NS][2];
            kvm_gicc_access(s, ICC_AP1R_EL1(2), ncpu, &reg64, true);
        case 6:
            reg64 = c->icc_apr[GICV3_G1NS][1];
            kvm_gicc_access(s, ICC_AP1R_EL1(1), ncpu, &reg64, true);
        default:
            reg64 = c->icc_apr[GICV3_G1NS][0];
            kvm_gicc_access(s, ICC_AP1R_EL1(0), ncpu, &reg64, true);
        }
    }
}

static void kvm_arm_gicv3_get(GICv3State *s)
{
    uint32_t regl, regh, reg;
    uint64_t reg64, redist_typer;
    int ncpu, i;

    kvm_arm_gicv3_check(s);

    kvm_gicr_access(s, GICR_TYPER, 0, &regl, false);
    kvm_gicr_access(s, GICR_TYPER + 4, 0, &regh, false);
    redist_typer = ((uint64_t)regh << 32) | regl;

    kvm_gicd_access(s, GICD_CTLR, &reg, false);
    s->gicd_ctlr = reg;

    /* Redistributor state (one per CPU) */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];

        kvm_gicr_access(s, GICR_CTLR, ncpu, &reg, false);
        c->gicr_ctlr = reg;

        kvm_gicr_access(s, GICR_STATUSR, ncpu, &reg, false);
        c->gicr_statusr[GICV3_NS] = reg;

        kvm_gicr_access(s, GICR_WAKER, ncpu, &reg, false);
        c->gicr_waker = reg;

        kvm_gicr_access(s, GICR_IGROUPR0, ncpu, &reg, false);
        c->gicr_igroupr0 = reg;
        kvm_gicr_access(s, GICR_ISENABLER0, ncpu, &reg, false);
        c->gicr_ienabler0 = reg;
        kvm_gicr_access(s, GICR_ICFGR1, ncpu, &reg, false);
        c->edge_trigger = half_unshuffle32(reg >> 1) << 16;
        kvm_gic_line_level_access(s, 0, ncpu, &reg, false);
        c->level = reg;
        kvm_gicr_access(s, GICR_ISPENDR0, ncpu, &reg, false);
        c->gicr_ipendr0 = reg;
        kvm_gicr_access(s, GICR_ISACTIVER0, ncpu, &reg, false);
        c->gicr_iactiver0 = reg;

        for (i = 0; i < GIC_INTERNAL; i += 4) {
            kvm_gicr_access(s, GICR_IPRIORITYR + i, ncpu, &reg, false);
            c->gicr_ipriorityr[i] = extract32(reg, 0, 8);
            c->gicr_ipriorityr[i + 1] = extract32(reg, 8, 8);
            c->gicr_ipriorityr[i + 2] = extract32(reg, 16, 8);
            c->gicr_ipriorityr[i + 3] = extract32(reg, 24, 8);
        }
    }

    if (redist_typer & GICR_TYPER_PLPIS) {
        for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
            GICv3CPUState *c = &s->cpu[ncpu];

            kvm_gicr_access(s, GICR_PROPBASER, ncpu, &regl, false);
            kvm_gicr_access(s, GICR_PROPBASER + 4, ncpu, &regh, false);
            c->gicr_propbaser = ((uint64_t)regh << 32) | regl;

            kvm_gicr_access(s, GICR_PENDBASER, ncpu, &regl, false);
            kvm_gicr_access(s, GICR_PENDBASER + 4, ncpu, &regh, false);
            c->gicr_pendbaser = ((uint64_t)regh << 32) | regl;
        }
    }

    /* Distributor state (shared between all CPUs */

    kvm_gicd_access(s, GICD_STATUSR, &reg, false);
    s->gicd_statusr[GICV3_NS] = reg;

    /* GICD_IGROUPRn -> s->group bitmap */
    kvm_dist_getbmp(s, GICD_IGROUPR, s->group);

    /* GICD_ISENABLERn -> s->enabled bitmap */
    kvm_dist_getbmp(s, GICD_ISENABLER, s->enabled);

    /* Line level of irq */
    kvm_gic_get_line_level_bmp(s, s->level);
    /* GICD_ISPENDRn -> s->pending bitmap */
    kvm_dist_getbmp(s, GICD_ISPENDR, s->pending);

    /* GICD_ISACTIVERn -> s->active bitmap */
    kvm_dist_getbmp(s, GICD_ISACTIVER, s->active);

    /* GICD_ICFGRn -> s->trigger bitmap */
    kvm_dist_get_edge_trigger(s, GICD_ICFGR, s->edge_trigger);

    /* GICD_IPRIORITYRn -> s->gicd_ipriority[] */
    kvm_dist_get_priority(s, GICD_IPRIORITYR, s->gicd_ipriority);

    /* GICD_IROUTERn -> s->gicd_irouter[irq] */
    for (i = GIC_INTERNAL; i < s->num_irq; i++) {
        uint32_t offset;

        offset = GICD_IROUTER + (sizeof(uint32_t) * i);
        kvm_gicd_access(s, offset, &regl, false);
        offset = GICD_IROUTER + (sizeof(uint32_t) * i) + 4;
        kvm_gicd_access(s, offset, &regh, false);
        s->gicd_irouter[i] = ((uint64_t)regh << 32) | regl;
    }

    /*****************************************************************
     * CPU Interface(s) State
     */

    for (ncpu = 0; ncpu < s->num_cpu; ncpu++) {
        GICv3CPUState *c = &s->cpu[ncpu];
        int num_pri_bits;

        kvm_gicc_access(s, ICC_SRE_EL1, ncpu, &c->icc_sre_el1, false);
        kvm_gicc_access(s, ICC_CTLR_EL1, ncpu,
                        &c->icc_ctlr_el1[GICV3_NS], false);
        kvm_gicc_access(s, ICC_IGRPEN0_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G0], false);
        kvm_gicc_access(s, ICC_IGRPEN1_EL1, ncpu,
                        &c->icc_igrpen[GICV3_G1NS], false);
        kvm_gicc_access(s, ICC_PMR_EL1, ncpu, &c->icc_pmr_el1, false);
        kvm_gicc_access(s, ICC_BPR0_EL1, ncpu, &c->icc_bpr[GICV3_G0], false);
        kvm_gicc_access(s, ICC_BPR1_EL1, ncpu, &c->icc_bpr[GICV3_G1NS], false);
        num_pri_bits = ((c->icc_ctlr_el1[GICV3_NS] &
                        ICC_CTLR_EL1_PRIBITS_MASK) >>
                        ICC_CTLR_EL1_PRIBITS_SHIFT) + 1;

        switch (num_pri_bits) {
        case 7:
            kvm_gicc_access(s, ICC_AP0R_EL1(3), ncpu, &reg64, false);
            c->icc_apr[GICV3_G0][3] = reg64;
            kvm_gicc_access(s, ICC_AP0R_EL1(2), ncpu, &reg64, false);
            c->icc_apr[GICV3_G0][2] = reg64;
        case 6:
            kvm_gicc_access(s, ICC_AP0R_EL1(1), ncpu, &reg64, false);
            c->icc_apr[GICV3_G0][1] = reg64;
        default:
            kvm_gicc_access(s, ICC_AP0R_EL1(0), ncpu, &reg64, false);
            c->icc_apr[GICV3_G0][0] = reg64;
        }

        switch (num_pri_bits) {
        case 7:
            kvm_gicc_access(s, ICC_AP1R_EL1(3), ncpu, &reg64, false);
            c->icc_apr[GICV3_G1NS][3] = reg64;
            kvm_gicc_access(s, ICC_AP1R_EL1(2), ncpu, &reg64, false);
            c->icc_apr[GICV3_G1NS][2] = reg64;
        case 6:
            kvm_gicc_access(s, ICC_AP1R_EL1(1), ncpu, &reg64, false);
            c->icc_apr[GICV3_G1NS][1] = reg64;
        default:
            kvm_gicc_access(s, ICC_AP1R_EL1(0), ncpu, &reg64, false);
            c->icc_apr[GICV3_G1NS][0] = reg64;
        }
    }
}

static void arm_gicv3_icc_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    GICv3State *s;
    GICv3CPUState *c;

    c = (GICv3CPUState *)env->gicv3state;
    s = c->gic;

    c->icc_pmr_el1 = 0;
    c->icc_bpr[GICV3_G0] = GIC_MIN_BPR;
    c->icc_bpr[GICV3_G1] = GIC_MIN_BPR;
    c->icc_bpr[GICV3_G1NS] = GIC_MIN_BPR;

    c->icc_sre_el1 = 0x7;
    memset(c->icc_apr, 0, sizeof(c->icc_apr));
    memset(c->icc_igrpen, 0, sizeof(c->icc_igrpen));

    if (s->migration_blocker) {
        return;
    }

    /* Initialize to actual HW supported configuration */
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS,
                      KVM_VGIC_ATTR(ICC_CTLR_EL1, c->gicr_typer),
                      &c->icc_ctlr_el1[GICV3_NS], false, &error_abort);

    c->icc_ctlr_el1[GICV3_S] = c->icc_ctlr_el1[GICV3_NS];
}

static void kvm_arm_gicv3_reset(DeviceState *dev)
{
    GICv3State *s = ARM_GICV3_COMMON(dev);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_GET_CLASS(s);

    DPRINTF("Reset\n");

    kgc->parent_reset(dev);

    if (s->migration_blocker) {
        DPRINTF("Cannot put kernel gic state, no kernel interface\n");
        return;
    }

    kvm_arm_gicv3_put(s);
}

/*
 * CPU interface registers of GIC needs to be reset on CPU reset.
 * For the calling arm_gicv3_icc_reset() on CPU reset, we register
 * below ARMCPRegInfo. As we reset the whole cpu interface under single
 * register reset, we define only one register of CPU interface instead
 * of defining all the registers.
 */
static const ARMCPRegInfo gicv3_cpuif_reginfo[] = {
    { .name = "ICC_CTLR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 12, .opc2 = 4,
      /*
       * If ARM_CP_NOP is used, resetfn is not called,
       * So ARM_CP_NO_RAW is appropriate type.
       */
      .type = ARM_CP_NO_RAW,
      .access = PL1_RW,
      .readfn = arm_cp_read_zero,
      .writefn = arm_cp_write_ignore,
      /*
       * We hang the whole cpu interface reset routine off here
       * rather than parcelling it out into one little function
       * per register
       */
      .resetfn = arm_gicv3_icc_reset,
    },
    REGINFO_SENTINEL
};

/**
 * vm_change_state_handler - VM change state callback aiming at flushing
 * RDIST pending tables into guest RAM
 *
 * The tables get flushed to guest RAM whenever the VM gets stopped.
 */
static void vm_change_state_handler(void *opaque, int running,
                                    RunState state)
{
    GICv3State *s = (GICv3State *)opaque;
    Error *err = NULL;
    int ret;

    if (running) {
        return;
    }

    ret = kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                           KVM_DEV_ARM_VGIC_SAVE_PENDING_TABLES,
                           NULL, true, &err);
    if (err) {
        error_report_err(err);
    }
    if (ret < 0 && ret != -EFAULT) {
        abort();
    }
}


static void kvm_arm_gicv3_realize(DeviceState *dev, Error **errp)
{
    GICv3State *s = KVM_ARM_GICV3(dev);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_GET_CLASS(s);
    bool multiple_redist_region_allowed;
    Error *local_err = NULL;
    int i;

    DPRINTF("kvm_arm_gicv3_realize\n");

    kgc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->security_extn) {
        error_setg(errp, "the in-kernel VGICv3 does not implement the "
                   "security extensions");
        return;
    }

    gicv3_init_irqs_and_mmio(s, kvm_arm_gicv3_set_irq, NULL, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    for (i = 0; i < s->num_cpu; i++) {
        ARMCPU *cpu = ARM_CPU(qemu_get_cpu(i));

        define_arm_cp_regs(cpu, gicv3_cpuif_reginfo);
    }

    /* Try to create the device via the device control API */
    s->dev_fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_ARM_VGIC_V3, false);
    if (s->dev_fd < 0) {
        error_setg_errno(errp, -s->dev_fd, "error creating in-kernel VGIC");
        return;
    }

    multiple_redist_region_allowed =
        kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
                              KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION);

    if (!multiple_redist_region_allowed && s->nb_redist_regions > 1) {
        error_setg(errp, "Multiple VGICv3 redistributor regions are not "
                   "supported by this host kernel");
        error_append_hint(errp, "A maximum of %d VCPUs can be used",
                          s->redist_region_count[0]);
        return;
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
                      0, &s->num_irq, true, &error_abort);

    /* Tell the kernel to complete VGIC initialization now */
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                      KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true, &error_abort);

    kvm_arm_register_device(&s->iomem_dist, -1, KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_V3_ADDR_TYPE_DIST, s->dev_fd, 0);

    if (!multiple_redist_region_allowed) {
        kvm_arm_register_device(&s->iomem_redist[0], -1,
                                KVM_DEV_ARM_VGIC_GRP_ADDR,
                                KVM_VGIC_V3_ADDR_TYPE_REDIST, s->dev_fd, 0);
    } else {
        /* we register regions in reverse order as "devices" are inserted at
         * the head of a QSLIST and the list is then popped from the head
         * onwards by kvm_arm_machine_init_done()
         */
        for (i = s->nb_redist_regions - 1; i >= 0; i--) {
            /* Address mask made of the rdist region index and count */
            uint64_t addr_ormask =
                        i | ((uint64_t)s->redist_region_count[i] << 52);

            kvm_arm_register_device(&s->iomem_redist[i], -1,
                                    KVM_DEV_ARM_VGIC_GRP_ADDR,
                                    KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION,
                                    s->dev_fd, addr_ormask);
        }
    }

    if (kvm_has_gsi_routing()) {
        /* set up irq routing */
        for (i = 0; i < s->num_irq - GIC_INTERNAL; ++i) {
            kvm_irqchip_add_irq_route(kvm_state, i, 0, i);
        }

        kvm_gsi_routing_allowed = true;

        kvm_irqchip_commit_routes(kvm_state);
    }

    if (!kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
                               GICD_CTLR)) {
        error_setg(&s->migration_blocker, "This operating system kernel does "
                                          "not support vGICv3 migration");
        if (migrate_add_blocker(s->migration_blocker, errp) < 0) {
            error_free(s->migration_blocker);
            return;
        }
    }
    if (kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                              KVM_DEV_ARM_VGIC_SAVE_PENDING_TABLES)) {
        qemu_add_vm_change_state_handler(vm_change_state_handler, s);
    }
}

static void kvm_arm_gicv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_CLASS(klass);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_CLASS(klass);

    agcc->pre_save = kvm_arm_gicv3_get;
    agcc->post_load = kvm_arm_gicv3_put;
    device_class_set_parent_realize(dc, kvm_arm_gicv3_realize,
                                    &kgc->parent_realize);
    device_class_set_parent_reset(dc, kvm_arm_gicv3_reset, &kgc->parent_reset);
}

static const TypeInfo kvm_arm_gicv3_info = {
    .name = TYPE_KVM_ARM_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = kvm_arm_gicv3_class_init,
    .class_size = sizeof(KVMARMGICv3Class),
};

static void kvm_arm_gicv3_register_types(void)
{
    type_register_static(&kvm_arm_gicv3_info);
}

type_init(kvm_arm_gicv3_register_types)
