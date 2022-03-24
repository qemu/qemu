#ifndef QEMU_IRQ_H
#define QEMU_IRQ_H

/* Generic IRQ/GPIO pin infrastructure.  */

#define TYPE_IRQ "irq"

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

/* Returns an array of N IRQs. Each IRQ is assigned the argument handler and
 * opaque data.
 */
qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n);

/*
 * Allocates a single IRQ. The irq is assigned with a handler, an opaque
 * data and the interrupt number.
 */
qemu_irq qemu_allocate_irq(qemu_irq_handler handler, void *opaque, int n);

/* Extends an Array of IRQs. Old IRQs have their handlers and opaque data
 * preserved. New IRQs are assigned the argument handler and opaque data.
 */
qemu_irq *qemu_extend_irqs(qemu_irq *old, int n_old, qemu_irq_handler handler,
                                void *opaque, int n);

void qemu_free_irqs(qemu_irq *s, int n);
void qemu_free_irq(qemu_irq irq);

/* Returns a new IRQ with opposite polarity.  */
qemu_irq qemu_irq_invert(qemu_irq irq);

/* For internal use in qtest.  Similar to qemu_irq_split, but operating
   on an existing vector of qemu_irq.  */
void qemu_irq_intercept_in(qemu_irq *gpio_in, qemu_irq_handler handler, int n);

/**
 * qemu_irq_is_connected: Return true if IRQ line is wired up
 *
 * If a qemu_irq has a device on the other (receiving) end of it,
 * return true; otherwise return false.
 *
 * Usually device models don't need to care whether the machine model
 * has wired up their outbound qemu_irq lines, because functions like
 * qemu_set_irq() silently do nothing if there is nothing on the other
 * end of the line. However occasionally a device model will want to
 * provide default behaviour if its output is left floating, and
 * it can use this function to identify when that is the case.
 */
static inline bool qemu_irq_is_connected(qemu_irq irq)
{
    return irq != NULL;
}

#endif
