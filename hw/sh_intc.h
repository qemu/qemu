#ifndef __SH_INTC_H__
#define __SH_INTC_H__

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

#define INTC_GROUP(enum_id, ids...) { enum_id, { ids } }

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

#define _INTC_ARRAY(a) a, sizeof(a)/sizeof(*a)

struct intc_source {
    unsigned short vect;
    intc_enum next_enum_id;

    int asserted;
    int enable_count;
    int enable_max;
};

struct intc_desc {
    struct intc_source *sources;
    int nr_sources;
    struct intc_mask_reg *mask_regs;
    int nr_mask_regs;
    struct intc_prio_reg *prio_regs;
    int nr_prio_regs;

    int iomemtype;
};

struct intc_source *sh_intc_source(struct intc_desc *desc, intc_enum id);

void sh_intc_register_sources(struct intc_desc *desc,
			      struct intc_vect *vectors,
			      int nr_vectors,
			      struct intc_group *groups,
			      int nr_groups);

int sh_intc_init(struct intc_desc *desc,
		 int nr_sources,
		 struct intc_mask_reg *mask_regs,
		 int nr_mask_regs,
		 struct intc_prio_reg *prio_regs,
		 int nr_prio_regs);

#endif /* __SH_INTC_H__ */
