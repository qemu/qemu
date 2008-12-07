/*
 * SuperH interrupt controller module
 *
 * Copyright (c) 2007 Magnus Damm
 * Based on sh_timer.c and arm_timer.c by Paul Brook
 * Copyright (c) 2005-2006 CodeSourcery.
 *
 * This code is licenced under the GPL.
 */

#include <assert.h>
#include "sh_intc.h"
#include "hw.h"
#include "sh.h"

//#define DEBUG_INTC
//#define DEBUG_INTC_SOURCES

#define INTC_A7(x) ((x) & 0x1fffffff)
#define INTC_ARRAY(x) (sizeof(x) / sizeof(x[0]))

void sh_intc_toggle_source(struct intc_source *source,
			   int enable_adj, int assert_adj)
{
    int enable_changed = 0;
    int pending_changed = 0;
    int old_pending;

    if ((source->enable_count == source->enable_max) && (enable_adj == -1))
        enable_changed = -1;

    source->enable_count += enable_adj;

    if (source->enable_count == source->enable_max)
        enable_changed = 1;

    source->asserted += assert_adj;

    old_pending = source->pending;
    source->pending = source->asserted &&
      (source->enable_count == source->enable_max);

    if (old_pending != source->pending)
        pending_changed = 1;

    if (pending_changed) {
        if (source->pending) {
            source->parent->pending++;
	    if (source->parent->pending == 1)
                cpu_interrupt(first_cpu, CPU_INTERRUPT_HARD);
	}
	else {
            source->parent->pending--;
	    if (source->parent->pending == 0)
                cpu_reset_interrupt(first_cpu, CPU_INTERRUPT_HARD);
	}
    }

  if (enable_changed || assert_adj || pending_changed) {
#ifdef DEBUG_INTC_SOURCES
            printf("sh_intc: (%d/%d/%d/%d) interrupt source 0x%x %s%s%s\n",
		   source->parent->pending,
		   source->asserted,
		   source->enable_count,
		   source->enable_max,
		   source->vect,
		   source->asserted ? "asserted " :
		   assert_adj ? "deasserted" : "",
		   enable_changed == 1 ? "enabled " :
		   enable_changed == -1 ? "disabled " : "",
		   source->pending ? "pending" : "");
#endif
  }
}

static void sh_intc_set_irq (void *opaque, int n, int level)
{
  struct intc_desc *desc = opaque;
  struct intc_source *source = &(desc->sources[n]);

  if (level && !source->asserted)
    sh_intc_toggle_source(source, 0, 1);
  else if (!level && source->asserted)
    sh_intc_toggle_source(source, 0, -1);
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
        struct intc_source *source = desc->sources + i;

	if (source->pending) {
#ifdef DEBUG_INTC_SOURCES
            printf("sh_intc: (%d) returning interrupt source 0x%x\n",
		   desc->pending, source->vect);
#endif
            return source->vect;
	}
    }

    assert(0);
}

#define INTC_MODE_NONE       0
#define INTC_MODE_DUAL_SET   1
#define INTC_MODE_DUAL_CLR   2
#define INTC_MODE_ENABLE_REG 3
#define INTC_MODE_MASK_REG   4
#define INTC_MODE_IS_PRIO    8

static unsigned int sh_intc_mode(unsigned long address,
				 unsigned long set_reg, unsigned long clr_reg)
{
    if ((address != INTC_A7(set_reg)) &&
	(address != INTC_A7(clr_reg)))
        return INTC_MODE_NONE;

    if (set_reg && clr_reg) {
        if (address == INTC_A7(set_reg))
            return INTC_MODE_DUAL_SET;
	else
            return INTC_MODE_DUAL_CLR;
    }

    if (set_reg)
        return INTC_MODE_ENABLE_REG;
    else
        return INTC_MODE_MASK_REG;
}

static void sh_intc_locate(struct intc_desc *desc,
			   unsigned long address,
			   unsigned long **datap,
			   intc_enum **enums,
			   unsigned int *first,
			   unsigned int *width,
			   unsigned int *modep)
{
    unsigned int i, mode;

    /* this is slow but works for now */

    if (desc->mask_regs) {
        for (i = 0; i < desc->nr_mask_regs; i++) {
	    struct intc_mask_reg *mr = desc->mask_regs + i;

	    mode = sh_intc_mode(address, mr->set_reg, mr->clr_reg);
	    if (mode == INTC_MODE_NONE)
                continue;

	    *modep = mode;
	    *datap = &mr->value;
	    *enums = mr->enum_ids;
	    *first = mr->reg_width - 1;
	    *width = 1;
	    return;
	}
    }

    if (desc->prio_regs) {
        for (i = 0; i < desc->nr_prio_regs; i++) {
	    struct intc_prio_reg *pr = desc->prio_regs + i;

	    mode = sh_intc_mode(address, pr->set_reg, pr->clr_reg);
	    if (mode == INTC_MODE_NONE)
                continue;

	    *modep = mode | INTC_MODE_IS_PRIO;
	    *datap = &pr->value;
	    *enums = pr->enum_ids;
	    *first = (pr->reg_width / pr->field_width) - 1;
	    *width = pr->field_width;
	    return;
	}
    }

    assert(0);
}

