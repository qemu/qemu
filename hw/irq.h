/* Generic IRQ/GPIO pin infrastructure.  */

typedef void (*qemu_irq_handler)(void *opaque, int n, int level);

typedef struct IRQState *qemu_irq;

void qemu_set_irq(qemu_irq irq, int level);

static inline void qemu_irq_raise(qemu_irq irq)
{
    qemu_set_irq(irq, 1);
}

static inline void qemu_irq_lower(qemu_irq irq)
{
    qemu_set_irq(irq, 0);
}

/* Returns an array of N IRQs.  */
qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n);

/* Returns a new IRQ with opposite polarity.  */
qemu_irq qemu_irq_invert(qemu_irq irq);
