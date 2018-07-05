/*
 * ARM GIC support
 *
 * Copyright (c) 2012 Linaro Limited
 * Copyright (c) 2015 Huawei.
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Peter Maydell
 * Reworked for GICv3 by Shlomo Pongratz and Pavel Fedin
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

#ifndef HW_ARM_GICV3_COMMON_H
#define HW_ARM_GICV3_COMMON_H

#include "hw/sysbus.h"
#include "hw/intc/arm_gic_common.h"

/*
 * Maximum number of possible interrupts, determined by the GIC architecture.
 * Note that this does not include LPIs. When implemented, these should be
 * dealt with separately.
 */
#define GICV3_MAXIRQ 1020
#define GICV3_MAXSPI (GICV3_MAXIRQ - GIC_INTERNAL)

#define GICV3_REDIST_SIZE 0x20000

/* Number of SGI target-list bits */
#define GICV3_TARGETLIST_BITS 16

/* Maximum number of list registers (architectural limit) */
#define GICV3_LR_MAX 16

/* Minimum BPR for Secure, or when security not enabled */
#define GIC_MIN_BPR 0
/* Minimum BPR for Nonsecure when security is enabled */
#define GIC_MIN_BPR_NS (GIC_MIN_BPR + 1)

/* For some distributor fields we want to model the array of 32-bit
 * register values which hold various bitmaps corresponding to enabled,
 * pending, etc bits. These macros and functions facilitate that; the
 * APIs are generally modelled on the generic bitmap.h functions
 * (which are unsuitable here because they use 'unsigned long' as the
 * underlying storage type, which is very awkward when you need to
 * access the data as 32-bit values.)
 * Each bitmap contains a bit for each interrupt. Although there is
 * space for the PPIs and SGIs, those bits (the first 32) are never
 * used as that state lives in the redistributor. The unused bits are
 * provided purely so that interrupt X's state is always in bit X; this
 * avoids bugs where we forget to subtract GIC_INTERNAL from an
 * interrupt number.
 */
#define GICV3_BMP_SIZE DIV_ROUND_UP(GICV3_MAXIRQ, 32)

#define GIC_DECLARE_BITMAP(name) \
    uint32_t name[GICV3_BMP_SIZE]

#define GIC_BIT_MASK(nr) (1U << ((nr) % 32))
#define GIC_BIT_WORD(nr) ((nr) / 32)

static inline void gic_bmp_set_bit(int nr, uint32_t *addr)
{
    uint32_t mask = GIC_BIT_MASK(nr);
    uint32_t *p = addr + GIC_BIT_WORD(nr);

    *p |= mask;
}

static inline void gic_bmp_clear_bit(int nr, uint32_t *addr)
{
    uint32_t mask = GIC_BIT_MASK(nr);
    uint32_t *p = addr + GIC_BIT_WORD(nr);

    *p &= ~mask;
}

static inline int gic_bmp_test_bit(int nr, const uint32_t *addr)
{
    return 1U & (addr[GIC_BIT_WORD(nr)] >> (nr & 31));
}

static inline void gic_bmp_replace_bit(int nr, uint32_t *addr, int val)
{
    uint32_t mask = GIC_BIT_MASK(nr);
    uint32_t *p = addr + GIC_BIT_WORD(nr);

    *p &= ~mask;
    *p |= (val & 1U) << (nr % 32);
}

/* Return a pointer to the 32-bit word containing the specified bit. */
static inline uint32_t *gic_bmp_ptr32(uint32_t *addr, int nr)
{
    return addr + GIC_BIT_WORD(nr);
}

typedef struct GICv3State GICv3State;
typedef struct GICv3CPUState GICv3CPUState;

/* Some CPU interface registers come in three flavours:
 * Group0, Group1 (Secure) and Group1 (NonSecure)
 * (where the latter two are exposed as a single banked system register).
 * In the state struct they are implemented as a 3-element array which
 * can be indexed into by the GICV3_G0, GICV3_G1 and GICV3_G1NS constants.
 * If the CPU doesn't support EL3 then the G1 element is unused.
 *
 * These constants are also used to communicate the group to use for
 * an interrupt or SGI when it is passed between the cpu interface and
 * the redistributor or distributor. For those purposes the receiving end
 * must be prepared to cope with a Group 1 Secure interrupt even if it does
 * not have security support enabled, because security can be disabled
 * independently in the CPU and in the GIC. In that case the receiver should
 * treat an incoming Group 1 Secure interrupt as if it were Group 0.
 * (This architectural requirement is why the _G1 element is the unused one
 * in a no-EL3 CPU:  we would otherwise have to translate back and forth
 * between (G0, G1NS) from the distributor and (G0, G1) in the CPU i/f.)
 */
#define GICV3_G0 0
#define GICV3_G1 1
#define GICV3_G1NS 2

/* ICC_CTLR_EL1, GICD_STATUSR and GICR_STATUSR are banked but not
 * group-related, so those indices are just 0 for S and 1 for NS.
 * (If the CPU or the GIC, respectively, don't support the Security
 * extensions then the S element is unused.)
 */
#define GICV3_S 0
#define GICV3_NS 1

typedef struct {
    int irq;
    uint8_t prio;
    int grp;
} PendingIrq;

struct GICv3CPUState {
    GICv3State *gic;
    CPUState *cpu;
    qemu_irq parent_irq;
    qemu_irq parent_fiq;
    qemu_irq parent_virq;
    qemu_irq parent_vfiq;
    qemu_irq maintenance_irq;

