/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 *
 * Copyright (C) 2016 Imagination Technologies
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "exec/memory.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"
#include "kvm_mips.h"
#include "hw/intc/mips_gic.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"

static void mips_gic_set_vp_irq(MIPSGICState *gic, int vp, int pin)
{
    int ored_level = 0;
    int i;

    /* ORing pending registers sharing same pin */
    for (i = 0; i < gic->num_irq; i++) {
        if ((gic->irq_state[i].map_pin & GIC_MAP_MSK) == pin &&
                gic->irq_state[i].map_vp == vp &&
                gic->irq_state[i].enabled) {
            ored_level |= gic->irq_state[i].pending;
        }
        if (ored_level) {
            /* no need to iterate all interrupts */
            break;
        }
    }
    if (((gic->vps[vp].compare_map & GIC_MAP_MSK) == pin) &&
            (gic->vps[vp].mask & GIC_VP_MASK_CMP_MSK)) {
        /* ORing with local pending register (count/compare) */
        ored_level |= (gic->vps[vp].pend & GIC_VP_MASK_CMP_MSK) >>
                      GIC_VP_MASK_CMP_SHF;
    }
    if (kvm_enabled())  {
        kvm_mips_set_ipi_interrupt(env_archcpu(gic->vps[vp].env),
                                   pin + GIC_CPU_PIN_OFFSET,
                                   ored_level);
    } else {
        qemu_set_irq(gic->vps[vp].env->irq[pin + GIC_CPU_PIN_OFFSET],
                     ored_level);
    }
}

static void gic_update_pin_for_irq(MIPSGICState *gic, int n_IRQ)
{
    int vp = gic->irq_state[n_IRQ].map_vp;
    int pin = gic->irq_state[n_IRQ].map_pin & GIC_MAP_MSK;

    if (vp < 0 || vp >= gic->num_vps) {
        return;
    }
    mips_gic_set_vp_irq(gic, vp, pin);
}

static void gic_set_irq(void *opaque, int n_IRQ, int level)
{
    MIPSGICState *gic = (MIPSGICState *) opaque;

    gic->irq_state[n_IRQ].pending = (uint8_t) level;
    if (!gic->irq_state[n_IRQ].enabled) {
        /* GIC interrupt source disabled */
        return;
    }
    gic_update_pin_for_irq(gic, n_IRQ);
}

#define OFFSET_CHECK(c)                         \
    do {                                        \
        if (!(c)) {                             \
            goto bad_offset;                    \
        }                                       \
    } while (0)

/* GIC Read VP Local/Other Registers */
static uint64_t gic_read_vp(MIPSGICState *gic, uint32_t vp_index, hwaddr addr,
                            unsigned size)
{
    switch (addr) {
    case GIC_VP_CTL_OFS:
        return gic->vps[vp_index].ctl;
    case GIC_VP_PEND_OFS:
        mips_gictimer_get_sh_count(gic->gic_timer);
        return gic->vps[vp_index].pend;
    case GIC_VP_MASK_OFS:
        return gic->vps[vp_index].mask;
    case GIC_VP_COMPARE_MAP_OFS:
        return gic->vps[vp_index].compare_map;
    case GIC_VP_OTHER_ADDR_OFS:
        return gic->vps[vp_index].other_addr;
    case GIC_VP_IDENT_OFS:
        return vp_index;
    case GIC_VP_COMPARE_LO_OFS:
        return mips_gictimer_get_vp_compare(gic->gic_timer, vp_index);
    case GIC_VP_COMPARE_HI_OFS:
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "Read %d bytes at GIC offset LOCAL/OTHER 0x%"
                      PRIx64 "\n", size, addr);
        break;
    }
    return 0;
}

