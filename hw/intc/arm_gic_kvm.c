/*
 * ARM Generic Interrupt Controller using KVM in-kernel support
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
 * Save/Restore logic added by Christoffer Dall.
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
#include "hw/sysbus.h"
#include "migration/migration.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "gic_internal.h"
#include "vgic_common.h"

//#define DEBUG_GIC_KVM

#ifdef DEBUG_GIC_KVM
static const int debug_gic_kvm = 1;
#else
static const int debug_gic_kvm = 0;
#endif

#define DPRINTF(fmt, ...) do { \
        if (debug_gic_kvm) { \
            printf("arm_gic: " fmt , ## __VA_ARGS__); \
        } \
    } while (0)

#define TYPE_KVM_ARM_GIC "kvm-arm-gic"
#define KVM_ARM_GIC(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_KVM_ARM_GIC)
#define KVM_ARM_GIC_CLASS(klass) \
     OBJECT_CLASS_CHECK(KVMARMGICClass, (klass), TYPE_KVM_ARM_GIC)
#define KVM_ARM_GIC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(KVMARMGICClass, (obj), TYPE_KVM_ARM_GIC)

typedef struct KVMARMGICClass {
    ARMGICCommonClass parent_class;
    DeviceRealize parent_realize;
    void (*parent_reset)(DeviceState *dev);
} KVMARMGICClass;

void kvm_arm_gic_set_irq(uint32_t num_irq, int irq, int level)
{
    /* Meaning of the 'irq' parameter:
     *  [0..N-1] : external interrupts
     *  [N..N+31] : PPI (internal) interrupts for CPU 0
     *  [N+32..N+63] : PPI (internal interrupts for CPU 1
     *  ...
     * Convert this to the kernel's desired encoding, which
     * has separate fields in the irq number for type,
     * CPU number and interrupt number.
     */
    int kvm_irq, irqtype, cpu;

    if (irq < (num_irq - GIC_INTERNAL)) {
        /* External interrupt. The kernel numbers these like the GIC
         * hardware, with external interrupt IDs starting after the
         * internal ones.
         */
        irqtype = KVM_ARM_IRQ_TYPE_SPI;
        cpu = 0;
        irq += GIC_INTERNAL;
    } else {
        /* Internal interrupt: decode into (cpu, interrupt id) */
        irqtype = KVM_ARM_IRQ_TYPE_PPI;
        irq -= (num_irq - GIC_INTERNAL);
        cpu = irq / GIC_INTERNAL;
        irq %= GIC_INTERNAL;
    }
    kvm_irq = (irqtype << KVM_ARM_IRQ_TYPE_SHIFT)
        | (cpu << KVM_ARM_IRQ_VCPU_SHIFT) | irq;

    kvm_set_irq(kvm_state, kvm_irq, !!level);
}

static void kvm_arm_gicv2_set_irq(void *opaque, int irq, int level)
{
    GICState *s = (GICState *)opaque;

    kvm_arm_gic_set_irq(s->num_irq, irq, level);
}

static bool kvm_arm_gic_can_save_restore(GICState *s)
{
    return s->dev_fd >= 0;
}

#define KVM_VGIC_ATTR(offset, cpu) \
    ((((uint64_t)(cpu) << KVM_DEV_ARM_VGIC_CPUID_SHIFT) & \
      KVM_DEV_ARM_VGIC_CPUID_MASK) | \
     (((uint64_t)(offset) << KVM_DEV_ARM_VGIC_OFFSET_SHIFT) & \
      KVM_DEV_ARM_VGIC_OFFSET_MASK))

static void kvm_gicd_access(GICState *s, int offset, int cpu,
                            uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_DIST_REGS,
                      KVM_VGIC_ATTR(offset, cpu), val, write);
}

static void kvm_gicc_access(GICState *s, int offset, int cpu,
                            uint32_t *val, bool write)
{
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CPU_REGS,
                      KVM_VGIC_ATTR(offset, cpu), val, write);
}

#define for_each_irq_reg(_ctr, _max_irq, _field_width) \
    for (_ctr = 0; _ctr < ((_max_irq) / (32 / (_field_width))); _ctr++)

/*
 * Translate from the in-kernel field for an IRQ value to/from the qemu
 * representation.
 */
