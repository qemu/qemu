/*
 * ARM GIC support
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
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

#ifndef HW_ARM_GIC_COMMON_H
#define HW_ARM_GIC_COMMON_H

#include "hw/sysbus.h"

/* Maximum number of possible interrupts, determined by the GIC architecture */
#define GIC_MAXIRQ 1020
/* First 32 are private to each CPU (SGIs and PPIs). */
#define GIC_INTERNAL 32
#define GIC_NR_SGIS 16
/* Maximum number of possible CPU interfaces, determined by GIC architecture */
#define GIC_NCPU 8
/* Maximum number of possible CPU interfaces with their respective vCPU */
#define GIC_NCPU_VCPU (GIC_NCPU * 2)

#define MAX_NR_GROUP_PRIO 128
#define GIC_NR_APRS (MAX_NR_GROUP_PRIO / 32)

#define GIC_MIN_BPR 0
#define GIC_MIN_ABPR (GIC_MIN_BPR + 1)

/* Architectural maximum number of list registers in the virtual interface */
#define GIC_MAX_LR 64

/* Only 32 priority levels and 32 preemption levels in the vCPU interfaces */
#define GIC_VIRT_MAX_GROUP_PRIO_BITS 5
#define GIC_VIRT_MAX_NR_GROUP_PRIO (1 << GIC_VIRT_MAX_GROUP_PRIO_BITS)
#define GIC_VIRT_NR_APRS (GIC_VIRT_MAX_NR_GROUP_PRIO / 32)

#define GIC_VIRT_MIN_BPR 2
#define GIC_VIRT_MIN_ABPR (GIC_VIRT_MIN_BPR + 1)

typedef struct gic_irq_state {
    /* The enable bits are only banked for per-cpu interrupts.  */
    uint8_t enabled;
    uint8_t pending;
    uint8_t active;
    uint8_t level;
    bool model; /* 0 = N:N, 1 = 1:N */
    bool edge_trigger; /* true: edge-triggered, false: level-triggered  */
    uint8_t group;
} gic_irq_state;

typedef struct GICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    qemu_irq parent_irq[GIC_NCPU];
    qemu_irq parent_fiq[GIC_NCPU];
    qemu_irq parent_virq[GIC_NCPU];
    qemu_irq parent_vfiq[GIC_NCPU];
    qemu_irq maintenance_irq[GIC_NCPU];

    /* GICD_CTLR; for a GIC with the security extensions the NS banked version
     * of this register is just an alias of bit 1 of the S banked version.
     */
    uint32_t ctlr;
    /* GICC_CTLR; again, the NS banked version is just aliases of bits of
     * the S banked register, so our state only needs to store the S version.
     */
    uint32_t cpu_ctlr[GIC_NCPU_VCPU];

    gic_irq_state irq_state[GIC_MAXIRQ];
    uint8_t irq_target[GIC_MAXIRQ];
    uint8_t priority1[GIC_INTERNAL][GIC_NCPU];
    uint8_t priority2[GIC_MAXIRQ - GIC_INTERNAL];
    /* For each SGI on the target CPU, we store 8 bits
     * indicating which source CPUs have made this SGI
     * pending on the target CPU. These correspond to
     * the bytes in the GIC_SPENDSGIR* registers as
     * read by the target CPU.
     */
    uint8_t sgi_pending[GIC_NR_SGIS][GIC_NCPU];

    uint16_t priority_mask[GIC_NCPU_VCPU];
    uint16_t running_priority[GIC_NCPU_VCPU];
    uint16_t current_pending[GIC_NCPU_VCPU];
    uint32_t n_prio_bits;

    /* If we present the GICv2 without security extensions to a guest,
     * the guest can configure the GICC_CTLR to configure group 1 binary point
     * in the abpr.
     * For a GIC with Security Extensions we use use bpr for the
     * secure copy and abpr as storage for the non-secure copy of the register.
     */
    uint8_t  bpr[GIC_NCPU_VCPU];
    uint8_t  abpr[GIC_NCPU_VCPU];

    /* The APR is implementation defined, so we choose a layout identical to
     * the KVM ABI layout for QEMU's implementation of the gic:
     * If an interrupt for preemption level X is active, then
     *   APRn[X mod 32] == 0b1,  where n = X / 32
     * otherwise the bit is clear.
     */
    uint32_t apr[GIC_NR_APRS][GIC_NCPU];
    uint32_t nsapr[GIC_NR_APRS][GIC_NCPU];

    /* Virtual interface control registers */
    uint32_t h_hcr[GIC_NCPU];
    uint32_t h_misr[GIC_NCPU];
    uint32_t h_lr[GIC_MAX_LR][GIC_NCPU];
    uint32_t h_apr[GIC_NCPU];

    /* Number of LRs implemented in this GIC instance */
    uint32_t num_lrs;

    uint32_t num_cpu;

    MemoryRegion iomem; /* Distributor */
    /* This is just so we can have an opaque pointer which identifies
     * both this GIC and which CPU interface we should be accessing.
     */
    struct GICState *backref[GIC_NCPU];
    MemoryRegion cpuiomem[GIC_NCPU + 1]; /* CPU interfaces */
    MemoryRegion vifaceiomem[GIC_NCPU + 1]; /* Virtual interfaces */
    MemoryRegion vcpuiomem; /* vCPU interface */

    uint32_t num_irq;
    uint32_t revision;
    bool security_extn;
    bool virt_extn;
    bool irq_reset_nonsecure; /* configure IRQs as group 1 (NS) on reset? */
    int dev_fd; /* kvm device fd if backed by kvm vgic support */
    Error *migration_blocker;
} GICState;

#define TYPE_ARM_GIC_COMMON "arm_gic_common"
#define ARM_GIC_COMMON(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_ARM_GIC_COMMON)
#define ARM_GIC_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICCommonClass, (klass), TYPE_ARM_GIC_COMMON)
#define ARM_GIC_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICCommonClass, (obj), TYPE_ARM_GIC_COMMON)

typedef struct ARMGICCommonClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    void (*pre_save)(GICState *s);
    void (*post_load)(GICState *s);
} ARMGICCommonClass;

void gic_init_irqs_and_mmio(GICState *s, qemu_irq_handler handler,
                            const MemoryRegionOps *ops,
                            const MemoryRegionOps *virt_ops);

#endif
