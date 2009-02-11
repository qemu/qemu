#ifndef QEMU_IRQ_H
#define QEMU_IRQ_H

/* Generic IRQ/GPIO pin infrastructure.  */

/* FIXME: Rmove one of these.  */
typedef void (*qemu_irq_handler)(void *opaque, int n, int level);
typedef void SetIRQFunc(void *opaque, int irq_num, int level);

void qemu_set_irq(qemu_irq irq, int level);

static inline void qemu_irq_raise(qemu_irq irq)
{
    qemu_set_irq(irq, 1);
}

static inline void qemu_irq_lower(qemu_irq irq)
{
    qemu_set_irq(irq, 0);
}

static inline void qemu_irq_pulse(qemu_irq irq)
{
    qemu_set_irq(irq, 1);
    qemu_set_irq(irq, 0);
}

/* Returns an array of N IRQs.  */
qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n);
void qemu_free_irqs(qemu_irq *s);

/* Returns a new IRQ with opposite polarity.  */
qemu_irq qemu_irq_invert(qemu_irq irq);

#endif