static uint64_t gic_read(void *opaque, hwaddr addr, unsigned size)
{
    MIPSGICState *gic = (MIPSGICState *) opaque;
    uint32_t vp_index = current_cpu->cpu_index;
    uint64_t ret = 0;
    int i, base, irq_src;
    uint32_t other_index;

    switch (addr) {
    case GIC_SH_CONFIG_OFS:
        ret = gic->sh_config | (mips_gictimer_get_countstop(gic->gic_timer) <<
                               GIC_SH_CONFIG_COUNTSTOP_SHF);
        break;
    case GIC_SH_COUNTERLO_OFS:
        ret = mips_gictimer_get_sh_count(gic->gic_timer);
        break;
    case GIC_SH_COUNTERHI_OFS:
        ret = 0;
        break;
    case GIC_SH_PEND_OFS ... GIC_SH_PEND_LAST_OFS:
        /* each bit represents pending status for an interrupt pin */
        base = (addr - GIC_SH_PEND_OFS) * 8;
        OFFSET_CHECK((base + size * 8) <= gic->num_irq);
        for (i = 0; i < size * 8; i++) {
            ret |= (uint64_t) (gic->irq_state[base + i].pending) << i;
        }
        break;
    case GIC_SH_MASK_OFS ... GIC_SH_MASK_LAST_OFS:
        /* each bit represents status for an interrupt pin */
        base = (addr - GIC_SH_MASK_OFS) * 8;
        OFFSET_CHECK((base + size * 8) <= gic->num_irq);
        for (i = 0; i < size * 8; i++) {
            ret |= (uint64_t) (gic->irq_state[base + i].enabled) << i;
        }
        break;
    case GIC_SH_MAP0_PIN_OFS ... GIC_SH_MAP255_PIN_OFS:
        /* 32 bits per a pin */
        irq_src = (addr - GIC_SH_MAP0_PIN_OFS) / 4;
        OFFSET_CHECK(irq_src < gic->num_irq);
        ret = gic->irq_state[irq_src].map_pin;
        break;
    case GIC_SH_MAP0_VP_OFS ... GIC_SH_MAP255_VP_LAST_OFS:
        /* up to 32 bytes per a pin */
        irq_src = (addr - GIC_SH_MAP0_VP_OFS) / 32;
        OFFSET_CHECK(irq_src < gic->num_irq);
        if ((gic->irq_state[irq_src].map_vp) >= 0) {
            ret = (uint64_t) 1 << (gic->irq_state[irq_src].map_vp);
        } else {
            ret = 0;
        }
        break;
    /* VP-Local Register */
    case VP_LOCAL_SECTION_OFS ... (VP_LOCAL_SECTION_OFS + GIC_VL_BRK_GROUP):
        ret = gic_read_vp(gic, vp_index, addr - VP_LOCAL_SECTION_OFS, size);
        break;
    /* VP-Other Register */
    case VP_OTHER_SECTION_OFS ... (VP_OTHER_SECTION_OFS + GIC_VL_BRK_GROUP):
        other_index = gic->vps[vp_index].other_addr;
        ret = gic_read_vp(gic, other_index, addr - VP_OTHER_SECTION_OFS, size);
        break;
    /* User-Mode Visible section */
    case USM_VISIBLE_SECTION_OFS + GIC_USER_MODE_COUNTERLO:
        ret = mips_gictimer_get_sh_count(gic->gic_timer);
        break;
    case USM_VISIBLE_SECTION_OFS + GIC_USER_MODE_COUNTERHI:
        ret = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Read %d bytes at GIC offset 0x%" PRIx64 "\n",
                      size, addr);
        break;
    }
    return ret;
bad_offset:
    qemu_log_mask(LOG_GUEST_ERROR, "Wrong GIC offset at 0x%" PRIx64 "\n", addr);
    return 0;
}

static void gic_timer_expire_cb(void *opaque, uint32_t vp_index)
{
    MIPSGICState *gic = opaque;

    gic->vps[vp_index].pend |= (1 << GIC_LOCAL_INT_COMPARE);
    if (gic->vps[vp_index].pend &
            (gic->vps[vp_index].mask & GIC_VP_MASK_CMP_MSK)) {
        if (gic->vps[vp_index].compare_map & GIC_MAP_TO_PIN_MSK) {
            /* it is safe to set the irq high regardless of other GIC IRQs */
            uint32_t pin = (gic->vps[vp_index].compare_map & GIC_MAP_MSK);
            qemu_irq_raise(gic->vps[vp_index].env->irq
                           [pin + GIC_CPU_PIN_OFFSET]);
        }
    }
}

static void gic_timer_store_vp_compare(MIPSGICState *gic, uint32_t vp_index,
                                       uint64_t compare)
{
    gic->vps[vp_index].pend &= ~(1 << GIC_LOCAL_INT_COMPARE);
    if (gic->vps[vp_index].compare_map & GIC_MAP_TO_PIN_MSK) {
        uint32_t pin = (gic->vps[vp_index].compare_map & GIC_MAP_MSK);
        mips_gic_set_vp_irq(gic, vp_index, pin);
    }
    mips_gictimer_store_vp_compare(gic->gic_timer, vp_index, compare);
}

/* GIC Write VP Local/Other Registers */
static void gic_write_vp(MIPSGICState *gic, uint32_t vp_index, hwaddr addr,
                              uint64_t data, unsigned size)
{
    switch (addr) {
    case GIC_VP_CTL_OFS:
        /* EIC isn't supported */
        break;
    case GIC_VP_RMASK_OFS:
        gic->vps[vp_index].mask &= ~(data & GIC_VP_SET_RESET_MSK) &
                                   GIC_VP_SET_RESET_MSK;
        break;
    case GIC_VP_SMASK_OFS:
        gic->vps[vp_index].mask |= data & GIC_VP_SET_RESET_MSK;
        break;
    case GIC_VP_COMPARE_MAP_OFS:
        /* EIC isn't supported */
        OFFSET_CHECK((data & GIC_MAP_MSK) <= GIC_CPU_INT_MAX);
        gic->vps[vp_index].compare_map = data & GIC_MAP_TO_PIN_REG_MSK;
        break;
    case GIC_VP_OTHER_ADDR_OFS:
        OFFSET_CHECK(data < gic->num_vps);
        gic->vps[vp_index].other_addr = data;
        break;
    case GIC_VP_COMPARE_LO_OFS:
        gic_timer_store_vp_compare(gic, vp_index, data);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Write %d bytes at GIC offset LOCAL/OTHER "
                      "0x%" PRIx64" 0x%08" PRIx64 "\n", size, addr, data);
        break;
    }
    return;