static void sh_intc_toggle_mask(struct intc_desc *desc, intc_enum id,
				int enable, int is_group)
{
    struct intc_source *source = desc->sources + id;

    if (!id)
	return;

    if (!source->next_enum_id && (!source->enable_max || !source->vect)) {
#ifdef DEBUG_INTC_SOURCES
        printf("sh_intc: reserved interrupt source %d modified\n", id);
#endif
	return;
    }

    if (source->vect)
        sh_intc_toggle_source(source, enable ? 1 : -1, 0);

#ifdef DEBUG_INTC
    else {
        printf("setting interrupt group %d to %d\n", id, !!enable);
    }
#endif

    if ((is_group || !source->vect) && source->next_enum_id) {
        sh_intc_toggle_mask(desc, source->next_enum_id, enable, 1);
    }

#ifdef DEBUG_INTC
    if (!source->vect) {
        printf("setting interrupt group %d to %d - done\n", id, !!enable);
    }
#endif
}

static uint32_t sh_intc_read(void *opaque, target_phys_addr_t offset)
{
    struct intc_desc *desc = opaque;
    intc_enum *enum_ids = NULL;
    unsigned int first = 0;
    unsigned int width = 0;
    unsigned int mode = 0;
    unsigned long *valuep;

#ifdef DEBUG_INTC
    printf("sh_intc_read 0x%lx\n", (unsigned long) offset);
#endif

    sh_intc_locate(desc, (unsigned long)offset, &valuep, 
		   &enum_ids, &first, &width, &mode);
    return *valuep;
}

static void sh_intc_write(void *opaque, target_phys_addr_t offset,
			  uint32_t value)
{
    struct intc_desc *desc = opaque;
    intc_enum *enum_ids = NULL;
    unsigned int first = 0;
    unsigned int width = 0;
    unsigned int mode = 0;
    unsigned int k;
    unsigned long *valuep;
    unsigned long mask;

#ifdef DEBUG_INTC
    printf("sh_intc_write 0x%lx 0x%08x\n", (unsigned long) offset, value);
#endif

    sh_intc_locate(desc, (unsigned long)offset, &valuep, 
		   &enum_ids, &first, &width, &mode);

    switch (mode) {
    case INTC_MODE_ENABLE_REG | INTC_MODE_IS_PRIO: break;
    case INTC_MODE_DUAL_SET: value |= *valuep; break;
    case INTC_MODE_DUAL_CLR: value = *valuep & ~value; break;
    default: assert(0);
    }

    for (k = 0; k <= first; k++) {
        mask = ((1 << width) - 1) << ((first - k) * width);

	if ((*valuep & mask) == (value & mask))
            continue;
#if 0
	printf("k = %d, first = %d, enum = %d, mask = 0x%08x\n", 
	       k, first, enum_ids[k], (unsigned int)mask);
#endif
        sh_intc_toggle_mask(desc, enum_ids[k], value & mask, 0);
    }

    *valuep = value;

#ifdef DEBUG_INTC
    printf("sh_intc_write 0x%lx -> 0x%08x\n", (unsigned long) offset, value);
#endif
}

static CPUReadMemoryFunc *sh_intc_readfn[] = {
    sh_intc_read,
    sh_intc_read,
    sh_intc_read
};

static CPUWriteMemoryFunc *sh_intc_writefn[] = {
    sh_intc_write,
    sh_intc_write,
    sh_intc_write
};

struct intc_source *sh_intc_source(struct intc_desc *desc, intc_enum id)
{
    if (id)
        return desc->sources + id;

    return NULL;
}

static void sh_intc_register(struct intc_desc *desc, 
			     unsigned long address)
{
    if (address) {
        cpu_register_physical_memory_offset(P4ADDR(address), 4,
                                            desc->iomemtype, INTC_A7(address));
        cpu_register_physical_memory_offset(A7ADDR(address), 4,
                                            desc->iomemtype, INTC_A7(address));
    }
}

