#ifndef SH_INTC_H
#define SH_INTC_H

#include "exec/memory.h"

typedef unsigned char intc_enum;

struct intc_vect {
    intc_enum enum_id;
    unsigned short vect;
};

#define INTC_VECT(enum_id, vect) { enum_id, vect }

struct intc_group {
    intc_enum enum_id;
    intc_enum enum_ids[32];
};

#define INTC_GROUP(enum_id, ...) { enum_id, {  __VA_ARGS__ } }

struct intc_mask_reg {
    unsigned long set_reg, clr_reg, reg_width;
    intc_enum enum_ids[32];
    unsigned long value;
};

struct intc_prio_reg {
    unsigned long set_reg, clr_reg, reg_width, field_width;
    intc_enum enum_ids[16];
    unsigned long value;
};

#define _INTC_ARRAY(a) a, ARRAY_SIZE(a)

struct intc_source {
    unsigned short vect;
    intc_enum next_enum_id;

    int asserted; /* emulates the interrupt signal line from device to intc */
    int enable_count;
    int enable_max;
    int pending; /* emulates the result of signal and masking */
    struct intc_desc *parent;
};

struct intc_desc {
    MemoryRegion iomem;
    MemoryRegion *iomem_aliases;
    qemu_irq *irqs;
    struct intc_source *sources;
    int nr_sources;
    struct intc_mask_reg *mask_regs;
    int nr_mask_regs;
    struct intc_prio_reg *prio_regs;
    int nr_prio_regs;
    int pending; /* number of interrupt sources that has pending set */
};

int sh_intc_get_pending_vector(struct intc_desc *desc, int imask);

void sh_intc_toggle_source(struct intc_source *source,
                           int enable_adj, int assert_adj);

void sh_intc_register_sources(struct intc_desc *desc,
                              struct intc_vect *vectors,
                              int nr_vectors,
                              struct intc_group *groups,
                              int nr_groups);

int sh_intc_init(MemoryRegion *sysmem,
                 struct intc_desc *desc,
                 int nr_sources,
                 struct intc_mask_reg *mask_regs,
                 int nr_mask_regs,
                 struct intc_prio_reg *prio_regs,
                 int nr_prio_regs);

void sh_intc_set_irl(void *opaque, int n, int level);

#endif /* SH_INTC_H */