bad_offset:
    qemu_log_mask(LOG_GUEST_ERROR, "Wrong GIC offset at 0x%" PRIx64 "\n", addr);
    return;
}

static void gic_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    int intr;
    MIPSGICState *gic = (MIPSGICState *) opaque;
    uint32_t vp_index = current_cpu->cpu_index;
    int i, base, irq_src;
    uint32_t other_index;

    switch (addr) {
    case GIC_SH_CONFIG_OFS:
        {
            uint32_t pre_cntstop = mips_gictimer_get_countstop(gic->gic_timer);
            uint32_t new_cntstop = (data & GIC_SH_CONFIG_COUNTSTOP_MSK) >>
                                   GIC_SH_CONFIG_COUNTSTOP_SHF;
            if (pre_cntstop != new_cntstop) {
                if (new_cntstop == 1) {
                    mips_gictimer_stop_count(gic->gic_timer);
                } else {
                    mips_gictimer_start_count(gic->gic_timer);
                }
            }
        }
        break;
    case GIC_SH_COUNTERLO_OFS:
        if (mips_gictimer_get_countstop(gic->gic_timer)) {
            mips_gictimer_store_sh_count(gic->gic_timer, data);
        }
        break;
    case GIC_SH_RMASK_OFS ... GIC_SH_RMASK_LAST_OFS:
        /* up to 64 bits per a pin */
        base = (addr - GIC_SH_RMASK_OFS) * 8;
        OFFSET_CHECK((base + size * 8) <= gic->num_irq);
        for (i = 0; i < size * 8; i++) {
            gic->irq_state[base + i].enabled &= !((data >> i) & 1);
            gic_update_pin_for_irq(gic, base + i);
        }
        break;
    case GIC_SH_WEDGE_OFS:
        /* Figure out which VP/HW Interrupt this maps to */
        intr = data & ~GIC_SH_WEDGE_RW_MSK;
        /* Mask/Enabled Checks */
        OFFSET_CHECK(intr < gic->num_irq);
        if (data & GIC_SH_WEDGE_RW_MSK) {
            gic_set_irq(gic, intr, 1);
        } else {
            gic_set_irq(gic, intr, 0);
        }
        break;
    case GIC_SH_SMASK_OFS ... GIC_SH_SMASK_LAST_OFS:
        /* up to 64 bits per a pin */
        base = (addr - GIC_SH_SMASK_OFS) * 8;
        OFFSET_CHECK((base + size * 8) <= gic->num_irq);
        for (i = 0; i < size * 8; i++) {
            gic->irq_state[base + i].enabled |= (data >> i) & 1;
            gic_update_pin_for_irq(gic, base + i);
        }
        break;
    case GIC_SH_MAP0_PIN_OFS ... GIC_SH_MAP255_PIN_OFS:
        /* 32 bits per a pin */
        irq_src = (addr - GIC_SH_MAP0_PIN_OFS) / 4;
        OFFSET_CHECK(irq_src < gic->num_irq);
        /* EIC isn't supported */
        OFFSET_CHECK((data & GIC_MAP_MSK) <= GIC_CPU_INT_MAX);
        gic->irq_state[irq_src].map_pin = data & GIC_MAP_TO_PIN_REG_MSK;
        break;
    case GIC_SH_MAP0_VP_OFS ... GIC_SH_MAP255_VP_LAST_OFS:
        /* up to 32 bytes per a pin */
        irq_src = (addr - GIC_SH_MAP0_VP_OFS) / 32;
        OFFSET_CHECK(irq_src < gic->num_irq);
        data = data ? ctz64(data) : -1;
        OFFSET_CHECK(data < gic->num_vps);
        gic->irq_state[irq_src].map_vp = data;
        break;
    case VP_LOCAL_SECTION_OFS ... (VP_LOCAL_SECTION_OFS + GIC_VL_BRK_GROUP):
        gic_write_vp(gic, vp_index, addr - VP_LOCAL_SECTION_OFS, data, size);
        break;
    case VP_OTHER_SECTION_OFS ... (VP_OTHER_SECTION_OFS + GIC_VL_BRK_GROUP):
        other_index = gic->vps[vp_index].other_addr;
        gic_write_vp(gic, other_index, addr - VP_OTHER_SECTION_OFS, data, size);
        break;
    case USM_VISIBLE_SECTION_OFS + GIC_USER_MODE_COUNTERLO:
    case USM_VISIBLE_SECTION_OFS + GIC_USER_MODE_COUNTERHI:
        /* do nothing. Read-only section */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Write %d bytes at GIC offset 0x%" PRIx64
                      " 0x%08" PRIx64 "\n", size, addr, data);
        break;
    }
    return;
