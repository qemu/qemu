/*
 * SuperH interrupt controller module
 *
 * Copyright (c) 2007 Magnus Damm
 * Based on sh_timer.c and arm_timer.c by Paul Brook
 * Copyright (c) 2005-2006 CodeSourcery.
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "hw/sh4/sh_intc.h"
#include "hw/irq.h"
#include "hw/sh4/sh.h"
#include "trace.h"

void sh_intc_toggle_source(struct intc_source *source,
                           int enable_adj, int assert_adj)
{
    int enable_changed = 0;
    int pending_changed = 0;
    int old_pending;

    if (source->enable_count == source->enable_max && enable_adj == -1) {
        enable_changed = -1;
    }
    source->enable_count += enable_adj;

    if (source->enable_count == source->enable_max) {
        enable_changed = 1;
    }
    source->asserted += assert_adj;

    old_pending = source->pending;
    source->pending = source->asserted &&
      (source->enable_count == source->enable_max);

    if (old_pending != source->pending) {
        pending_changed = 1;
    }
    if (pending_changed) {
        if (source->pending) {
            source->parent->pending++;
            if (source->parent->pending == 1) {
                cpu_interrupt(first_cpu, CPU_INTERRUPT_HARD);
            }
        } else {
            source->parent->pending--;
            if (source->parent->pending == 0) {
                cpu_reset_interrupt(first_cpu, CPU_INTERRUPT_HARD);
            }
        }
    }

    if (enable_changed || assert_adj || pending_changed) {
        trace_sh_intc_sources(source->parent->pending, source->asserted,
                              source->enable_count, source->enable_max,
                              source->vect, source->asserted ? "asserted " :
                              assert_adj ? "deasserted" : "",
                              enable_changed == 1 ? "enabled " :
                              enable_changed == -1 ? "disabled " : "",
                              source->pending ? "pending" : "");
    }
}

static void sh_intc_set_irq(void *opaque, int n, int level)
{
    struct intc_desc *desc = opaque;
    struct intc_source *source = &desc->sources[n];

    if (level && !source->asserted) {
        sh_intc_toggle_source(source, 0, 1);
    } else if (!level && source->asserted) {
        sh_intc_toggle_source(source, 0, -1);
    }
}

int sh_intc_get_pending_vector(struct intc_desc *desc, int imask)
{
    unsigned int i;

    /* slow: use a linked lists of pending sources instead */
    /* wrong: take interrupt priority into account (one list per priority) */

    if (imask == 0x0f) {
        return -1; /* FIXME, update code to include priority per source */
    }

    for (i = 0; i < desc->nr_sources; i++) {
        struct intc_source *source = &desc->sources[i];

        if (source->pending) {
            trace_sh_intc_pending(desc->pending, source->vect);
            return source->vect;
        }
    }
    g_assert_not_reached();
}

typedef enum {
    INTC_MODE_NONE,
    INTC_MODE_DUAL_SET,
    INTC_MODE_DUAL_CLR,
    INTC_MODE_ENABLE_REG,
    INTC_MODE_MASK_REG,
} SHIntCMode;
#define INTC_MODE_IS_PRIO 0x80

static SHIntCMode sh_intc_mode(unsigned long address, unsigned long set_reg,
                               unsigned long clr_reg)
{
    if (address != A7ADDR(set_reg) && address != A7ADDR(clr_reg)) {
        return INTC_MODE_NONE;
    }
    if (set_reg && clr_reg) {
        return address == A7ADDR(set_reg) ?
               INTC_MODE_DUAL_SET : INTC_MODE_DUAL_CLR;
    }
    return set_reg ? INTC_MODE_ENABLE_REG : INTC_MODE_MASK_REG;
}

static void sh_intc_locate(struct intc_desc *desc,
                           unsigned long address,
                           unsigned long **datap,
                           intc_enum **enums,
                           unsigned int *first,
                           unsigned int *width,
                           unsigned int *modep)
{
    SHIntCMode mode;
    unsigned int i;

    /* this is slow but works for now */

    if (desc->mask_regs) {
        for (i = 0; i < desc->nr_mask_regs; i++) {
            struct intc_mask_reg *mr = &desc->mask_regs[i];

            mode = sh_intc_mode(address, mr->set_reg, mr->clr_reg);
            if (mode != INTC_MODE_NONE) {
                *modep = mode;
                *datap = &mr->value;
                *enums = mr->enum_ids;
                *first = mr->reg_width - 1;
                *width = 1;
                return;
            }
        }
    }

    if (desc->prio_regs) {
        for (i = 0; i < desc->nr_prio_regs; i++) {
            struct intc_prio_reg *pr = &desc->prio_regs[i];

            mode = sh_intc_mode(address, pr->set_reg, pr->clr_reg);
            if (mode != INTC_MODE_NONE) {
                *modep = mode | INTC_MODE_IS_PRIO;
                *datap = &pr->value;
                *enums = pr->enum_ids;
                *first = pr->reg_width / pr->field_width - 1;
                *width = pr->field_width;
                return;
            }
        }
    }
    g_assert_not_reached();
}

