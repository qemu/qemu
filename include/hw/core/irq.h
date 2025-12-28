#ifndef QEMU_IRQ_H
#define QEMU_IRQ_H

#include "qom/object.h"

/* Generic IRQ/GPIO pin infrastructure.  */

#define TYPE_IRQ "irq"
OBJECT_DECLARE_SIMPLE_TYPE(IRQState, IRQ)

struct IRQState {
    Object parent_obj;

    qemu_irq_handler handler;
    void *opaque;
    int n;
};

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

/*
 * Init a single IRQ. The irq is assigned with a handler, an opaque data
 * and the interrupt number. The caller must free this with qemu_free_irq().
 * If you are using this inside a device's init or realize method, then
 * qemu_init_irq_child() is probably a better choice to avoid the need
 * to manually clean up the IRQ.
 */
void qemu_init_irq(IRQState *irq, qemu_irq_handler handler, void *opaque,
                   int n);

/**
 * qemu_init_irq_child: Initialize IRQ and make it a QOM child
 * @parent: QOM object which owns this IRQ
 * @propname: child property name
 * @irq: pointer to IRQState to initialize
 * @handler: handler function for incoming interrupts
 * @opaque: opaque data to pass to @handler
 * @n: interrupt number to pass to @handler
 *
 * Init a single IRQ and make the IRQ object a child of @parent with
 * the child-property name @propname. The IRQ object will thus be
 * automatically freed when @parent is destroyed.
 */
void qemu_init_irq_child(Object *parent, const char *propname,
                         IRQState *irq, qemu_irq_handler handler,
                         void *opaque, int n);


/**
 * qemu_init_irqs: Initialize an array of IRQs.
 *
 * @irq: Array of IRQs to initialize
 * @count: number of IRQs to initialize
 * @handler: handler to assign to each IRQ
 * @opaque: opaque data to pass to @handler
 */
void qemu_init_irqs(IRQState irq[], size_t count,
                    qemu_irq_handler handler, void *opaque);

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