bad_offset:
    qemu_log_mask(LOG_GUEST_ERROR, "Wrong GIC offset at 0x%" PRIx64 "\n", addr);
}

static void gic_reset(void *opaque)
{
    int i;
    MIPSGICState *gic = (MIPSGICState *) opaque;
    int numintrs = (gic->num_irq / 8) - 1;

    gic->sh_config = /* COUNTSTOP = 0 it is accessible via MIPSGICTimer*/
                     /* CounterHi not implemented */
                     (0            << GIC_SH_CONFIG_COUNTBITS_SHF) |
                     (numintrs     << GIC_SH_CONFIG_NUMINTRS_SHF)  |
                     (gic->num_vps << GIC_SH_CONFIG_PVPS_SHF);
    for (i = 0; i < gic->num_vps; i++) {
        gic->vps[i].ctl         = 0x0;
        gic->vps[i].pend        = 0x0;
        /* PERFCNT, TIMER and WD not implemented */
        gic->vps[i].mask        = 0x32;
        gic->vps[i].compare_map = GIC_MAP_TO_PIN_MSK;
        mips_gictimer_store_vp_compare(gic->gic_timer, i, 0xffffffff);
        gic->vps[i].other_addr  = 0x0;
    }
    for (i = 0; i < gic->num_irq; i++) {
        gic->irq_state[i].enabled = 0;
        gic->irq_state[i].pending = 0;
        gic->irq_state[i].map_pin = GIC_MAP_TO_PIN_MSK;
        gic->irq_state[i].map_vp  = -1;
    }
    mips_gictimer_store_sh_count(gic->gic_timer, 0);
    /* COUNTSTOP = 0 */
    mips_gictimer_start_count(gic->gic_timer);
}

static const MemoryRegionOps gic_ops = {
    .read = gic_read,
    .write = gic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 8,
    },
};

static void mips_gic_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MIPSGICState *s = MIPS_GIC(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &gic_ops, s,
                          "mips-gic", GIC_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->mr);
    qemu_register_reset(gic_reset, s);
}

static void mips_gic_realize(DeviceState *dev, Error **errp)
{
    MIPSGICState *s = MIPS_GIC(dev);
    CPUState *cs = first_cpu;
    int i;

    if (s->num_vps > GIC_MAX_VPS) {
        error_setg(errp, "Exceeded maximum CPUs %d", s->num_vps);
        return;
    }
    if ((s->num_irq > GIC_MAX_INTRS) || (s->num_irq % 8) || (s->num_irq <= 0)) {
        error_setg(errp, "GIC supports up to %d external interrupts in "
                   "multiples of 8 : %d", GIC_MAX_INTRS, s->num_irq);
        return;
    }
    s->vps = g_new(MIPSGICVPState, s->num_vps);
    s->irq_state = g_new(MIPSGICIRQState, s->num_irq);
    /* Register the env for all VPs with the GIC */
    for (i = 0; i < s->num_vps; i++) {
        if (cs != NULL) {
            s->vps[i].env = cs->env_ptr;
            cs = CPU_NEXT(cs);
        } else {
            error_setg(errp,
               "Unable to initialize GIC, CPUState for CPU#%d not valid.", i);
            return;
        }
    }
    s->gic_timer = mips_gictimer_init(s, s->num_vps, gic_timer_expire_cb);
    qdev_init_gpio_in(dev, gic_set_irq, s->num_irq);
    for (i = 0; i < s->num_irq; i++) {
        s->irq_state[i].irq = qdev_get_gpio_in(dev, i);
    }
}

static Property mips_gic_properties[] = {
    DEFINE_PROP_UINT32("num-vp", MIPSGICState, num_vps, 1),
    DEFINE_PROP_UINT32("num-irq", MIPSGICState, num_irq, 256),
    DEFINE_PROP_END_OF_LIST(),
};

static void mips_gic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, mips_gic_properties);
    dc->realize = mips_gic_realize;
}

static const TypeInfo mips_gic_info = {
    .name          = TYPE_MIPS_GIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSGICState),
    .instance_init = mips_gic_init,
    .class_init    = mips_gic_class_init,
};

static void mips_gic_register_types(void)
{
    type_register_static(&mips_gic_info);
}

type_init(mips_gic_register_types)