typedef void (*vgic_translate_fn)(GICState *s, int irq, int cpu,
                                  uint32_t *field, bool to_kernel);

/* synthetic translate function used for clear/set registers to completely
 * clear a setting using a clear-register before setting the remaining bits
 * using a set-register */
static void translate_clear(GICState *s, int irq, int cpu,
                            uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = ~0;
    } else {
        /* does not make sense: qemu model doesn't use set/clear regs */
        abort();
    }
}

static void translate_group(GICState *s, int irq, int cpu,
                            uint32_t *field, bool to_kernel)
{
    int cm = (irq < GIC_INTERNAL) ? (1 << cpu) : ALL_CPU_MASK;

    if (to_kernel) {
        *field = GIC_TEST_GROUP(irq, cm);
    } else {
        if (*field & 1) {
            GIC_SET_GROUP(irq, cm);
        }
    }
}

static void translate_enabled(GICState *s, int irq, int cpu,
                              uint32_t *field, bool to_kernel)
{
    int cm = (irq < GIC_INTERNAL) ? (1 << cpu) : ALL_CPU_MASK;

    if (to_kernel) {
        *field = GIC_TEST_ENABLED(irq, cm);
    } else {
        if (*field & 1) {
            GIC_SET_ENABLED(irq, cm);
        }
    }
}

static void translate_pending(GICState *s, int irq, int cpu,
                              uint32_t *field, bool to_kernel)
{
    int cm = (irq < GIC_INTERNAL) ? (1 << cpu) : ALL_CPU_MASK;

    if (to_kernel) {
        *field = gic_test_pending(s, irq, cm);
    } else {
        if (*field & 1) {
            GIC_SET_PENDING(irq, cm);
            /* TODO: Capture is level-line is held high in the kernel */
        }
    }
}

static void translate_active(GICState *s, int irq, int cpu,
                             uint32_t *field, bool to_kernel)
{
    int cm = (irq < GIC_INTERNAL) ? (1 << cpu) : ALL_CPU_MASK;

    if (to_kernel) {
        *field = GIC_TEST_ACTIVE(irq, cm);
    } else {
        if (*field & 1) {
            GIC_SET_ACTIVE(irq, cm);
        }
    }
}

static void translate_trigger(GICState *s, int irq, int cpu,
                              uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = (GIC_TEST_EDGE_TRIGGER(irq)) ? 0x2 : 0x0;
    } else {
        if (*field & 0x2) {
            GIC_SET_EDGE_TRIGGER(irq);
        }
    }
}

static void translate_priority(GICState *s, int irq, int cpu,
                               uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = GIC_GET_PRIORITY(irq, cpu) & 0xff;
    } else {
        gic_set_priority(s, cpu, irq, *field & 0xff, MEMTXATTRS_UNSPECIFIED);
    }
}

static void translate_targets(GICState *s, int irq, int cpu,
                              uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = s->irq_target[irq] & 0xff;
    } else {
        s->irq_target[irq] = *field & 0xff;
    }
}

static void translate_sgisource(GICState *s, int irq, int cpu,
                                uint32_t *field, bool to_kernel)
{
    if (to_kernel) {
        *field = s->sgi_pending[irq][cpu] & 0xff;
    } else {
        s->sgi_pending[irq][cpu] = *field & 0xff;
    }
}

/* Read a register group from the kernel VGIC */
static void kvm_dist_get(GICState *s, uint32_t offset, int width,
                         int maxirq, vgic_translate_fn translate_fn)
{
    uint32_t reg;
    int i;
    int j;
    int irq;
    int cpu;
    int regsz = 32 / width; /* irqs per kernel register */
    uint32_t field;

    for_each_irq_reg(i, maxirq, width) {
        irq = i * regsz;
        cpu = 0;
        while ((cpu < s->num_cpu && irq < GIC_INTERNAL) || cpu == 0) {
            kvm_gicd_access(s, offset, cpu, &reg, false);
            for (j = 0; j < regsz; j++) {
                field = extract32(reg, j * width, width);
                translate_fn(s, irq + j, cpu, &field, false);
            }

            cpu++;
        }
        offset += 4;
    }
}

