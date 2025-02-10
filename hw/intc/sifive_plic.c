/*
 * SiFive PLIC (Platform Level Interrupt Controller)
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * This provides a parameterizable interrupt controller based on SiFive's PLIC.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "hw/intc/sifive_plic.h"
#include "target/riscv/cpu.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "system/kvm.h"

static bool addr_between(uint32_t addr, uint32_t base, uint32_t num)
{
    return addr >= base && addr - base < num;
}

static PLICMode char_to_mode(char c)
{
    switch (c) {
    case 'U': return PLICMode_U;
    case 'S': return PLICMode_S;
    case 'M': return PLICMode_M;
    default:
        error_report("plic: invalid mode '%c'", c);
        exit(1);
    }
}

static uint32_t atomic_set_masked(uint32_t *a, uint32_t mask, uint32_t value)
{
    uint32_t old, new, cmp = qatomic_read(a);

    do {
        old = cmp;
        new = (old & ~mask) | (value & mask);
        cmp = qatomic_cmpxchg(a, old, new);
    } while (old != cmp);

    return old;
}

static void sifive_plic_set_pending(SiFivePLICState *plic, int irq, bool level)
{
    atomic_set_masked(&plic->pending[irq >> 5], 1 << (irq & 31), -!!level);
}

static void sifive_plic_set_claimed(SiFivePLICState *plic, int irq, bool level)
{
    atomic_set_masked(&plic->claimed[irq >> 5], 1 << (irq & 31), -!!level);
}

static uint32_t sifive_plic_claimed(SiFivePLICState *plic, uint32_t addrid)
{
    uint32_t max_irq = 0;
    uint32_t max_prio = plic->target_priority[addrid];
    int i, j;
    int num_irq_in_word = 32;

    for (i = 0; i < plic->bitfield_words; i++) {
        uint32_t pending_enabled_not_claimed =
                        (plic->pending[i] & ~plic->claimed[i]) &
                            plic->enable[addrid * plic->bitfield_words + i];

        if (!pending_enabled_not_claimed) {
            continue;
        }

        if (i == (plic->bitfield_words - 1)) {
            /*
             * If plic->num_sources is not multiple of 32, num-of-irq in last
             * word is not 32. Compute the num-of-irq of last word to avoid
             * out-of-bound access of source_priority array.
             */
            num_irq_in_word = plic->num_sources - ((plic->bitfield_words - 1) << 5);
        }

        for (j = 0; j < num_irq_in_word; j++) {
            int irq = (i << 5) + j;
            uint32_t prio = plic->source_priority[irq];
            int enabled = pending_enabled_not_claimed & (1 << j);

            if (enabled && prio > max_prio) {
                max_irq = irq;
                max_prio = prio;
            }
        }
    }

    return max_irq;
}

static void sifive_plic_update(SiFivePLICState *plic)
{
    int addrid;

    /* raise irq on harts where this irq is enabled */
    for (addrid = 0; addrid < plic->num_addrs; addrid++) {
        uint32_t hartid = plic->addr_config[addrid].hartid;
        PLICMode mode = plic->addr_config[addrid].mode;
        bool level = !!sifive_plic_claimed(plic, addrid);

        switch (mode) {
        case PLICMode_M:
            qemu_set_irq(plic->m_external_irqs[hartid - plic->hartid_base], level);
            break;
        case PLICMode_S:
            qemu_set_irq(plic->s_external_irqs[hartid - plic->hartid_base], level);
            break;
        default:
            break;
        }
    }
}

