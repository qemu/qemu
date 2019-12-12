#ifndef HW_I8259_H
#define HW_I8259_H

/* i8259.c */

extern DeviceState *isa_pic;
qemu_irq *i8259_init(ISABus *bus, qemu_irq parent_irq);
qemu_irq *kvm_i8259_init(ISABus *bus);
int pic_get_output(DeviceState *d);
int pic_read_irq(DeviceState *d);

#endif