/* Write a register group to the kernel VGIC */
static void kvm_dist_put(GICState *s, uint32_t offset, int width,
                         int maxirq, vgic_translate_fn translate_fn)
{
    uint32_t reg;
    int i;
    int j;
    int irq;
    int cpu;
    int regsz = 32 / width; /* irqs per kernel register */
    uint32_t field;

    for_each_irq_reg(i, maxirq, width) {
        irq = i * regsz;
        cpu = 0;
        while ((cpu < s->num_cpu && irq < GIC_INTERNAL) || cpu == 0) {
            reg = 0;
            for (j = 0; j < regsz; j++) {
                translate_fn(s, irq + j, cpu, &field, true);
                reg = deposit32(reg, j * width, width, field);
            }
            kvm_gicd_access(s, offset, cpu, &reg, true);

            cpu++;
        }
        offset += 4;
    }
}

static void kvm_arm_gic_put(GICState *s)
{
    uint32_t reg;
    int i;
    int cpu;
    int num_cpu;
    int num_irq;

    /* Note: We do the restore in a slightly different order than the save
     * (where the order doesn't matter and is simply ordered according to the
     * register offset values */

    /*****************************************************************
     * Distributor State
     */

    /* s->ctlr -> GICD_CTLR */
    reg = s->ctlr;
    kvm_gicd_access(s, 0x0, 0, &reg, true);

    /* Sanity checking on GICD_TYPER and s->num_irq, s->num_cpu */
    kvm_gicd_access(s, 0x4, 0, &reg, false);
    num_irq = ((reg & 0x1f) + 1) * 32;
    num_cpu = ((reg & 0xe0) >> 5) + 1;

    if (num_irq < s->num_irq) {
            fprintf(stderr, "Restoring %u IRQs, but kernel supports max %d\n",
                    s->num_irq, num_irq);
            abort();
    } else if (num_cpu != s->num_cpu) {
            fprintf(stderr, "Restoring %u CPU interfaces, kernel only has %d\n",
                    s->num_cpu, num_cpu);
            /* Did we not create the VCPUs in the kernel yet? */
            abort();
    }

    /* TODO: Consider checking compatibility with the IIDR ? */

    /* irq_state[n].enabled -> GICD_ISENABLERn */
    kvm_dist_put(s, 0x180, 1, s->num_irq, translate_clear);
    kvm_dist_put(s, 0x100, 1, s->num_irq, translate_enabled);

    /* irq_state[n].group -> GICD_IGROUPRn */
    kvm_dist_put(s, 0x80, 1, s->num_irq, translate_group);

    /* s->irq_target[irq] -> GICD_ITARGETSRn
     * (restore targets before pending to ensure the pending state is set on
     * the appropriate CPU interfaces in the kernel) */
    kvm_dist_put(s, 0x800, 8, s->num_irq, translate_targets);

    /* irq_state[n].trigger -> GICD_ICFGRn
     * (restore configuration registers before pending IRQs so we treat
     * level/edge correctly) */
    kvm_dist_put(s, 0xc00, 2, s->num_irq, translate_trigger);

    /* irq_state[n].pending + irq_state[n].level -> GICD_ISPENDRn */
    kvm_dist_put(s, 0x280, 1, s->num_irq, translate_clear);
    kvm_dist_put(s, 0x200, 1, s->num_irq, translate_pending);

    /* irq_state[n].active -> GICD_ISACTIVERn */
    kvm_dist_put(s, 0x380, 1, s->num_irq, translate_clear);
    kvm_dist_put(s, 0x300, 1, s->num_irq, translate_active);


    /* s->priorityX[irq] -> ICD_IPRIORITYRn */
    kvm_dist_put(s, 0x400, 8, s->num_irq, translate_priority);

    /* s->sgi_pending -> ICD_CPENDSGIRn */
    kvm_dist_put(s, 0xf10, 8, GIC_NR_SGIS, translate_clear);
    kvm_dist_put(s, 0xf20, 8, GIC_NR_SGIS, translate_sgisource);


    /*****************************************************************
     * CPU Interface(s) State
     */

    for (cpu = 0; cpu < s->num_cpu; cpu++) {
        /* s->cpu_ctlr[cpu] -> GICC_CTLR */
        reg = s->cpu_ctlr[cpu];
        kvm_gicc_access(s, 0x00, cpu, &reg, true);

        /* s->priority_mask[cpu] -> GICC_PMR */
        reg = (s->priority_mask[cpu] & 0xff);
        kvm_gicc_access(s, 0x04, cpu, &reg, true);

        /* s->bpr[cpu] -> GICC_BPR */
        reg = (s->bpr[cpu] & 0x7);
        kvm_gicc_access(s, 0x08, cpu, &reg, true);

        /* s->abpr[cpu] -> GICC_ABPR */
        reg = (s->abpr[cpu] & 0x7);
        kvm_gicc_access(s, 0x1c, cpu, &reg, true);

        /* s->apr[n][cpu] -> GICC_APRn */
        for (i = 0; i < 4; i++) {
            reg = s->apr[i][cpu];
            kvm_gicc_access(s, 0xd0 + i * 4, cpu, &reg, true);
        }
    }
}