static uint64_t sifive_plic_read(void *opaque, hwaddr addr, unsigned size)
{
    SiFivePLICState *plic = opaque;

    if (addr_between(addr, plic->priority_base, plic->num_sources << 2)) {
        uint32_t irq = (addr - plic->priority_base) >> 2;

        return plic->source_priority[irq];
    } else if (addr_between(addr, plic->pending_base,
                            (plic->num_sources + 31) >> 3)) {
        uint32_t word = (addr - plic->pending_base) >> 2;

        return plic->pending[word];
    } else if (addr_between(addr, plic->enable_base,
                            plic->num_addrs * plic->enable_stride)) {
        uint32_t addrid = (addr - plic->enable_base) / plic->enable_stride;
        uint32_t wordid = (addr & (plic->enable_stride - 1)) >> 2;

        if (wordid < plic->bitfield_words) {
            return plic->enable[addrid * plic->bitfield_words + wordid];
        }
    } else if (addr_between(addr, plic->context_base,
                            plic->num_addrs * plic->context_stride)) {
        uint32_t addrid = (addr - plic->context_base) / plic->context_stride;
        uint32_t contextid = (addr & (plic->context_stride - 1));

        if (contextid == 0) {
            return plic->target_priority[addrid];
        } else if (contextid == 4) {
            uint32_t max_irq = sifive_plic_claimed(plic, addrid);

            if (max_irq) {
                sifive_plic_set_pending(plic, max_irq, false);
                sifive_plic_set_claimed(plic, max_irq, true);
            }

            sifive_plic_update(plic);
            return max_irq;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register read 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void sifive_plic_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    SiFivePLICState *plic = opaque;

    if (addr_between(addr, plic->priority_base, plic->num_sources << 2)) {
        uint32_t irq = (addr - plic->priority_base) >> 2;
        if (irq == 0) {
            /* IRQ 0 source prioority is reserved */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Invalid source priority write 0x%"
                          HWADDR_PRIx "\n", __func__, addr);
            return;
        } else if (((plic->num_priorities + 1) & plic->num_priorities) == 0) {
            /*
             * if "num_priorities + 1" is power-of-2, make each register bit of
             * interrupt priority WARL (Write-Any-Read-Legal). Just filter
             * out the access to unsupported priority bits.
             */
            plic->source_priority[irq] = value % (plic->num_priorities + 1);
            sifive_plic_update(plic);
        } else if (value <= plic->num_priorities) {
            plic->source_priority[irq] = value;
            sifive_plic_update(plic);
        }
    } else if (addr_between(addr, plic->pending_base,
                            (plic->num_sources + 31) >> 3)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid pending write: 0x%" HWADDR_PRIx "",
                      __func__, addr);
    } else if (addr_between(addr, plic->enable_base,
                            plic->num_addrs * plic->enable_stride)) {
        uint32_t addrid = (addr - plic->enable_base) / plic->enable_stride;
        uint32_t wordid = (addr & (plic->enable_stride - 1)) >> 2;

        if (wordid < plic->bitfield_words) {
            plic->enable[addrid * plic->bitfield_words + wordid] = value;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Invalid enable write 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
        }
    } else if (addr_between(addr, plic->context_base,
                            plic->num_addrs * plic->context_stride)) {
        uint32_t addrid = (addr - plic->context_base) / plic->context_stride;
        uint32_t contextid = (addr & (plic->context_stride - 1));

        if (contextid == 0) {
            if (((plic->num_priorities + 1) & plic->num_priorities) == 0) {
                /*
                 * if "num_priorities + 1" is power-of-2, each register bit of
                 * interrupt priority is WARL (Write-Any-Read-Legal). Just
                 * filter out the access to unsupported priority bits.
                 */
                plic->target_priority[addrid] = value %
                                                (plic->num_priorities + 1);
                sifive_plic_update(plic);
            } else if (value <= plic->num_priorities) {
                plic->target_priority[addrid] = value;
                sifive_plic_update(plic);
            }
        } else if (contextid == 4) {
            if (value < plic->num_sources) {
                sifive_plic_set_claimed(plic, value, false);
                sifive_plic_update(plic);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Invalid context write 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid register write 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
}

static const MemoryRegionOps sifive_plic_ops = {
    .read = sifive_plic_read,
    .write = sifive_plic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void sifive_plic_reset(DeviceState *dev)
{
    SiFivePLICState *s = SIFIVE_PLIC(dev);
    int i;

    memset(s->source_priority, 0, sizeof(uint32_t) * s->num_sources);
    memset(s->target_priority, 0, sizeof(uint32_t) * s->num_addrs);
    memset(s->pending, 0, sizeof(uint32_t) * s->bitfield_words);
    memset(s->claimed, 0, sizeof(uint32_t) * s->bitfield_words);
    memset(s->enable, 0, sizeof(uint32_t) * s->num_enables);

    for (i = 0; i < s->num_harts; i++) {
        qemu_set_irq(s->m_external_irqs[i], 0);
        qemu_set_irq(s->s_external_irqs[i], 0);
    }
}

/*
 * parse PLIC hart/mode address offset config
 *
 * "M"              1 hart with M mode
 * "MS,MS"          2 harts, 0-1 with M and S mode
 * "M,MS,MS,MS,MS"  5 harts, 0 with M mode, 1-5 with M and S mode
 */
static void parse_hart_config(SiFivePLICState *plic)
{
    int addrid, hartid, modes, m;
    const char *p;
    char c;

    /* count and validate hart/mode combinations */
    addrid = 0, hartid = 0, modes = 0;
    p = plic->hart_config;
    while ((c = *p++)) {
        if (c == ',') {
            if (modes) {
                addrid += ctpop8(modes);
                hartid++;
                modes = 0;
            }
        } else {
            m = 1 << char_to_mode(c);
            if (modes == (modes | m)) {
                error_report("plic: duplicate mode '%c' in config: %s",
                             c, plic->hart_config);
                exit(1);
            }
            modes |= m;
        }
    }
    if (modes) {
        addrid += ctpop8(modes);
        hartid++;
        modes = 0;
    }

    plic->num_addrs = addrid;
    plic->num_harts = hartid;

    /* store hart/mode combinations */
    plic->addr_config = g_new(PLICAddr, plic->num_addrs);
    addrid = 0, hartid = plic->hartid_base;
    p = plic->hart_config;
    while ((c = *p++)) {
        if (c == ',') {
            if (modes) {
                hartid++;
                modes = 0;
            }
        } else {
            m = char_to_mode(c);
            plic->addr_config[addrid].addrid = addrid;
            plic->addr_config[addrid].hartid = hartid;
            plic->addr_config[addrid].mode = m;
            modes |= (1 << m);
            addrid++;
        }
    }
}

static void sifive_plic_irq_request(void *opaque, int irq, int level)
{
    SiFivePLICState *s = opaque;

    if (level > 0) {
        sifive_plic_set_pending(s, irq, true);
        sifive_plic_update(s);
    }
}

static void sifive_plic_realize(DeviceState *dev, Error **errp)
{
    SiFivePLICState *s = SIFIVE_PLIC(dev);
    int i;

    memory_region_init_io(&s->mmio, OBJECT(dev), &sifive_plic_ops, s,
                          TYPE_SIFIVE_PLIC, s->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    parse_hart_config(s);

    if (!s->num_sources) {
        error_setg(errp, "plic: invalid number of interrupt sources");
        return;
    }

    s->bitfield_words = (s->num_sources + 31) >> 5;
    s->num_enables = s->bitfield_words * s->num_addrs;
    s->source_priority = g_new0(uint32_t, s->num_sources);
    s->target_priority = g_new(uint32_t, s->num_addrs);
    s->pending = g_new0(uint32_t, s->bitfield_words);
    s->claimed = g_new0(uint32_t, s->bitfield_words);
    s->enable = g_new0(uint32_t, s->num_enables);

    qdev_init_gpio_in(dev, sifive_plic_irq_request, s->num_sources);

    s->s_external_irqs = g_malloc(sizeof(qemu_irq) * s->num_harts);
    qdev_init_gpio_out(dev, s->s_external_irqs, s->num_harts);

    s->m_external_irqs = g_malloc(sizeof(qemu_irq) * s->num_harts);
    qdev_init_gpio_out(dev, s->m_external_irqs, s->num_harts);

    /*
     * We can't allow the supervisor to control SEIP as this would allow the
     * supervisor to clear a pending external interrupt which will result in
     * lost a interrupt in the case a PLIC is attached. The SEIP bit must be
     * hardware controlled when a PLIC is attached.
     */
    for (i = 0; i < s->num_harts; i++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(s->hartid_base + i));
        if (riscv_cpu_claim_interrupts(cpu, MIP_SEIP) < 0) {
            error_setg(errp, "SEIP already claimed");
            return;
        }
    }

    msi_nonbroken = true;
}

static const VMStateDescription vmstate_sifive_plic = {
    .name = "riscv_sifive_plic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
            VMSTATE_VARRAY_UINT32(source_priority, SiFivePLICState,
                                  num_sources, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(target_priority, SiFivePLICState,
                                  num_addrs, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(pending, SiFivePLICState, bitfield_words, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(claimed, SiFivePLICState, bitfield_words, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(enable, SiFivePLICState, num_enables, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_END_OF_LIST()
        }
};

static const Property sifive_plic_properties[] = {
    DEFINE_PROP_STRING("hart-config", SiFivePLICState, hart_config),
    DEFINE_PROP_UINT32("hartid-base", SiFivePLICState, hartid_base, 0),
    /* number of interrupt sources including interrupt source 0 */
    DEFINE_PROP_UINT32("num-sources", SiFivePLICState, num_sources, 1),
    DEFINE_PROP_UINT32("num-priorities", SiFivePLICState, num_priorities, 0),
    /* interrupt priority register base starting from source 0 */
    DEFINE_PROP_UINT32("priority-base", SiFivePLICState, priority_base, 0),
    DEFINE_PROP_UINT32("pending-base", SiFivePLICState, pending_base, 0),
    DEFINE_PROP_UINT32("enable-base", SiFivePLICState, enable_base, 0),
    DEFINE_PROP_UINT32("enable-stride", SiFivePLICState, enable_stride, 0),
    DEFINE_PROP_UINT32("context-base", SiFivePLICState, context_base, 0),
    DEFINE_PROP_UINT32("context-stride", SiFivePLICState, context_stride, 0),
    DEFINE_PROP_UINT32("aperture-size", SiFivePLICState, aperture_size, 0),
};

static void sifive_plic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, sifive_plic_reset);
    device_class_set_props(dc, sifive_plic_properties);
    dc->realize = sifive_plic_realize;
    dc->vmsd = &vmstate_sifive_plic;
}

static const TypeInfo sifive_plic_info = {
    .name          = TYPE_SIFIVE_PLIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFivePLICState),
    .class_init    = sifive_plic_class_init,
};

static void sifive_plic_register_types(void)
{
    type_register_static(&sifive_plic_info);
}

type_init(sifive_plic_register_types)

/*
 * Create PLIC device.
 */
DeviceState *sifive_plic_create(hwaddr addr, char *hart_config,
    uint32_t num_harts,
    uint32_t hartid_base, uint32_t num_sources,
    uint32_t num_priorities, uint32_t priority_base,
    uint32_t pending_base, uint32_t enable_base,
    uint32_t enable_stride, uint32_t context_base,
    uint32_t context_stride, uint32_t aperture_size)
{
    DeviceState *dev = qdev_new(TYPE_SIFIVE_PLIC);
    int i;
    SiFivePLICState *plic;

    assert(enable_stride == (enable_stride & -enable_stride));
    assert(context_stride == (context_stride & -context_stride));
    qdev_prop_set_string(dev, "hart-config", hart_config);
    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-sources", num_sources);
    qdev_prop_set_uint32(dev, "num-priorities", num_priorities);
    qdev_prop_set_uint32(dev, "priority-base", priority_base);
    qdev_prop_set_uint32(dev, "pending-base", pending_base);
    qdev_prop_set_uint32(dev, "enable-base", enable_base);
    qdev_prop_set_uint32(dev, "enable-stride", enable_stride);
    qdev_prop_set_uint32(dev, "context-base", context_base);
    qdev_prop_set_uint32(dev, "context-stride", context_stride);
    qdev_prop_set_uint32(dev, "aperture-size", aperture_size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    plic = SIFIVE_PLIC(dev);

    for (i = 0; i < plic->num_addrs; i++) {
        int cpu_num = plic->addr_config[i].hartid;
        CPUState *cpu = qemu_get_cpu(cpu_num);

        if (plic->addr_config[i].mode == PLICMode_M) {
            qdev_connect_gpio_out(dev, cpu_num - hartid_base + num_harts,
                                  qdev_get_gpio_in(DEVICE(cpu), IRQ_M_EXT));
        }
        if (plic->addr_config[i].mode == PLICMode_S) {
            qdev_connect_gpio_out(dev, cpu_num - hartid_base,
                                  qdev_get_gpio_in(DEVICE(cpu), IRQ_S_EXT));
        }
    }

    return dev;
}