static void sh_intc_register_source(struct intc_desc *desc,
				    intc_enum source,
				    struct intc_group *groups,
				    int nr_groups)
{
    unsigned int i, k;
    struct intc_source *s;

    if (desc->mask_regs) {
        for (i = 0; i < desc->nr_mask_regs; i++) {
	    struct intc_mask_reg *mr = desc->mask_regs + i;

	    for (k = 0; k < INTC_ARRAY(mr->enum_ids); k++) {
                if (mr->enum_ids[k] != source)
                    continue;

		s = sh_intc_source(desc, mr->enum_ids[k]);
		if (s)
                    s->enable_max++;
	    }
	}
    }

    if (desc->prio_regs) {
        for (i = 0; i < desc->nr_prio_regs; i++) {
	    struct intc_prio_reg *pr = desc->prio_regs + i;

	    for (k = 0; k < INTC_ARRAY(pr->enum_ids); k++) {
                if (pr->enum_ids[k] != source)
                    continue;

		s = sh_intc_source(desc, pr->enum_ids[k]);
		if (s)
                    s->enable_max++;
	    }
	}
    }

    if (groups) {
        for (i = 0; i < nr_groups; i++) {
	    struct intc_group *gr = groups + i;

	    for (k = 0; k < INTC_ARRAY(gr->enum_ids); k++) {
                if (gr->enum_ids[k] != source)
                    continue;

		s = sh_intc_source(desc, gr->enum_ids[k]);
		if (s)
                    s->enable_max++;
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
    struct intc_source *s;

    for (i = 0; i < nr_vectors; i++) {
	struct intc_vect *vect = vectors + i;

	sh_intc_register_source(desc, vect->enum_id, groups, nr_groups);
	s = sh_intc_source(desc, vect->enum_id);
	if (s)
	    s->vect = vect->vect;

#ifdef DEBUG_INTC_SOURCES
	printf("sh_intc: registered source %d -> 0x%04x (%d/%d)\n",
	       vect->enum_id, s->vect, s->enable_count, s->enable_max);
#endif
    }

    if (groups) {
        for (i = 0; i < nr_groups; i++) {
	    struct intc_group *gr = groups + i;

	    s = sh_intc_source(desc, gr->enum_id);
	    s->next_enum_id = gr->enum_ids[0];

	    for (k = 1; k < INTC_ARRAY(gr->enum_ids); k++) {
                if (!gr->enum_ids[k])
                    continue;

		s = sh_intc_source(desc, gr->enum_ids[k - 1]);
		s->next_enum_id = gr->enum_ids[k];
	    }

#ifdef DEBUG_INTC_SOURCES
	    printf("sh_intc: registered group %d (%d/%d)\n",
		   gr->enum_id, s->enable_count, s->enable_max);
#endif
	}
    }
}

int sh_intc_init(struct intc_desc *desc,
		 int nr_sources,
		 struct intc_mask_reg *mask_regs,
		 int nr_mask_regs,
		 struct intc_prio_reg *prio_regs,
		 int nr_prio_regs)
{
    unsigned int i;

    desc->pending = 0;
    desc->nr_sources = nr_sources;
    desc->mask_regs = mask_regs;
    desc->nr_mask_regs = nr_mask_regs;
    desc->prio_regs = prio_regs;
    desc->nr_prio_regs = nr_prio_regs;

    i = sizeof(struct intc_source) * nr_sources;
    desc->sources = malloc(i);
    if (!desc->sources)
        return -1;

    memset(desc->sources, 0, i);
    for (i = 0; i < desc->nr_sources; i++) {
        struct intc_source *source = desc->sources + i;

        source->parent = desc;
    }

    desc->irqs = qemu_allocate_irqs(sh_intc_set_irq, desc, nr_sources);
 
    desc->iomemtype = cpu_register_io_memory(0, sh_intc_readfn,
					     sh_intc_writefn, desc);
    if (desc->mask_regs) {
        for (i = 0; i < desc->nr_mask_regs; i++) {
	    struct intc_mask_reg *mr = desc->mask_regs + i;

	    sh_intc_register(desc, mr->set_reg);
	    sh_intc_register(desc, mr->clr_reg);
	}
    }

    if (desc->prio_regs) {
        for (i = 0; i < desc->nr_prio_regs; i++) {
	    struct intc_prio_reg *pr = desc->prio_regs + i;

	    sh_intc_register(desc, pr->set_reg);
	    sh_intc_register(desc, pr->clr_reg);
	}
    }

    return 0;
}

/* Assert level <n> IRL interrupt. 
   0:deassert. 1:lowest priority,... 15:highest priority. */
void sh_intc_set_irl(void *opaque, int n, int level)
{
    struct intc_source *s = opaque;
    int i, irl = level ^ 15;
    for (i = 0; (s = sh_intc_source(s->parent, s->next_enum_id)); i++) {
	if (i == irl)
	    sh_intc_toggle_source(s, s->enable_count?0:1, s->asserted?0:1);
	else
	    if (s->asserted)
	        sh_intc_toggle_source(s, 0, -1);
    }
}