static void kvm_arm_gic_get(GICState *s)
{
    uint32_t reg;
    int i;
    int cpu;

    /*****************************************************************
     * Distributor State
     */

    /* GICD_CTLR -> s->ctlr */
    kvm_gicd_access(s, 0x0, 0, &reg, false);
    s->ctlr = reg;

    /* Sanity checking on GICD_TYPER -> s->num_irq, s->num_cpu */
    kvm_gicd_access(s, 0x4, 0, &reg, false);
    s->num_irq = ((reg & 0x1f) + 1) * 32;
    s->num_cpu = ((reg & 0xe0) >> 5) + 1;

    if (s->num_irq > GIC_MAXIRQ) {
            fprintf(stderr, "Too many IRQs reported from the kernel: %d\n",
                    s->num_irq);
            abort();
    }

    /* GICD_IIDR -> ? */
    kvm_gicd_access(s, 0x8, 0, &reg, false);

    /* Clear all the IRQ settings */
    for (i = 0; i < s->num_irq; i++) {
        memset(&s->irq_state[i], 0, sizeof(s->irq_state[0]));
    }

    /* GICD_IGROUPRn -> irq_state[n].group */
    kvm_dist_get(s, 0x80, 1, s->num_irq, translate_group);

    /* GICD_ISENABLERn -> irq_state[n].enabled */
    kvm_dist_get(s, 0x100, 1, s->num_irq, translate_enabled);

    /* GICD_ISPENDRn -> irq_state[n].pending + irq_state[n].level */
    kvm_dist_get(s, 0x200, 1, s->num_irq, translate_pending);

    /* GICD_ISACTIVERn -> irq_state[n].active */
    kvm_dist_get(s, 0x300, 1, s->num_irq, translate_active);

    /* GICD_ICFRn -> irq_state[n].trigger */
    kvm_dist_get(s, 0xc00, 2, s->num_irq, translate_trigger);

    /* GICD_IPRIORITYRn -> s->priorityX[irq] */
    kvm_dist_get(s, 0x400, 8, s->num_irq, translate_priority);

    /* GICD_ITARGETSRn -> s->irq_target[irq] */
    kvm_dist_get(s, 0x800, 8, s->num_irq, translate_targets);

    /* GICD_CPENDSGIRn -> s->sgi_pending */
    kvm_dist_get(s, 0xf10, 8, GIC_NR_SGIS, translate_sgisource);


    /*****************************************************************
     * CPU Interface(s) State
     */

    for (cpu = 0; cpu < s->num_cpu; cpu++) {
        /* GICC_CTLR -> s->cpu_ctlr[cpu] */
        kvm_gicc_access(s, 0x00, cpu, &reg, false);
        s->cpu_ctlr[cpu] = reg;

        /* GICC_PMR -> s->priority_mask[cpu] */
        kvm_gicc_access(s, 0x04, cpu, &reg, false);
        s->priority_mask[cpu] = (reg & 0xff);

        /* GICC_BPR -> s->bpr[cpu] */
        kvm_gicc_access(s, 0x08, cpu, &reg, false);
        s->bpr[cpu] = (reg & 0x7);

        /* GICC_ABPR -> s->abpr[cpu] */
        kvm_gicc_access(s, 0x1c, cpu, &reg, false);
        s->abpr[cpu] = (reg & 0x7);

        /* GICC_APRn -> s->apr[n][cpu] */
        for (i = 0; i < 4; i++) {
            kvm_gicc_access(s, 0xd0 + i * 4, cpu, &reg, false);
            s->apr[i][cpu] = reg;
        }
    }
}