static void sh_intc_toggle_mask(struct intc_desc *desc, intc_enum id,
                                int enable, int is_group)
{
    struct intc_source *source = &desc->sources[id];

    if (!id) {
        return;
    }
    if (!source->next_enum_id && (!source->enable_max || !source->vect)) {
        qemu_log_mask(LOG_UNIMP,
                      "sh_intc: reserved interrupt source %d modified\n", id);
        return;
    }

    if (source->vect) {
        sh_intc_toggle_source(source, enable ? 1 : -1, 0);
    }

    if ((is_group || !source->vect) && source->next_enum_id) {
        sh_intc_toggle_mask(desc, source->next_enum_id, enable, 1);
    }

    if (!source->vect) {
        trace_sh_intc_set(id, !!enable);
    }
}

static uint64_t sh_intc_read(void *opaque, hwaddr offset, unsigned size)
{
    struct intc_desc *desc = opaque;
    intc_enum *enum_ids;
    unsigned int first;
    unsigned int width;
    unsigned int mode;
    unsigned long *valuep;

    sh_intc_locate(desc, (unsigned long)offset, &valuep,
                   &enum_ids, &first, &width, &mode);
    trace_sh_intc_read(size, (uint64_t)offset, *valuep);
    return *valuep;
}

static void sh_intc_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    struct intc_desc *desc = opaque;
    intc_enum *enum_ids;
    unsigned int first;
    unsigned int width;
    unsigned int mode;
    unsigned long *valuep;
    unsigned int k;
    unsigned long mask;

    trace_sh_intc_write(size, (uint64_t)offset, value);
    sh_intc_locate(desc, (unsigned long)offset, &valuep,
                   &enum_ids, &first, &width, &mode);
    switch (mode) {
    case INTC_MODE_ENABLE_REG | INTC_MODE_IS_PRIO:
        break;
    case INTC_MODE_DUAL_SET:
        value |= *valuep;
        break;
    case INTC_MODE_DUAL_CLR:
        value = *valuep & ~value;
        break;
    default:
        g_assert_not_reached();
    }

    for (k = 0; k <= first; k++) {
        mask = (1 << width) - 1;
        mask <<= (first - k) * width;

        if ((*valuep & mask) != (value & mask)) {
            sh_intc_toggle_mask(desc, enum_ids[k], value & mask, 0);
        }
    }

    *valuep = value;
}

