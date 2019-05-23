/*
 * QEMU IRQ/GPIO common code.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "hw/irq.h"
#include "qom/object.h"

#define IRQ(obj) OBJECT_CHECK(struct IRQState, (obj), TYPE_IRQ)

struct IRQState {
    Object parent_obj;

    qemu_irq_handler handler;
    void *opaque;
    int n;
};

void qemu_set_irq(qemu_irq irq, int level)
{
    if (!irq)
        return;

    irq->handler(irq->opaque, irq->n, level);
}

qemu_irq *qemu_extend_irqs(qemu_irq *old, int n_old, qemu_irq_handler handler,
                           void *opaque, int n)
{
    qemu_irq *s;
    int i;

    if (!old) {
        n_old = 0;
    }
    s = old ? g_renew(qemu_irq, old, n + n_old) : g_new(qemu_irq, n);
    for (i = n_old; i < n + n_old; i++) {
        s[i] = qemu_allocate_irq(handler, opaque, i);
    }
    return s;
}

qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n)
{
    return qemu_extend_irqs(NULL, 0, handler, opaque, n);
}

qemu_irq qemu_allocate_irq(qemu_irq_handler handler, void *opaque, int n)
{
    struct IRQState *irq;

    irq = IRQ(object_new(TYPE_IRQ));
    irq->handler = handler;
    irq->opaque = opaque;
    irq->n = n;

    return irq;
}

void qemu_free_irqs(qemu_irq *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        qemu_free_irq(s[i]);
    }
    g_free(s);
}

void qemu_free_irq(qemu_irq irq)
{
    object_unref(OBJECT(irq));
}

static void qemu_notirq(void *opaque, int line, int level)
{
    struct IRQState *irq = opaque;

    irq->handler(irq->opaque, irq->n, !level);
}

qemu_irq qemu_irq_invert(qemu_irq irq)
{
    /* The default state for IRQs is low, so raise the output now.  */
    qemu_irq_raise(irq);
    return qemu_allocate_irq(qemu_notirq, irq, 0);
}

static void qemu_splitirq(void *opaque, int line, int level)
{
    struct IRQState **irq = opaque;
    irq[0]->handler(irq[0]->opaque, irq[0]->n, level);
    irq[1]->handler(irq[1]->opaque, irq[1]->n, level);
}

qemu_irq qemu_irq_split(qemu_irq irq1, qemu_irq irq2)
{
    qemu_irq *s = g_malloc0(2 * sizeof(qemu_irq));
    s[0] = irq1;
    s[1] = irq2;
    return qemu_allocate_irq(qemu_splitirq, s, 0);
}

static void proxy_irq_handler(void *opaque, int n, int level)
{
    qemu_irq **target = opaque;

    if (*target) {
        qemu_set_irq((*target)[n], level);
    }
}

qemu_irq *qemu_irq_proxy(qemu_irq **target, int n)
{
    return qemu_allocate_irqs(proxy_irq_handler, target, n);
}

void qemu_irq_intercept_in(qemu_irq *gpio_in, qemu_irq_handler handler, int n)
{
    int i;
    qemu_irq *old_irqs = qemu_allocate_irqs(NULL, NULL, n);
    for (i = 0; i < n; i++) {
        *old_irqs[i] = *gpio_in[i];
        gpio_in[i]->handler = handler;
        gpio_in[i]->opaque = &old_irqs[i];
    }
}

static const TypeInfo irq_type_info = {
   .name = TYPE_IRQ,
   .parent = TYPE_OBJECT,
   .instance_size = sizeof(struct IRQState),
};

static void irq_register_types(void)
{
    type_register_static(&irq_type_info);
}

type_init(irq_register_types)