static void kvm_arm_gic_reset(DeviceState *dev)
{
    GICState *s = ARM_GIC_COMMON(dev);
    KVMARMGICClass *kgc = KVM_ARM_GIC_GET_CLASS(s);

    kgc->parent_reset(dev);

    if (kvm_arm_gic_can_save_restore(s)) {
        kvm_arm_gic_put(s);
    }
}

static void kvm_arm_gic_realize(DeviceState *dev, Error **errp)
{
    int i;
    GICState *s = KVM_ARM_GIC(dev);
    KVMARMGICClass *kgc = KVM_ARM_GIC_GET_CLASS(s);
    Error *local_err = NULL;
    int ret;

    kgc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->security_extn) {
        error_setg(errp, "the in-kernel VGIC does not implement the "
                   "security extensions");
        return;
    }

    gic_init_irqs_and_mmio(s, kvm_arm_gicv2_set_irq, NULL);

    for (i = 0; i < s->num_irq - GIC_INTERNAL; i++) {
        qemu_irq irq = qdev_get_gpio_in(dev, i);
        kvm_irqchip_set_qemuirq_gsi(kvm_state, irq, i);
    }

    /* Try to create the device via the device control API */
    s->dev_fd = -1;
    ret = kvm_create_device(kvm_state, KVM_DEV_TYPE_ARM_VGIC_V2, false);
    if (ret >= 0) {
        s->dev_fd = ret;

        /* Newstyle API is used, we may have attributes */
        if (kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS, 0)) {
            uint32_t numirqs = s->num_irq;
            kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS, 0,
                              &numirqs, true);
        }
        /* Tell the kernel to complete VGIC initialization now */
        if (kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                                  KVM_DEV_ARM_VGIC_CTRL_INIT)) {
            kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                              KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true);
        }
    } else if (ret != -ENODEV && ret != -ENOTSUP) {
        error_setg_errno(errp, -ret, "error creating in-kernel VGIC");
        return;
    }

    /* Distributor */
    kvm_arm_register_device(&s->iomem,
                            (KVM_ARM_DEVICE_VGIC_V2 << KVM_ARM_DEVICE_ID_SHIFT)
                            | KVM_VGIC_V2_ADDR_TYPE_DIST,
                            KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_V2_ADDR_TYPE_DIST,
                            s->dev_fd);
    /* CPU interface for current core. Unlike arm_gic, we don't
     * provide the "interface for core #N" memory regions, because
     * cores with a VGIC don't have those.
     */
    kvm_arm_register_device(&s->cpuiomem[0],
                            (KVM_ARM_DEVICE_VGIC_V2 << KVM_ARM_DEVICE_ID_SHIFT)
                            | KVM_VGIC_V2_ADDR_TYPE_CPU,
                            KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_V2_ADDR_TYPE_CPU,
                            s->dev_fd);

    if (!kvm_arm_gic_can_save_restore(s)) {
        error_setg(&s->migration_blocker, "This operating system kernel does "
                                          "not support vGICv2 migration");
        migrate_add_blocker(s->migration_blocker);
    }
}

static void kvm_arm_gic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICCommonClass *agcc = ARM_GIC_COMMON_CLASS(klass);
    KVMARMGICClass *kgc = KVM_ARM_GIC_CLASS(klass);

    agcc->pre_save = kvm_arm_gic_get;
    agcc->post_load = kvm_arm_gic_put;
    kgc->parent_realize = dc->realize;
    kgc->parent_reset = dc->reset;
    dc->realize = kvm_arm_gic_realize;
    dc->reset = kvm_arm_gic_reset;
}

static const TypeInfo kvm_arm_gic_info = {
    .name = TYPE_KVM_ARM_GIC,
    .parent = TYPE_ARM_GIC_COMMON,
    .instance_size = sizeof(GICState),
    .class_init = kvm_arm_gic_class_init,
    .class_size = sizeof(KVMARMGICClass),
};

static void kvm_arm_gic_register_types(void)
{
    type_register_static(&kvm_arm_gic_info);
}

type_init(kvm_arm_gic_register_types)