static const MemoryRegionOps sh_intc_ops = {
    .read = sh_intc_read,
    .write = sh_intc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void sh_intc_register_source(struct intc_desc *desc,
                                    intc_enum source,
                                    struct intc_group *groups,
                                    int nr_groups)
{
    unsigned int i, k;
    intc_enum id;

    if (desc->mask_regs) {
        for (i = 0; i < desc->nr_mask_regs; i++) {
            struct intc_mask_reg *mr = &desc->mask_regs[i];

            for (k = 0; k < ARRAY_SIZE(mr->enum_ids); k++) {
                id = mr->enum_ids[k];
                if (id && id == source) {
                    desc->sources[id].enable_max++;
                }
            }
        }
    }

    if (desc->prio_regs) {
        for (i = 0; i < desc->nr_prio_regs; i++) {
            struct intc_prio_reg *pr = &desc->prio_regs[i];

            for (k = 0; k < ARRAY_SIZE(pr->enum_ids); k++) {
                id = pr->enum_ids[k];
                if (id && id == source) {
                    desc->sources[id].enable_max++;
                }
            }
        }
    }

    if (groups) {
        for (i = 0; i < nr_groups; i++) {
            struct intc_group *gr = &groups[i];

            for (k = 0; k < ARRAY_SIZE(gr->enum_ids); k++) {
                id = gr->enum_ids[k];
                if (id && id == source) {
                    desc->sources[id].enable_max++;
                }
            }
        }
    }

}

void sh_intc_register_sources(struct intc_desc *desc,
                              struct intc_vect *vectors,
                              int nr_vectors,
                              struct intc_group *groups,
                              int nr_groups)
{
    unsigned int i, k;
    intc_enum id;
    struct intc_source *s;

    for (i = 0; i < nr_vectors; i++) {
        struct intc_vect *vect = &vectors[i];

        sh_intc_register_source(desc, vect->enum_id, groups, nr_groups);
        id = vect->enum_id;
        if (id) {
            s = &desc->sources[id];
            s->vect = vect->vect;
            trace_sh_intc_register("source", vect->enum_id, s->vect,
                                   s->enable_count, s->enable_max);
        }
    }

    if (groups) {
        for (i = 0; i < nr_groups; i++) {
            struct intc_group *gr = &groups[i];

            id = gr->enum_id;
            s = &desc->sources[id];
            s->next_enum_id = gr->enum_ids[0];

            for (k = 1; k < ARRAY_SIZE(gr->enum_ids); k++) {
                if (gr->enum_ids[k]) {
                    id = gr->enum_ids[k - 1];
                    s = &desc->sources[id];
                    s->next_enum_id = gr->enum_ids[k];
                }
            }
            trace_sh_intc_register("group", gr->enum_id, 0xffff,
                                   s->enable_count, s->enable_max);
        }
    }
}

static unsigned int sh_intc_register(MemoryRegion *sysmem,
                                     struct intc_desc *desc,
                                     const unsigned long address,
                                     const char *type,
                                     const char *action,
                                     const unsigned int index)
{
    char name[60];
    MemoryRegion *iomem, *iomem_p4, *iomem_a7;

    if (!address) {
        return 0;
    }

    iomem = &desc->iomem;
    iomem_p4 = &desc->iomem_aliases[index];
    iomem_a7 = iomem_p4 + 1;

    snprintf(name, sizeof(name), "intc-%s-%s-%s", type, action, "p4");
    memory_region_init_alias(iomem_p4, NULL, name, iomem, A7ADDR(address), 4);
    memory_region_add_subregion(sysmem, P4ADDR(address), iomem_p4);

    snprintf(name, sizeof(name), "intc-%s-%s-%s", type, action, "a7");
    memory_region_init_alias(iomem_a7, NULL, name, iomem, A7ADDR(address), 4);
    memory_region_add_subregion(sysmem, A7ADDR(address), iomem_a7);

    /* used to increment aliases index */
    return 2;
}

int sh_intc_init(MemoryRegion *sysmem,
                 struct intc_desc *desc,
                 int nr_sources,
                 struct intc_mask_reg *mask_regs,
                 int nr_mask_regs,
                 struct intc_prio_reg *prio_regs,
                 int nr_prio_regs)
{
    unsigned int i, j;

    desc->pending = 0;
    desc->nr_sources = nr_sources;
    desc->mask_regs = mask_regs;
    desc->nr_mask_regs = nr_mask_regs;
    desc->prio_regs = prio_regs;
    desc->nr_prio_regs = nr_prio_regs;
    /* Allocate 4 MemoryRegions per register (2 actions * 2 aliases) */
    desc->iomem_aliases = g_new0(MemoryRegion,
                                 (nr_mask_regs + nr_prio_regs) * 4);
    desc->sources = g_new0(struct intc_source, nr_sources);
    for (i = 0; i < nr_sources; i++) {
        desc->sources[i].parent = desc;
    }
    desc->irqs = qemu_allocate_irqs(sh_intc_set_irq, desc, nr_sources);
    memory_region_init_io(&desc->iomem, NULL, &sh_intc_ops, desc, "intc",
                          0x100000000ULL);
    j = 0;
    if (desc->mask_regs) {
        for (i = 0; i < desc->nr_mask_regs; i++) {
            struct intc_mask_reg *mr = &desc->mask_regs[i];

            j += sh_intc_register(sysmem, desc, mr->set_reg, "mask", "set", j);
            j += sh_intc_register(sysmem, desc, mr->clr_reg, "mask", "clr", j);
        }
    }

    if (desc->prio_regs) {
        for (i = 0; i < desc->nr_prio_regs; i++) {
            struct intc_prio_reg *pr = &desc->prio_regs[i];

            j += sh_intc_register(sysmem, desc, pr->set_reg, "prio", "set", j);
            j += sh_intc_register(sysmem, desc, pr->clr_reg, "prio", "clr", j);
        }
    }

    return 0;
}

/*
 * Assert level <n> IRL interrupt.
 * 0:deassert. 1:lowest priority,... 15:highest priority
 */
void sh_intc_set_irl(void *opaque, int n, int level)
{
    struct intc_source *s = opaque;
    int i, irl = level ^ 15;
    intc_enum id = s->next_enum_id;

    for (i = 0; id; id = s->next_enum_id, i++) {
        s = &s->parent->sources[id];
        if (i == irl) {
            sh_intc_toggle_source(s, s->enable_count ? 0 : 1,
                                  s->asserted ? 0 : 1);
        } else if (s->asserted) {
            sh_intc_toggle_source(s, 0, -1);
        }
    }
}
