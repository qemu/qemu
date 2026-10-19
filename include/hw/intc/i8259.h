#ifndef HW_I8259_H
#define HW_I8259_H

#include "hw/intc/intc.h"

/* i8259.c */

typedef struct I8259PICState I8259PICState;

extern I8259PICState *isa_pic;

/*
 * i8259_init()
 *
 * Create a i8259 device on an ISA @bus,
 * connect its output to @parent_irq_in,
 * return an (allocated) array of 16 input IRQs.
 */
qemu_irq *i8259_init(ISABus *bus, qemu_irq parent_irq_in);
qemu_irq *kvm_i8259_init(ISABus *bus);
int pic_get_output(I8259PICState *s);
int pic_read_irq(I8259PICState *s);
bool i8259_pic_get_statistics(InterruptStatsProvider *obj,
                              uint64_t **irq_counts,
                              unsigned int *nb_irqs);
void i8259_pic_print_info(InterruptStatsProvider *obj, GString *buf);

#endif