    /* Redistributor */
    uint32_t level;                  /* Current IRQ level */
    /* RD_base page registers */
    uint32_t gicr_ctlr;
    uint64_t gicr_typer;
    uint32_t gicr_statusr[2];
    uint32_t gicr_waker;
    uint64_t gicr_propbaser;
    uint64_t gicr_pendbaser;
    /* SGI_base page registers */
    uint32_t gicr_igroupr0;
    uint32_t gicr_ienabler0;
    uint32_t gicr_ipendr0;
    uint32_t gicr_iactiver0;
    uint32_t edge_trigger; /* ICFGR0 and ICFGR1 even bits */
    uint32_t gicr_igrpmodr0;
    uint32_t gicr_nsacr;
    uint8_t gicr_ipriorityr[GIC_INTERNAL];

    /* CPU interface */
    uint64_t icc_sre_el1;
    uint64_t icc_ctlr_el1[2];
    uint64_t icc_pmr_el1;
    uint64_t icc_bpr[3];
    uint64_t icc_apr[3][4];
    uint64_t icc_igrpen[3];
    uint64_t icc_ctlr_el3;

    /* Virtualization control interface */
    uint64_t ich_apr[3][4]; /* ich_apr[GICV3_G1][x] never used */
    uint64_t ich_hcr_el2;
    uint64_t ich_lr_el2[GICV3_LR_MAX];
    uint64_t ich_vmcr_el2;

    /* Properties of the CPU interface. These are initialized from
     * the settings in the CPU proper.
     * If the number of implemented list registers is 0 then the
     * virtualization support is not implemented.
     */
    int num_list_regs;
    int vpribits; /* number of virtual priority bits */
    int vprebits; /* number of virtual preemption bits */

    /* Current highest priority pending interrupt for this CPU.
     * This is cached information that can be recalculated from the
     * real state above; it doesn't need to be migrated.
     */
    PendingIrq hppi;
    /* This is temporary working state, to avoid a malloc in gicv3_update() */
    bool seenbetter;
};

struct GICv3State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem_dist; /* Distributor */
    MemoryRegion *iomem_redist; /* Redistributor Regions */
    uint32_t *redist_region_count; /* redistributor count within each region */
    uint32_t nb_redist_regions; /* number of redist regions */

    uint32_t num_cpu;
    uint32_t num_irq;
    uint32_t revision;
    bool security_extn;
    bool irq_reset_nonsecure;
    bool gicd_no_migration_shift_bug;

    int dev_fd; /* kvm device fd if backed by kvm vgic support */
    Error *migration_blocker;

    /* Distributor */

    /* for a GIC with the security extensions the NS banked version of this
     * register is just an alias of bit 1 of the S banked version.
     */
    uint32_t gicd_ctlr;
    uint32_t gicd_statusr[2];
    GIC_DECLARE_BITMAP(group);        /* GICD_IGROUPR */
    GIC_DECLARE_BITMAP(grpmod);       /* GICD_IGRPMODR */
    GIC_DECLARE_BITMAP(enabled);      /* GICD_ISENABLER */
    GIC_DECLARE_BITMAP(pending);      /* GICD_ISPENDR */
    GIC_DECLARE_BITMAP(active);       /* GICD_ISACTIVER */
    GIC_DECLARE_BITMAP(level);        /* Current level */
    GIC_DECLARE_BITMAP(edge_trigger); /* GICD_ICFGR even bits */
    uint8_t gicd_ipriority[GICV3_MAXIRQ];
    uint64_t gicd_irouter[GICV3_MAXIRQ];
    /* Cached information: pointer to the cpu i/f for the CPUs specified
     * in the IROUTER registers
     */
    GICv3CPUState *gicd_irouter_target[GICV3_MAXIRQ];
    uint32_t gicd_nsacr[DIV_ROUND_UP(GICV3_MAXIRQ, 16)];

    GICv3CPUState *cpu;
};

#define GICV3_BITMAP_ACCESSORS(BMP)                                     \
    static inline void gicv3_gicd_##BMP##_set(GICv3State *s, int irq)   \
    {                                                                   \
        gic_bmp_set_bit(irq, s->BMP);                                   \
    }                                                                   \
    static inline int gicv3_gicd_##BMP##_test(GICv3State *s, int irq)   \
    {                                                                   \
        return gic_bmp_test_bit(irq, s->BMP);                           \
    }                                                                   \
    static inline void gicv3_gicd_##BMP##_clear(GICv3State *s, int irq) \
    {                                                                   \
        gic_bmp_clear_bit(irq, s->BMP);                                 \
    }                                                                   \
    static inline void gicv3_gicd_##BMP##_replace(GICv3State *s,        \
                                                  int irq, int value)   \
    {                                                                   \
        gic_bmp_replace_bit(irq, s->BMP, value);                        \
    }

GICV3_BITMAP_ACCESSORS(group)
GICV3_BITMAP_ACCESSORS(grpmod)
GICV3_BITMAP_ACCESSORS(enabled)
GICV3_BITMAP_ACCESSORS(pending)
GICV3_BITMAP_ACCESSORS(active)
GICV3_BITMAP_ACCESSORS(level)
GICV3_BITMAP_ACCESSORS(edge_trigger)

#define TYPE_ARM_GICV3_COMMON "arm-gicv3-common"
#define ARM_GICV3_COMMON(obj) \
     OBJECT_CHECK(GICv3State, (obj), TYPE_ARM_GICV3_COMMON)
#define ARM_GICV3_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICv3CommonClass, (klass), TYPE_ARM_GICV3_COMMON)
#define ARM_GICV3_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICv3CommonClass, (obj), TYPE_ARM_GICV3_COMMON)

typedef struct ARMGICv3CommonClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    void (*pre_save)(GICv3State *s);
    void (*post_load)(GICv3State *s);
} ARMGICv3CommonClass;

void gicv3_init_irqs_and_mmio(GICv3State *s, qemu_irq_handler handler,
                              const MemoryRegionOps *ops, Error **errp);

#endif
