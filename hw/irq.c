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
#include "qemu-common.h"
#include "irq.h"

struct IRQState {
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

qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n)
{
    qemu_irq *s;
    struct IRQState *p;
    int i;

    s = (qemu_irq *)g_malloc0(sizeof(qemu_irq) * n);
    p = (struct IRQState *)g_malloc0(sizeof(struct IRQState) * n);
    for (i = 0; i < n; i++) {
        p->handler = handler;
        p->opaque = opaque;
        p->n = i;
        s[i] = p;
        p++;
    }
    return s;
}

void qemu_free_irqs(qemu_irq *s)
{
    g_free(s[0]);
    g_free(s);
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
    return qemu_allocate_irqs(qemu_notirq, irq, 1)[0];
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
    return qemu_allocate_irqs(qemu_splitirq, s, 1)[0];
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
        gpio_in[i]->opaque = old_irqs;
    }
}

void qemu_irq_intercept_out(qemu_irq **gpio_out, qemu_irq_handler handler, int n)
{
    qemu_irq *old_irqs = *gpio_out;
    *gpio_out = qemu_allocate_irqs(handler, old_irqs, n);
}
